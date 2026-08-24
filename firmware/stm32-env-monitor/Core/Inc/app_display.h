#ifndef APP_DISPLAY_H
#define APP_DISPLAY_H

#include "app_monitor.h"
#include "ssd1306.h"

typedef struct
{
    ssd1306_t oled;
    HAL_StatusTypeDef status;
    uint32_t error_count;
    uint32_t last_retry_tick;
    uint8_t initialized;
} app_display_t;

void app_display_init(app_display_t *display);
void app_display_update(app_display_t *display,
                        I2C_HandleTypeDef *hi2c,
                        const app_monitor_t *monitor,
                        uint8_t network_flags,
                        uint8_t data_changed);

#endif
