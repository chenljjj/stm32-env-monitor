#ifndef APP_LOG_H
#define APP_LOG_H

#include "stm32f1xx_hal.h"

HAL_StatusTypeDef app_log_write(UART_HandleTypeDef *huart,
                                const char *message);

#endif