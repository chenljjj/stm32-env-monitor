#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "app_net_status.h"
#include "app_secrets.h"
#include "app_wifi.h"

static const char *TAG = "env_wifi";
static bool s_connected;
static app_wifi_connected_handler_t s_on_connected;
static esp_event_handler_instance_t s_wifi_event_instance;
static esp_event_handler_instance_t s_ip_event_instance;

static void app_wifi_connect(void)
{
    const esp_err_t result = esp_wifi_connect();

    if (result != ESP_OK)
    {
        ESP_LOGW(TAG, "Wi-Fi 连接请求失败: %s", esp_err_to_name(result));
    }
}

static void app_wifi_event_handler(void *argument,
                                   esp_event_base_t event_base,
                                   int32_t event_id,
                                   void *event_data)
{
    (void)argument;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        ESP_LOGI(TAG, "开始连接 Wi-Fi: %s", APP_WIFI_SSID);
        app_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        const wifi_event_sta_disconnected_t *event = event_data;

        s_connected = false;
        app_net_status_set_wifi_connected(false);
        ESP_LOGW(TAG,
                 "Wi-Fi 已断开，原因码=%d，准备重连",
                 event == NULL ? -1 : event->reason);
        app_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        const ip_event_got_ip_t *event = event_data;
        const bool newly_connected = !s_connected;

        s_connected = true;
        app_net_status_set_wifi_connected(true);
        ESP_LOGI(TAG, "Wi-Fi 已连接，IPv4=" IPSTR, IP2STR(&event->ip_info.ip));

        if (newly_connected && s_on_connected != NULL)
        {
            s_on_connected();
        }
    }
}

void app_wifi_start(app_wifi_connected_handler_t on_connected)
{
    esp_err_t result;
    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    wifi_config_t wifi_config = {0};

    s_on_connected = on_connected;
    result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        result = nvs_flash_init();
    }
    ESP_ERROR_CHECK(result);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                         ESP_EVENT_ANY_ID,
                                                         &app_wifi_event_handler,
                                                         NULL,
                                                         &s_wifi_event_instance));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                         IP_EVENT_STA_GOT_IP,
                                                         &app_wifi_event_handler,
                                                         NULL,
                                                         &s_ip_event_instance));

    strlcpy((char *)wifi_config.sta.ssid, APP_WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, APP_WIFI_PASSWORD, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

bool app_wifi_is_connected(void)
{
    return s_connected;
}
