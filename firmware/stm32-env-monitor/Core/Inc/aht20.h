#ifndef AHT20_H
#define AHT20_H

#include "stm32f1xx_hal.h"

typedef struct
{
    int32_t temperature_milli_c;
    uint32_t humidity_milli_rh;
} aht20_measurement_t;

HAL_StatusTypeDef aht20_init(I2C_HandleTypeDef *hi2c);
HAL_StatusTypeDef aht20_read(I2C_HandleTypeDef *hi2c,
                              aht20_measurement_t *measurement);

#endif
