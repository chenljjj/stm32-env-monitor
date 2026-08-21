#include "app_display.h"

#include <stdio.h>
#include <string.h>

#define APP_DISPLAY_RETRY_PERIOD_MS 3000U

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

static void app_display_render(app_display_t *display, const app_monitor_t *monitor)
{
    char temperature[16];
    char humidity[16];
    char illuminance[16];

    app_display_format_temperature(monitor, temperature, sizeof(temperature));
    app_display_format_humidity(monitor, humidity, sizeof(humidity));
    app_display_format_illuminance(monitor, illuminance, sizeof(illuminance));

    ssd1306_clear(&display->oled);
    ssd1306_draw_string(&display->oled, 0U, 1U, "ENV MONITOR", 1U);
    ssd1306_draw_string(&display->oled, 0U, 14U, temperature, 2U);
    ssd1306_draw_string(&display->oled, 0U, 30U, humidity, 2U);
    ssd1306_draw_string(&display->oled, 0U, 46U, illuminance, 2U);
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
        app_display_render(display, monitor);
        display->status = ssd1306_update(&display->oled, hi2c);
        if (display->status != HAL_OK)
        {
            ++display->error_count;
            display->initialized = 0U;
        }
    }
}
