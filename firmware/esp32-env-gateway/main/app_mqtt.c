#include <inttypes.h>
#include <stdio.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mqtt_client.h"

#include "app_mqtt.h"
#include "app_net_status.h"
#include "app_secrets.h"

static const char *TAG = "env_mqtt";
static esp_mqtt_client_handle_t s_client;
static EventGroupHandle_t s_mqtt_events;
static QueueHandle_t s_report_queue;
static TaskHandle_t s_publish_task;
static portMUX_TYPE s_stats_lock = portMUX_INITIALIZER_UNLOCKED;
static app_mqtt_stats_t s_stats;

#define APP_MQTT_CONNECTED_BIT            BIT0
#define APP_MQTT_REPORT_QUEUE_LENGTH      32U
#define APP_MQTT_PUBLISH_TASK_STACK_SIZE  4096U
#define APP_MQTT_PUBLISH_TASK_PRIORITY    5U
#define APP_MQTT_MAX_PUBLISH_ATTEMPTS     3U
#define APP_MQTT_PUBLISH_RETRY_DELAY_MS   1000U

/* 由 CMake 将 EMQX Serverless 的公开根证书嵌入固件。 */
extern const uint8_t emqxsl_ca_crt_start[] asm("_binary_emqxsl_ca_crt_start");

static uint32_t app_mqtt_increment_stat(uint32_t *counter)
{
    uint32_t value;

    portENTER_CRITICAL(&s_stats_lock);
    value = ++(*counter);
    portEXIT_CRITICAL(&s_stats_lock);

    return value;
}

static void app_mqtt_publish_status(bool online)
{
    const char *payload = online ? "{\"online\":true}" : "{\"online\":false}";
    const int message_id = esp_mqtt_client_publish(s_client,
                                                   APP_MQTT_STATUS_TOPIC,
                                                   payload,
                                                   0,
                                                   1,
                                                   1);

    if (message_id < 0)
    {
        ESP_LOGW(TAG, "状态消息入队失败: %d", message_id);
    }
}

static void app_mqtt_event_handler(void *argument,
                                   esp_event_base_t event_base,
                                   int32_t event_id,
                                   void *event_data)
{
    const esp_mqtt_event_handle_t event = event_data;

    (void)argument;
    (void)event_base;

    switch ((esp_mqtt_event_id_t)event_id)
    {
        case MQTT_EVENT_CONNECTED:
            xEventGroupSetBits(s_mqtt_events, APP_MQTT_CONNECTED_BIT);
            app_net_status_set_mqtt_connected(true);
            ESP_LOGI(TAG, "MQTT 已连接: %s:%d", APP_MQTT_HOST, APP_MQTT_PORT);
            app_mqtt_publish_status(true);
            break;

        case MQTT_EVENT_DISCONNECTED:
            xEventGroupClearBits(s_mqtt_events, APP_MQTT_CONNECTED_BIT);
            app_net_status_set_mqtt_connected(false);
            ESP_LOGW(TAG, "MQTT 已断开，客户端将自动重连");
            break;

        case MQTT_EVENT_PUBLISHED:
            (void)app_mqtt_increment_stat(&s_stats.puback_count);
            ESP_LOGD(TAG, "MQTT 发布确认: id=%d", event->msg_id);
            break;

        case MQTT_EVENT_ERROR:
            xEventGroupClearBits(s_mqtt_events, APP_MQTT_CONNECTED_BIT);
            /* 错误事件不一定立刻伴随 DISCONNECTED，先同步网络快照。 */
            app_net_status_set_mqtt_connected(false);
            if (event->error_handle != NULL)
            {
                ESP_LOGW(TAG,
                         "MQTT 连接错误: type=%d tls=0x%x socket=%d",
                         event->error_handle->error_type,
                         event->error_handle->esp_tls_last_esp_err,
                         event->error_handle->esp_transport_sock_errno);
            }
            else
            {
                ESP_LOGW(TAG, "MQTT 连接错误");
            }
            break;

        default:
            break;
    }
}

