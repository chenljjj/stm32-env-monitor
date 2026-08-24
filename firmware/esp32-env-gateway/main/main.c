#include <inttypes.h>

#include "app_mqtt.h"
#include "app_net_status.h"
#include "app_protocol.h"
#include "app_wifi.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/semphr.h"

#define APP_UART_PORT             UART_NUM_2
#define APP_UART_BAUD_RATE        115200
#define APP_UART_RX_PIN           GPIO_NUM_25
#define APP_UART_TX_PIN           GPIO_NUM_26
#define APP_UART_RX_BUFFER_SIZE   512
/* 未收满缓冲区时最多等待 20ms，确保 ACK 不会落后一个采样周期。 */
#define APP_UART_READ_TIMEOUT_MS  20
#define APP_UART_TX_MUTEX_TIMEOUT_MS  50

#define APP_PROTOCOL_FLAG_CLIMATE_VALID      0x01U
#define APP_PROTOCOL_FLAG_ILLUMINANCE_VALID  0x02U
#define APP_PROTOCOL_FLAG_DISPLAY_READY      0x04U

static const char *TAG = "env_gateway";
static uint16_t app_uart_tx_sequence;
static SemaphoreHandle_t app_uart_tx_mutex;
/* ACK 丢失后的重传仍会抵达 ESP32；只确认，不重复入队。 */
static bool app_has_last_env_report_sequence;
static uint16_t app_last_env_report_sequence;
static uint32_t app_duplicate_env_report_count;

