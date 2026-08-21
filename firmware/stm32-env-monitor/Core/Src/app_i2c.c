#include "app_i2c.h"

/* I2C 设备地址以 7 位形式传入，HAL 调用时需左移一位。 */
#define APP_I2C_HAL_ADDRESS(address_7bit) ((uint16_t)(address_7bit) << 1U)
#define APP_I2C_TIMEOUT_MS                100U
#define APP_I2C_READY_TRIALS               3U

HAL_StatusTypeDef app_i2c_is_device_ready(I2C_HandleTypeDef *hi2c,
                                           uint8_t address_7bit)
{
    if (hi2c == NULL)
    {
        return HAL_ERROR;
    }

    return HAL_I2C_IsDeviceReady(hi2c,
                                 APP_I2C_HAL_ADDRESS(address_7bit),
                                 APP_I2C_READY_TRIALS,
                                 APP_I2C_TIMEOUT_MS);
}

HAL_StatusTypeDef app_i2c_transmit(I2C_HandleTypeDef *hi2c,
                                    uint8_t address_7bit,
                                    const uint8_t *data,
                                    uint16_t length)
{
    if ((hi2c == NULL) || (data == NULL) || (length == 0U))
    {
        return HAL_ERROR;
    }

    return HAL_I2C_Master_Transmit(hi2c,
                                   APP_I2C_HAL_ADDRESS(address_7bit),
                                   (uint8_t *)data,
                                   length,
                                   APP_I2C_TIMEOUT_MS);
}

HAL_StatusTypeDef app_i2c_receive(I2C_HandleTypeDef *hi2c,
                                   uint8_t address_7bit,
                                   uint8_t *data,
                                   uint16_t length)
{
    if ((hi2c == NULL) || (data == NULL) || (length == 0U))
    {
        return HAL_ERROR;
    }

    return HAL_I2C_Master_Receive(hi2c,
                                  APP_I2C_HAL_ADDRESS(address_7bit),
                                  data,
                                  length,
                                  APP_I2C_TIMEOUT_MS);
}

HAL_StatusTypeDef app_i2c_read_register(I2C_HandleTypeDef *hi2c,
                                         uint8_t address_7bit,
                                         uint8_t register_address,
                                         uint8_t *data,
                                         uint16_t length)
{
    if ((hi2c == NULL) || (data == NULL) || (length == 0U))
    {
        return HAL_ERROR;
    }

    return HAL_I2C_Mem_Read(hi2c,
                            APP_I2C_HAL_ADDRESS(address_7bit),
                            register_address,
                            I2C_MEMADD_SIZE_8BIT,
                            data,
                            length,
                            APP_I2C_TIMEOUT_MS);
}

HAL_StatusTypeDef app_i2c_write_register(I2C_HandleTypeDef *hi2c,
                                          uint8_t address_7bit,
                                          uint8_t register_address,
                                          const uint8_t *data,
                                          uint16_t length)
{
    if ((hi2c == NULL) || (data == NULL) || (length == 0U))
    {
        return HAL_ERROR;
    }

    return HAL_I2C_Mem_Write(hi2c,
                             APP_I2C_HAL_ADDRESS(address_7bit),
                             register_address,
                             I2C_MEMADD_SIZE_8BIT,
                             (uint8_t *)data,
                             length,
                             APP_I2C_TIMEOUT_MS);
}
