#include "app_monitor.h"

#include <string.h>

#include "aht20.h"
#include "bh1750.h"

#define APP_MONITOR_SAMPLE_PERIOD_MS 1000U

static void app_monitor_sample_aht20(app_monitor_t *monitor,
                                     I2C_HandleTypeDef *hi2c)
{
    if (monitor->aht20_initialized == 0U)
    {
        monitor->aht20_status = aht20_init(hi2c);
        monitor->aht20_initialized = (monitor->aht20_status == HAL_OK) ? 1U : 0U;
    }

    if (monitor->aht20_initialized != 0U)
    {
        monitor->aht20_status = aht20_read(hi2c, &monitor->climate);
        monitor->climate_valid = (monitor->aht20_status == HAL_OK) ? 1U : 0U;
        if (monitor->aht20_status != HAL_OK)
        {
            /* 下个采样周期重新初始化，处理设备复位或总线异常。 */
            monitor->aht20_initialized = 0U;
        }
    }
    else
    {
        monitor->climate_valid = 0U;
    }

    if (monitor->aht20_status != HAL_OK)
    {
        ++monitor->aht20_error_count;
    }
}

static void app_monitor_sample_bh1750(app_monitor_t *monitor,
                                      I2C_HandleTypeDef *hi2c)
{
    if (monitor->bh1750_initialized == 0U)
    {
        monitor->bh1750_status = bh1750_init(hi2c);
        monitor->bh1750_initialized = (monitor->bh1750_status == HAL_OK) ? 1U : 0U;
    }

    if (monitor->bh1750_initialized != 0U)
    {
        monitor->bh1750_status = bh1750_read(hi2c, &monitor->illuminance_milli_lux);
        monitor->illuminance_valid = (monitor->bh1750_status == HAL_OK) ? 1U : 0U;
        if (monitor->bh1750_status != HAL_OK)
        {
            /* 下个采样周期重新初始化，处理设备复位或总线异常。 */
            monitor->bh1750_initialized = 0U;
        }
    }
    else
    {
        monitor->illuminance_valid = 0U;
    }

    if (monitor->bh1750_status != HAL_OK)
    {
        ++monitor->bh1750_error_count;
    }
}

void app_monitor_init(app_monitor_t *monitor)
{
    if (monitor != NULL)
    {
        memset(monitor, 0, sizeof(*monitor));
        monitor->aht20_status = HAL_BUSY;
        monitor->bh1750_status = HAL_BUSY;
        /* 让主循环尽快完成第一次设备探测。 */
        monitor->last_sample_tick = HAL_GetTick() - APP_MONITOR_SAMPLE_PERIOD_MS;
    }
}

uint8_t app_monitor_update(app_monitor_t *monitor, I2C_HandleTypeDef *hi2c)
{
    uint32_t current_tick;

    if ((monitor == NULL) || (hi2c == NULL))
    {
        return 0U;
    }

    current_tick = HAL_GetTick();
    if ((current_tick - monitor->last_sample_tick) < APP_MONITOR_SAMPLE_PERIOD_MS)
    {
        return 0U;
    }

    monitor->last_sample_tick = current_tick;
    app_monitor_sample_aht20(monitor, hi2c);
    app_monitor_sample_bh1750(monitor, hi2c);
    ++monitor->sample_sequence;
    return 1U;
}