static void app_uart_init(void)
{
    const uart_config_t config = {
        .baud_rate = APP_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_param_config(APP_UART_PORT, &config));
    ESP_ERROR_CHECK(uart_set_pin(APP_UART_PORT,
                                 APP_UART_TX_PIN,
                                 APP_UART_RX_PIN,
                                 UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(APP_UART_PORT,
                                       APP_UART_RX_BUFFER_SIZE,
                                       0,
                                       0,
                                       NULL,
                                       0));
    app_uart_tx_mutex = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(app_uart_tx_mutex == NULL ? ESP_ERR_NO_MEM : ESP_OK);
}

/* ACK 和 NET_STATUS 可能来自不同任务，必须成帧互斥发送。 */
static bool app_uart_send_protocol_frame(uint8_t type,
                                         const uint8_t *payload,
                                         uint16_t payload_length)
{
    uint8_t frame[APP_PROTOCOL_MAX_FRAME_LENGTH];
    uint16_t frame_length;
    bool success = false;

    if (xSemaphoreTake(app_uart_tx_mutex,
                       pdMS_TO_TICKS(APP_UART_TX_MUTEX_TIMEOUT_MS)) != pdTRUE)
    {
        ESP_LOGW(TAG, "UART2 发送互斥锁超时: type=0x%02X", type);
        return false;
    }

    if (!app_protocol_encode(type,
                             app_uart_tx_sequence++,
                             payload,
                             payload_length,
                             frame,
                             sizeof(frame),
                             &frame_length))
    {
        ESP_LOGE(TAG, "协议帧编码失败: type=0x%02X", type);
    }
    else if (uart_write_bytes(APP_UART_PORT, frame, frame_length) != frame_length)
    {
        ESP_LOGE(TAG, "UART2 帧发送失败: type=0x%02X", type);
    }
    else
    {
        success = true;
    }

    xSemaphoreGive(app_uart_tx_mutex);
    return success;
}

/* 向 STM32 返回帧处理结果，ACK 不代表 MQTT 已发布成功。 */
static void app_uart_send_ack(uint16_t received_sequence,
                              uint8_t received_type,
                              app_protocol_ack_result_t result)
{
    uint8_t payload[4];

    payload[0] = (uint8_t)(received_sequence & 0xFFU);
    payload[1] = (uint8_t)(received_sequence >> 8U);
    payload[2] = received_type;
    payload[3] = (uint8_t)result;

    if (!app_uart_send_protocol_frame(APP_PROTOCOL_TYPE_ACK,
                                      payload,
                                      sizeof(payload)))
    {
        ESP_LOGE(TAG, "ACK 发送失败");
    }
}

static void app_uart_send_net_status(const app_net_status_snapshot_t *status)
{
    app_protocol_net_status_t protocol_status;
    uint8_t payload[APP_PROTOCOL_NET_STATUS_PAYLOAD_LENGTH];

    if (status == NULL)
    {
        return;
    }

    protocol_status.flags = status->flags;
    protocol_status.reason = status->reason;
    protocol_status.wifi_disconnect_count = status->wifi_disconnect_count;
    protocol_status.mqtt_disconnect_count = status->mqtt_disconnect_count;

    if (!app_protocol_pack_net_status(&protocol_status, payload) ||
        !app_uart_send_protocol_frame(APP_PROTOCOL_TYPE_NET_STATUS,
                                      payload,
                                      sizeof(payload)))
    {
        ESP_LOGW(TAG, "NET_STATUS 发送失败");
    }
}

static void app_on_net_status_changed(const app_net_status_snapshot_t *status)
{
    app_uart_send_net_status(status);
}

static void app_log_env_report(const app_protocol_env_report_t *report)
{
    ESP_LOGI(TAG,
             "sample=%" PRIu32 " uptime=%" PRIu32 "ms T=%" PRId32 ".%03" PRId32
             "C H=%" PRIu32 ".%03" PRIu32 "%% L=%" PRIu32 ".%03" PRIu32
             "lx valid=%c%c%c err=%" PRIu32 "/%" PRIu32 ",%" PRIu32 "/%" PRIu32,
             report->sample_sequence,
             report->uptime_ms,
             report->temperature_milli_c / 1000,
             (report->temperature_milli_c < 0 ? -report->temperature_milli_c : report->temperature_milli_c) % 1000,
             report->humidity_milli_rh / 1000U,
             report->humidity_milli_rh % 1000U,
             report->illuminance_milli_lux / 1000U,
             report->illuminance_milli_lux % 1000U,
             (report->valid_flags & APP_PROTOCOL_FLAG_CLIMATE_VALID) != 0U ? 'C' : '-',
             (report->valid_flags & APP_PROTOCOL_FLAG_ILLUMINANCE_VALID) != 0U ? 'L' : '-',
             (report->valid_flags & APP_PROTOCOL_FLAG_DISPLAY_READY) != 0U ? 'D' : '-',
             report->aht20_comm_errors,
             report->bh1750_comm_errors,
             report->aht20_data_errors,
             report->bh1750_data_errors);
}

static void app_log_mqtt_stats(const app_protocol_env_report_t *report)
{
    app_mqtt_stats_t stats;
    app_net_status_snapshot_t network_status;

    if ((report->sample_sequence % 10U) != 0U)
    {
        return;
    }

    app_mqtt_get_stats(&stats);
    app_net_status_get(&network_status);
    ESP_LOGI(TAG,
             "net=%c%c wd=%" PRIu32 " md=%" PRIu32
             " queue=%" PRIu32 " submitted=%" PRIu32
             " overflow=%" PRIu32 " accepted=%" PRIu32
             " puback=%" PRIu32 " puberr=%" PRIu32 " abandoned=%" PRIu32
             " duplicate=%" PRIu32,
             (network_status.flags & APP_PROTOCOL_NET_FLAG_WIFI_CONNECTED) != 0U ? 'W' : '-',
             (network_status.flags & APP_PROTOCOL_NET_FLAG_MQTT_CONNECTED) != 0U ? 'M' : '-',
             network_status.wifi_disconnect_count,
             network_status.mqtt_disconnect_count,
             stats.pending_count,
             stats.submitted_count,
             stats.queue_overflow_count,
             stats.publish_accepted_count,
             stats.puback_count,
             stats.publish_error_count,
             stats.abandoned_count,
             app_duplicate_env_report_count);
}

static void app_handle_frame(const app_protocol_frame_t *frame)
{
    if (frame->type == APP_PROTOCOL_TYPE_ENV_REPORT)
    {
        app_protocol_env_report_t report;
        const bool duplicate = app_has_last_env_report_sequence &&
                               frame->sequence == app_last_env_report_sequence;

        if (!app_protocol_unpack_env_report(frame, &report))
        {
            app_uart_send_ack(frame->sequence,
                              frame->type,
                              APP_PROTOCOL_ACK_RESULT_INVALID_PAYLOAD);
            ESP_LOGW(TAG, "ENV_REPORT 载荷长度错误: %u", frame->payload_length);
            return;
        }

        /* 完成语义解包后立即 ACK，避免调试日志拖慢协议确认。 */
        app_uart_send_ack(frame->sequence,
                          frame->type,
                          APP_PROTOCOL_ACK_RESULT_OK);

        if (duplicate)
        {
            app_duplicate_env_report_count++;
            ESP_LOGW(TAG, "重复 ENV_REPORT 已确认但不重复入队: sequence=%u",
                     frame->sequence);
            return;
        }

        app_has_last_env_report_sequence = true;
        app_last_env_report_sequence = frame->sequence;
        app_log_env_report(&report);
        (void)app_mqtt_submit_env_report(&report);
        app_log_mqtt_stats(&report);
        if ((report.sample_sequence % 10U) == 0U)
        {
            app_net_status_snapshot_t status;

            app_net_status_get(&status);
            app_uart_send_net_status(&status);
        }
        return;
    }

    app_uart_send_ack(frame->sequence,
                      frame->type,
                      APP_PROTOCOL_ACK_RESULT_UNSUPPORTED_TYPE);
    ESP_LOGW(TAG, "未处理消息: type=0x%02X sequence=%u length=%u",
             frame->type,
             frame->sequence,
             frame->payload_length);
}

void app_main(void)
{
    uint8_t rx_buffer[128];
    app_protocol_parser_t parser;
    app_protocol_frame_t frame;

    app_uart_init();
    app_protocol_parser_init(&parser);
    app_net_status_init(app_on_net_status_changed);
    app_mqtt_init();
    app_wifi_start(app_mqtt_start);
    ESP_LOGI(TAG, "UART2 已启动: RX=GPIO%d TX=GPIO%d %d-8-N-1",
             APP_UART_RX_PIN,
             APP_UART_TX_PIN,
             APP_UART_BAUD_RATE);

    while (true)
    {
        const int received = uart_read_bytes(APP_UART_PORT,
                                             rx_buffer,
                                             sizeof(rx_buffer),
                                             pdMS_TO_TICKS(APP_UART_READ_TIMEOUT_MS));
        for (int index = 0; index < received; index++)
        {
            const app_protocol_parse_result_t result =
                app_protocol_parser_input(&parser, rx_buffer[index], &frame);

            if (result == APP_PROTOCOL_PARSE_FRAME)
            {
                app_handle_frame(&frame);
            }
            else if (result != APP_PROTOCOL_PARSE_NONE)
            {
                ESP_LOGW(TAG, "协议解析错误: %d", result);
            }
        }
    }
}
