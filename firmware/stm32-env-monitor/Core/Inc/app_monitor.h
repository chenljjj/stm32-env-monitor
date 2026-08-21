#ifndef APP_MONITOR_H
#define APP_MONITOR_H

#include "aht20.h"
#include "stm32f1xx_hal.h"

typedef struct
{
    aht20_measurement_t climate;
    uint32_t illuminance_milli_lux;
    HAL_StatusTypeDef aht20_status;
    HAL_StatusTypeDef bh1750_status;
    uint32_t aht20_error_count;
    uint32_t bh1750_error_count;
    uint32_t sample_sequence;
    uint32_t last_sample_tick;
    uint8_t aht20_initialized;
    uint8_t bh1750_initialized;
    uint8_t climate_valid;
    uint8_t illuminance_valid;
} app_monitor_t;

void app_monitor_init(app_monitor_t *monitor);
uint8_t app_monitor_update(app_monitor_t *monitor, I2C_HandleTypeDef *hi2c);

#endif
