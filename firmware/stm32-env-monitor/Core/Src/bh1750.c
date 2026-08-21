#include "bh1750.h"

#include "app_i2c.h"

#define BH1750_ADDRESS_7BIT             0x23U
#define BH1750_COMMAND_POWER_ON          0x01U
#define BH1750_COMMAND_RESET             0x07U
#define BH1750_COMMAND_CONTINUOUS_H_RES  0x10U
#define BH1750_FIRST_MEASURE_DELAY_MS    180U

static HAL_StatusTypeDef bh1750_send_command(I2C_HandleTypeDef *hi2c,
                                              uint8_t command)
{
    return app_i2c_transmit(hi2c, BH1750_ADDRESS_7BIT, &command, 1U);
}

HAL_StatusTypeDef bh1750_init(I2C_HandleTypeDef *hi2c)
{
    HAL_StatusTypeDef result;

    if (hi2c == NULL)
    {
        return HAL_ERROR;
    }

    result = app_i2c_is_device_ready(hi2c, BH1750_ADDRESS_7BIT);
    if (result != HAL_OK)
    {
        return result;
    }

    result = bh1750_send_command(hi2c, BH1750_COMMAND_POWER_ON);
    if (result != HAL_OK)
    {
        return result;
    }

    result = bh1750_send_command(hi2c, BH1750_COMMAND_RESET);
    if (result != HAL_OK)
    {
        return result;
    }

    result = bh1750_send_command(hi2c, BH1750_COMMAND_CONTINUOUS_H_RES);
    if (result == HAL_OK)
    {
        /* 首次高分辨率测量需要等待转换完成。 */
        HAL_Delay(BH1750_FIRST_MEASURE_DELAY_MS);
    }
    return result;
}

HAL_StatusTypeDef bh1750_read(I2C_HandleTypeDef *hi2c,
                               uint32_t *illuminance_milli_lux)
{
    uint8_t data[2];
    uint16_t raw_value;
    HAL_StatusTypeDef result;

    if ((hi2c == NULL) || (illuminance_milli_lux == NULL))
    {
        return HAL_ERROR;
    }

    result = app_i2c_receive(hi2c, BH1750_ADDRESS_7BIT, data, sizeof(data));
    if (result != HAL_OK)
    {
        return result;
    }

    raw_value = ((uint16_t)data[0] << 8U) | (uint16_t)data[1];
    /* BH1750 默认灵敏度下，lux = raw / 1.2。 */
    *illuminance_milli_lux = ((uint32_t)raw_value * 10000U) / 12U;
    return HAL_OK;
}
