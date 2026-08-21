#ifndef APP_STATUS_H
#define APP_STATUS_H

#include "stm32f1xx_hal.h"

typedef struct
{
    /* 保存启动阶段的日志发送结果。 */
    HAL_StatusTypeDef startup_status;
    /* 用于非阻塞 LED 心跳计时。 */
    uint32_t last_heartbeat_tick;
} app_status_t;

void app_status_init(app_status_t *status, HAL_StatusTypeDef startup_status);
void app_status_update(app_status_t *status,
                       GPIO_TypeDef *led_port,
                       uint16_t led_pin);

#endif