static void app_format_signed_milli(int32_t value, char *buffer, size_t buffer_length)
{
    const int64_t magnitude = value < 0 ? -(int64_t)value : value;

    (void)snprintf(buffer,
                   buffer_length,
                   "%s%" PRIi64 ".%03" PRIi64,
                   value < 0 ? "-" : "",
                   magnitude / 1000,
                   magnitude % 1000);
}

static bool app_mqtt_publish_env_report_now(const app_protocol_env_report_t *report)
{
    char temperature[16];
    char payload[384];
    int payload_length;
    int message_id;

    if (report == NULL || !app_mqtt_is_connected())
    {
        return false;
    }

    app_format_signed_milli(report->temperature_milli_c, temperature, sizeof(temperature));
    payload_length = snprintf(payload,
                              sizeof(payload),
                              "{\"device_id\":\"%s\",\"sample\":%" PRIu32
                              ",\"uptime_ms\":%" PRIu32
                              ",\"temperature_c\":%s"
                              ",\"humidity_rh\":%" PRIu32 ".%03" PRIu32
                              ",\"illuminance_lux\":%" PRIu32 ".%03" PRIu32
                              ",\"valid\":{\"climate\":%s,\"illuminance\":%s,\"display\":%s}"
                              ",\"errors\":{\"aht20_comm\":%" PRIu32
                              ",\"bh1750_comm\":%" PRIu32
                              ",\"aht20_data\":%" PRIu32
                              ",\"bh1750_data\":%" PRIu32 "}}",
                              APP_MQTT_CLIENT_ID,
                              report->sample_sequence,
                              report->uptime_ms,
                              temperature,
                              report->humidity_milli_rh / 1000U,
                              report->humidity_milli_rh % 1000U,
                              report->illuminance_milli_lux / 1000U,
                              report->illuminance_milli_lux % 1000U,
                              (report->valid_flags & 0x01U) != 0U ? "true" : "false",
                              (report->valid_flags & 0x02U) != 0U ? "true" : "false",
                              (report->valid_flags & 0x04U) != 0U ? "true" : "false",
                              report->aht20_comm_errors,
                              report->bh1750_comm_errors,
                              report->aht20_data_errors,
                              report->bh1750_data_errors);

    if (payload_length < 0 || payload_length >= (int)sizeof(payload))
    {
        ESP_LOGW(TAG, "遥测 JSON 构造失败: %d", payload_length);
        return false;
    }

    message_id = esp_mqtt_client_publish(s_client,
                                         APP_MQTT_TELEMETRY_TOPIC,
                                         payload,
                                         payload_length,
                                         1,
                                         0);
    if (message_id < 0)
    {
        ESP_LOGW(TAG, "遥测消息入队 ESP-MQTT 失败: %d", message_id);
        return false;
    }

    ESP_LOGD(TAG, "遥测消息已交给 ESP-MQTT: sample=%" PRIu32 " id=%d",
             report->sample_sequence,
             message_id);
    return true;
}

static void app_mqtt_publish_task(void *argument)
{
    app_protocol_env_report_t report;

    (void)argument;

    while (true)
    {
        uint32_t attempt;
        bool accepted = false;

        if (xQueueReceive(s_report_queue, &report, portMAX_DELAY) != pdTRUE)
        {
            continue;
        }

        for (attempt = 0U; attempt < APP_MQTT_MAX_PUBLISH_ATTEMPTS; attempt++)
        {
            (void)xEventGroupWaitBits(s_mqtt_events,
                                      APP_MQTT_CONNECTED_BIT,
                                      pdFALSE,
                                      pdTRUE,
                                      portMAX_DELAY);

            if (app_mqtt_publish_env_report_now(&report))
            {
                (void)app_mqtt_increment_stat(&s_stats.publish_accepted_count);
                accepted = true;
                break;
            }

            (void)app_mqtt_increment_stat(&s_stats.publish_error_count);
            vTaskDelay(pdMS_TO_TICKS(APP_MQTT_PUBLISH_RETRY_DELAY_MS));
        }

        if (!accepted)
        {
            const uint32_t abandoned = app_mqtt_increment_stat(&s_stats.abandoned_count);
            ESP_LOGW(TAG,
                     "遥测消息重试耗尽: sample=%" PRIu32 " abandoned=%" PRIu32,
                     report.sample_sequence,
                     abandoned);
        }
    }
}

