#include "app_display.h"

#include <stdio.h>
#include <string.h>

#define APP_DISPLAY_RETRY_PERIOD_MS 3000U
#define APP_DISPLAY_NET_FLAG_WIFI_CONNECTED 0x01U
#define APP_DISPLAY_NET_FLAG_MQTT_CONNECTED 0x02U
#define APP_DISPLAY_WIDTH 128U
#define APP_DISPLAY_GLYPH_STEP 4U

static void app_display_format_temperature(const app_monitor_t *monitor,
                                           char *text,
                                           size_t size)
{
    int32_t fraction;

    if (monitor->climate_valid == 0U)
    {
        (void)snprintf(text, size, "T:---.-C");
        return;
    }

    fraction = (monitor->climate.temperature_milli_c % 1000) / 100;
    if (fraction < 0)
    {
        fraction = -fraction;
    }
    (void)snprintf(text, size, "T:%ld.%01ldC",
                   (long)(monitor->climate.temperature_milli_c / 1000),
                   (long)fraction);
}

static void app_display_format_humidity(const app_monitor_t *monitor,
                                        char *text,
                                        size_t size)
{
    if (monitor->climate_valid == 0U)
    {
        (void)snprintf(text, size, "H:---.-%%");
        return;
    }

    (void)snprintf(text, size, "H:%lu.%01lu%%",
                   (unsigned long)(monitor->climate.humidity_milli_rh / 1000U),
                   (unsigned long)((monitor->climate.humidity_milli_rh % 1000U) / 100U));
}

static void app_display_format_illuminance(const app_monitor_t *monitor,
                                           char *text,
                                           size_t size)
{
    if (monitor->illuminance_valid == 0U)
    {
        (void)snprintf(text, size, "L:---.-");
        return;
    }

    (void)snprintf(text, size, "L:%lu.%01lu",
                   (unsigned long)(monitor->illuminance_milli_lux / 1000U),
                   (unsigned long)((monitor->illuminance_milli_lux % 1000U) / 100U));
}

static void app_display_format_network(uint8_t network_flags, char *text, size_t size)
{
    const char *wifi = (network_flags & APP_DISPLAY_NET_FLAG_WIFI_CONNECTED) != 0U ? "ON" : "--";
    const char *mqtt = (network_flags & APP_DISPLAY_NET_FLAG_MQTT_CONNECTED) != 0U ? "ON" : "--";

    (void)snprintf(text, size, "WIFI:%s MQTT:%s", wifi, mqtt);
}

/* SSD1306 使用等宽 3x5 点阵字，按当前缩放比例计算文字左边界。 */
static void app_display_draw_centered(ssd1306_t *oled,
                                      uint8_t y,
                                      const char *text,
                                      uint8_t scale)
{
    size_t width;
    uint8_t x = 0U;

    if ((oled == NULL) || (text == NULL) || (scale == 0U))
    {
        return;
    }

    width = strlen(text) * APP_DISPLAY_GLYPH_STEP * scale;
    if (width < APP_DISPLAY_WIDTH)
    {
        x = (uint8_t)((APP_DISPLAY_WIDTH - width) / 2U);
    }

    ssd1306_draw_string(oled, x, y, text, scale);
}

static void app_display_render(app_display_t *display,
                               const app_monitor_t *monitor,
                               uint8_t network_flags)
{
    char temperature[16];
    char humidity[16];
    char illuminance[16];
    char network[20];

    app_display_format_temperature(monitor, temperature, sizeof(temperature));
    app_display_format_humidity(monitor, humidity, sizeof(humidity));
    app_display_format_illuminance(monitor, illuminance, sizeof(illuminance));
    app_display_format_network(network_flags, network, sizeof(network));

    ssd1306_clear(&display->oled);
    app_display_draw_centered(&display->oled, 1U, "ENV MONITOR", 1U);
    /* 三项环境数据统一使用 2 倍字体，优先保证抬头可读性。 */
    app_display_draw_centered(&display->oled, 11U, temperature, 2U);
    app_display_draw_centered(&display->oled, 27U, humidity, 2U);
    app_display_draw_centered(&display->oled, 43U, illuminance, 2U);
    /* 网络状态以紧凑小字置于底部，Wi-Fi 与 MQTT 可同时判断。 */
    app_display_draw_centered(&display->oled, 57U, network, 1U);
}

void app_display_init(app_display_t *display)
{
    if (display != NULL)
    {
        memset(display, 0, sizeof(*display));
        ssd1306_init_buffer(&display->oled, SSD1306_DEFAULT_ADDRESS_7BIT);
        display->status = HAL_BUSY;
        /* 首次更新立即探测 OLED。 */
        display->last_retry_tick = HAL_GetTick() - APP_DISPLAY_RETRY_PERIOD_MS;
    }
}

void app_display_update(app_display_t *display,
                        I2C_HandleTypeDef *hi2c,
                        const app_monitor_t *monitor,
                        uint8_t network_flags,
                        uint8_t data_changed)
{
    uint32_t current_tick;

    if ((display == NULL) || (hi2c == NULL) || (monitor == NULL))
    {
        return;
    }

    current_tick = HAL_GetTick();
    if (display->initialized == 0U)
    {
        if ((current_tick - display->last_retry_tick) < APP_DISPLAY_RETRY_PERIOD_MS)
        {
            return;
        }

        display->last_retry_tick = current_tick;
        display->status = ssd1306_init(&display->oled, hi2c);
        if (display->status != HAL_OK)
        {
            ++display->error_count;
            return;
        }
        display->initialized = 1U;
        data_changed = 1U;
    }

    if (data_changed != 0U)
    {
        app_display_render(display, monitor, network_flags);
        display->status = ssd1306_update(&display->oled, hi2c);
        if (display->status != HAL_OK)
        {
            ++display->error_count;
            display->initialized = 0U;
        }
    }
}
