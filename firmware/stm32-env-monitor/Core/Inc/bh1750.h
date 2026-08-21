#ifndef BH1750_H
#define BH1750_H

#include "stm32f1xx_hal.h"

HAL_StatusTypeDef bh1750_init(I2C_HandleTypeDef *hi2c);
HAL_StatusTypeDef bh1750_read(I2C_HandleTypeDef *hi2c,
                               uint32_t *illuminance_milli_lux);

#endif