void app_mqtt_init(void)
{
    if (s_mqtt_events != NULL)
    {
        return;
    }

    s_mqtt_events = xEventGroupCreate();
    ESP_ERROR_CHECK(s_mqtt_events == NULL ? ESP_ERR_NO_MEM : ESP_OK);

    s_report_queue = xQueueCreate(APP_MQTT_REPORT_QUEUE_LENGTH,
                                  sizeof(app_protocol_env_report_t));
    ESP_ERROR_CHECK(s_report_queue == NULL ? ESP_ERR_NO_MEM : ESP_OK);

    ESP_ERROR_CHECK(xTaskCreate(app_mqtt_publish_task,
                                "mqtt_publish",
                                APP_MQTT_PUBLISH_TASK_STACK_SIZE,
                                NULL,
                                APP_MQTT_PUBLISH_TASK_PRIORITY,
                                &s_publish_task) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);

    ESP_LOGI(TAG, "遥测队列已创建: capacity=%u", APP_MQTT_REPORT_QUEUE_LENGTH);
}

void app_mqtt_start(void)
{
    const esp_mqtt_client_config_t config = {
        .broker = {
            .address = {
                .hostname = APP_MQTT_HOST,
                .transport = MQTT_TRANSPORT_OVER_SSL,
                .port = APP_MQTT_PORT,
            },
            .verification = {
                .certificate = (const char *)emqxsl_ca_crt_start,
            },
        },
        .credentials = {
            .username = APP_MQTT_USERNAME,
            .client_id = APP_MQTT_CLIENT_ID,
            .authentication = {
                .password = APP_MQTT_PASSWORD,
            },
        },
        .session = {
            .keepalive = 60,
            .protocol_ver = MQTT_PROTOCOL_V_3_1_1,
            .last_will = {
                .topic = APP_MQTT_STATUS_TOPIC,
                .msg = "{\"online\":false}",
                .qos = 1,
                .retain = 1,
            },
        },
        .network = {
            .reconnect_timeout_ms = 4000,
            .timeout_ms = 10000,
        },
    };

    app_mqtt_init();

    if (s_client != NULL)
    {
        return;
    }

    s_client = esp_mqtt_client_init(&config);
    ESP_ERROR_CHECK(s_client == NULL ? ESP_ERR_NO_MEM : ESP_OK);
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(s_client,
                                                   ESP_EVENT_ANY_ID,
                                                   app_mqtt_event_handler,
                                                   NULL));
    ESP_ERROR_CHECK(esp_mqtt_client_start(s_client));
}

bool app_mqtt_submit_env_report(const app_protocol_env_report_t *report)
{
    uint32_t overflow_count;

    if (report == NULL || s_report_queue == NULL)
    {
        return false;
    }

    if (xQueueSend(s_report_queue, report, 0U) == pdTRUE)
    {
        (void)app_mqtt_increment_stat(&s_stats.submitted_count);
        return true;
    }

    overflow_count = app_mqtt_increment_stat(&s_stats.queue_overflow_count);
    if (overflow_count == 1U || (overflow_count % 10U) == 0U)
    {
        ESP_LOGW(TAG,
                 "遥测队列已满，丢弃最新样本: sample=%" PRIu32 " overflows=%" PRIu32,
                 report->sample_sequence,
                 overflow_count);
    }

    return false;
}

bool app_mqtt_is_connected(void)
{
    return s_mqtt_events != NULL &&
           (xEventGroupGetBits(s_mqtt_events) & APP_MQTT_CONNECTED_BIT) != 0U;
}

void app_mqtt_get_stats(app_mqtt_stats_t *stats)
{
    if (stats == NULL)
    {
        return;
    }

    portENTER_CRITICAL(&s_stats_lock);
    *stats = s_stats;
    portEXIT_CRITICAL(&s_stats_lock);

    stats->pending_count = s_report_queue == NULL ?
                               0U :
                               (uint32_t)uxQueueMessagesWaiting(s_report_queue);
}
