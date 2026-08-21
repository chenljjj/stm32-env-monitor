#include "app_status.h"

/* 正常状态下每 500 ms 翻转一次，形成约 1 秒的闪烁周期。 */
#define APP_STATUS_HEARTBEAT_PERIOD_MS 500U

void app_status_init(app_status_t *status, HAL_StatusTypeDef startup_status)
{
    if (status != NULL)
    {
        status->startup_status = startup_status;
        status->last_heartbeat_tick = HAL_GetTick();
    }
}

void app_status_update(app_status_t *status,
                       GPIO_TypeDef *led_port,
                       uint16_t led_pin)
{
    uint32_t current_tick;

    if ((status == NULL) || (led_port == NULL))
    {
        return;
    }

    if (status->startup_status != HAL_OK)
    {
        /* Blue Pill 的 PC13 LED 为低电平点亮，用常亮表示启动故障。 */
        HAL_GPIO_WritePin(led_port, led_pin, GPIO_PIN_RESET);
        return;
    }

    /* 使用系统 tick 定时，避免心跳灯阻塞主循环。 */
    current_tick = HAL_GetTick();
    if ((current_tick - status->last_heartbeat_tick) >= APP_STATUS_HEARTBEAT_PERIOD_MS)
    {
        status->last_heartbeat_tick = current_tick;
        HAL_GPIO_TogglePin(led_port, led_pin);
    }
}
