#include "app_link.h"

#include <string.h>

#define APP_PROTOCOL_ACK_PAYLOAD_LENGTH 4U

static void app_link_start_receive(app_link_t *link)
{
  if ((link == NULL) || (link->huart == NULL))
  {
    return;
  }

  link->rx_status = HAL_UART_Receive_IT(link->huart, &link->rx_byte, 1U);
  if (link->rx_status != HAL_OK)
  {
    ++link->rx_error_count;
  }
}

static uint16_t app_link_get_u16_le(const uint8_t *buffer)
{
  return (uint16_t)buffer[0] | ((uint16_t)buffer[1] << 8U);
}

static void app_link_handle_ack(app_link_t *link)
{
  const uint8_t *payload = link->frame.payload;
  const uint16_t acknowledged_sequence = app_link_get_u16_le(payload);

  /* ACK 仅确认已发送的 ENV_REPORT，result=0 代表 ESP32 成功处理。 */
  if ((link->frame.payload_length != APP_PROTOCOL_ACK_PAYLOAD_LENGTH) ||
      (payload[2] != APP_PROTOCOL_TYPE_ENV_REPORT) ||
      (link->awaiting_ack == 0U) ||
      (acknowledged_sequence != link->expected_ack_sequence))
  {
    ++link->ack_error_count;
    return;
  }

  link->last_ack_sequence = acknowledged_sequence;
  link->last_ack_result = payload[3];
  link->awaiting_ack = 0U;
  if (payload[3] == 0U)
  {
    ++link->ack_count;
  }
  else
  {
    ++link->ack_error_count;
  }
}

void app_link_init(app_link_t *link, UART_HandleTypeDef *huart)
{
  if ((link == NULL) || (huart == NULL))
  {
    return;
  }

  (void)memset(link, 0, sizeof(*link));
  link->huart = huart;
  link->rx_status = HAL_BUSY;
  app_protocol_parser_init(&link->parser);
  app_link_start_receive(link);
}

void app_link_note_env_report_sent(app_link_t *link, uint16_t sequence)
{
  if (link != NULL)
  {
    link->expected_ack_sequence = sequence;
    link->awaiting_ack = 1U;
  }
}

void app_link_on_uart_rx_complete(app_link_t *link, UART_HandleTypeDef *huart)
{
  app_protocol_parse_result_t result;

  if ((link == NULL) || (huart != link->huart))
  {
    return;
  }

  result = app_protocol_parser_input(&link->parser, link->rx_byte, &link->frame);

  if (result == APP_PROTOCOL_PARSE_FRAME)
  {
    ++link->received_frame_count;
    if (link->frame.type == APP_PROTOCOL_TYPE_ACK)
    {
      app_link_handle_ack(link);
    }
  }
  else if (result != APP_PROTOCOL_PARSE_NONE)
  {
    ++link->rx_error_count;
  }

  app_link_start_receive(link);
}

void app_link_on_uart_error(app_link_t *link, UART_HandleTypeDef *huart)
{
  if ((link == NULL) || (huart != link->huart))
  {
    return;
  }

  ++link->rx_error_count;
  app_protocol_parser_reset(&link->parser);
  app_link_start_receive(link);
}
