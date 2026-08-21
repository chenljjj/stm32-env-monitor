#ifndef APP_I2C_H
#define APP_I2C_H

#include "stm32f1xx_hal.h"

HAL_StatusTypeDef app_i2c_is_device_ready(I2C_HandleTypeDef *hi2c,
                                           uint8_t address_7bit);
HAL_StatusTypeDef app_i2c_transmit(I2C_HandleTypeDef *hi2c,
                                    uint8_t address_7bit,
                                    const uint8_t *data,
                                    uint16_t length);
HAL_StatusTypeDef app_i2c_receive(I2C_HandleTypeDef *hi2c,
                                   uint8_t address_7bit,
                                   uint8_t *data,
                                   uint16_t length);
HAL_StatusTypeDef app_i2c_read_register(I2C_HandleTypeDef *hi2c,
                                         uint8_t address_7bit,
                                         uint8_t register_address,
                                         uint8_t *data,
                                         uint16_t length);
HAL_StatusTypeDef app_i2c_write_register(I2C_HandleTypeDef *hi2c,
                                          uint8_t address_7bit,
                                          uint8_t register_address,
                                          const uint8_t *data,
                                          uint16_t length);

#endif
