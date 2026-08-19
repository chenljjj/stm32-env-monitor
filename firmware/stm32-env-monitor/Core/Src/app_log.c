#include "app_log.h"

#include <string.h>

HAL_StatusTypeDef app_log_write(UART_HandleTypeDef *huart,
                                const char *message)
{
    if ((huart == NULL) || (message == NULL))
    {
        return HAL_ERROR;
    }

    return HAL_UART_Transmit(huart,
                             (uint8_t *)message,
                             (uint16_t)strlen(message),
                             100U);
}