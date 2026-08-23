#ifndef APP_LINK_H
#define APP_LINK_H

#include "app_protocol.h"

#define APP_LINK_ACK_ERROR_LENGTH      0x01U
#define APP_LINK_ACK_ERROR_TYPE        0x02U
#define APP_LINK_ACK_ERROR_NOT_WAITING 0x04U
#define APP_LINK_ACK_ERROR_SEQUENCE    0x08U
#define APP_LINK_ACK_ERROR_RESULT      0x10U

/* USART1 双向链路状态；接收相关字段在串口中断中更新。 */
typedef struct
{
  UART_HandleTypeDef *huart;
  app_protocol_parser_t parser;
  app_protocol_frame_t frame;
  uint8_t rx_byte;
  volatile HAL_StatusTypeDef rx_status;
  volatile uint32_t received_frame_count;
  volatile uint32_t rx_error_count;
  volatile uint32_t ack_count;
  volatile uint32_t ack_error_count;
  volatile uint16_t expected_ack_sequence;
  volatile uint16_t last_ack_sequence;
  volatile uint16_t last_received_ack_sequence;
  volatile uint8_t last_ack_result;
  volatile uint8_t last_received_ack_type;
  volatile uint8_t last_received_ack_result;
  volatile uint8_t last_ack_error_flags;
  volatile uint8_t awaiting_ack;
} app_link_t;

void app_link_init(app_link_t *link, UART_HandleTypeDef *huart);
void app_link_arm_env_report_ack(app_link_t *link, uint16_t sequence);
void app_link_cancel_env_report_ack(app_link_t *link);
void app_link_on_uart_rx_complete(app_link_t *link, UART_HandleTypeDef *huart);
void app_link_on_uart_error(app_link_t *link, UART_HandleTypeDef *huart);

#endif /* APP_LINK_H */
