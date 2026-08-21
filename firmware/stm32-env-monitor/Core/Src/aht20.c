#include "aht20.h"

#include "app_i2c.h"

#define AHT20_ADDRESS_7BIT            0x38U
#define AHT20_STATUS_BUSY_MASK         0x80U
#define AHT20_STATUS_CALIBRATED_MASK   0x08U
#define AHT20_INIT_COMMAND             0xBEU
#define AHT20_MEASURE_COMMAND          0xACU
#define AHT20_MEASURE_DELAY_MS         80U
#define AHT20_READY_POLL_DELAY_MS      10U
#define AHT20_READY_POLL_COUNT         10U
#define AHT20_RAW_MAX                  1048576ULL

static HAL_StatusTypeDef aht20_read_status(I2C_HandleTypeDef *hi2c,
                                            uint8_t *status)
{
    return app_i2c_receive(hi2c, AHT20_ADDRESS_7BIT, status, 1U);
}

static HAL_StatusTypeDef aht20_wait_ready(I2C_HandleTypeDef *hi2c)
{
    uint8_t status;
    uint8_t attempt;
    HAL_StatusTypeDef result;

    for (attempt = 0U; attempt < AHT20_READY_POLL_COUNT; ++attempt)
    {
        result = aht20_read_status(hi2c, &status);
        if (result != HAL_OK)
        {
            return result;
        }
        if ((status & AHT20_STATUS_BUSY_MASK) == 0U)
        {
            return HAL_OK;
        }
        HAL_Delay(AHT20_READY_POLL_DELAY_MS);
    }

    return HAL_TIMEOUT;
}

HAL_StatusTypeDef aht20_init(I2C_HandleTypeDef *hi2c)
{
    static const uint8_t init_command[] = { AHT20_INIT_COMMAND, 0x08U, 0x00U };
    uint8_t status;
    HAL_StatusTypeDef result;

    if (hi2c == NULL)
    {
        return HAL_ERROR;
    }

    result = app_i2c_is_device_ready(hi2c, AHT20_ADDRESS_7BIT);
    if (result != HAL_OK)
    {
        return result;
    }

    result = app_i2c_transmit(hi2c,
                               AHT20_ADDRESS_7BIT,
                               init_command,
                               sizeof(init_command));
    if (result != HAL_OK)
    {
        return result;
    }

    HAL_Delay(AHT20_READY_POLL_DELAY_MS);
    result = aht20_read_status(hi2c, &status);
    if (result != HAL_OK)
    {
        return result;
    }

    return ((status & AHT20_STATUS_CALIBRATED_MASK) != 0U) ? HAL_OK : HAL_ERROR;
}

HAL_StatusTypeDef aht20_read(I2C_HandleTypeDef *hi2c,
                              aht20_measurement_t *measurement)
{
    static const uint8_t measure_command[] = { AHT20_MEASURE_COMMAND, 0x33U, 0x00U };
    uint8_t data[6];
    uint32_t humidity_raw;
    uint32_t temperature_raw;
    HAL_StatusTypeDef result;

    if ((hi2c == NULL) || (measurement == NULL))
    {
        return HAL_ERROR;
    }

    result = app_i2c_transmit(hi2c,
                               AHT20_ADDRESS_7BIT,
                               measure_command,
                               sizeof(measure_command));
    if (result != HAL_OK)
    {
        return result;
    }

    HAL_Delay(AHT20_MEASURE_DELAY_MS);
    result = aht20_wait_ready(hi2c);
    if (result != HAL_OK)
    {
        return result;
    }

    result = app_i2c_receive(hi2c, AHT20_ADDRESS_7BIT, data, sizeof(data));
    if (result != HAL_OK)
    {
        return result;
    }

    humidity_raw = ((uint32_t)data[1] << 12U) |
                   ((uint32_t)data[2] << 4U) |
                   ((uint32_t)data[3] >> 4U);
    temperature_raw = (((uint32_t)data[3] & 0x0FU) << 16U) |
                      ((uint32_t)data[4] << 8U) |
                      (uint32_t)data[5];

    /* 使用定点单位，避免采样路径中的浮点运算。 */
    measurement->humidity_milli_rh = (uint32_t)((uint64_t)humidity_raw * 100000ULL /
                                                AHT20_RAW_MAX);
    measurement->temperature_milli_c = (int32_t)((uint64_t)temperature_raw * 200000ULL /
                                                 AHT20_RAW_MAX) - 50000;
    return HAL_OK;
}
