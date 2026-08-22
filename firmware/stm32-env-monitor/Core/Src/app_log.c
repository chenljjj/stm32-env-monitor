#include "app_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define APP_LOG_BUFFER_SIZE 192U

HAL_StatusTypeDef app_log_write(UART_HandleTypeDef *huart,
                                const char *message)
{
    if ((huart == NULL) || (message == NULL))
    {
        /* 防止无效参数传入 HAL 驱动。 */
        return HAL_ERROR;
    }

    /* 启动日志允许最多阻塞 100 ms。 */
    return HAL_UART_Transmit(huart,
                             (uint8_t *)message,
                             (uint16_t)strlen(message),
                             100U);
}

HAL_StatusTypeDef app_log_printf(UART_HandleTypeDef *huart,
                                 const char *format,
                                 ...)
{
    char buffer[APP_LOG_BUFFER_SIZE];
    va_list arguments;
    int length;

    if ((huart == NULL) || (format == NULL))
    {
        return HAL_ERROR;
    }

    va_start(arguments, format);
    length = vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);

    /* 不发送被截断的日志，避免在串口上出现残缺记录。 */
    if ((length < 0) || ((uint32_t)length >= sizeof(buffer)))
    {
        return HAL_ERROR;
    }

    return HAL_UART_Transmit(huart,
                             (uint8_t *)buffer,
                             (uint16_t)length,
                             100U);
}
