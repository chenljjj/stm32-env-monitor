#include "app_protocol.h"

#include <string.h>

static void app_protocol_put_u16_le(uint8_t *buffer, uint16_t value)
{
  buffer[0] = (uint8_t)(value & 0xFFU);
  buffer[1] = (uint8_t)(value >> 8U);
}

static void app_protocol_put_u32_le(uint8_t *buffer, uint32_t value)
{
  buffer[0] = (uint8_t)(value & 0xFFU);
  buffer[1] = (uint8_t)((value >> 8U) & 0xFFU);
  buffer[2] = (uint8_t)((value >> 16U) & 0xFFU);
  buffer[3] = (uint8_t)(value >> 24U);
}

static uint16_t app_protocol_get_u16_le(const uint8_t *buffer)
{
  return (uint16_t)buffer[0] | ((uint16_t)buffer[1] << 8U);
}

uint16_t app_protocol_crc16_ccitt(const uint8_t *data, uint16_t length)
{
  uint16_t crc = 0xFFFFU;
  uint16_t index;
  uint8_t bit;

  if ((data == NULL) && (length != 0U))
  {
    return 0U;
  }

  for (index = 0U; index < length; index++)
  {
    crc ^= (uint16_t)data[index] << 8U;

    for (bit = 0U; bit < 8U; bit++)
    {
      if ((crc & 0x8000U) != 0U)
      {
        crc = (uint16_t)((crc << 1U) ^ 0x1021U);
      }
      else
      {
        crc <<= 1U;
      }
    }
  }

  return crc;
}

HAL_StatusTypeDef app_protocol_encode(uint8_t type,
                                      uint16_t sequence,
                                      const uint8_t *payload,
                                      uint16_t payload_length,
                                      uint8_t *frame,
                                      uint16_t frame_capacity,
                                      uint16_t *frame_length)
{
  uint16_t body_length;
  uint16_t required_length;
  uint16_t crc;

  if ((frame == NULL) || (frame_length == NULL) ||
      ((payload == NULL) && (payload_length != 0U)) ||
      (payload_length > APP_PROTOCOL_MAX_PAYLOAD_LENGTH))
  {
    return HAL_ERROR;
  }

  body_length = APP_PROTOCOL_BODY_HEADER_LENGTH + payload_length;
  required_length = 2U + body_length + APP_PROTOCOL_CRC_LENGTH;
  if (frame_capacity < required_length)
  {
    return HAL_ERROR;
  }

  frame[0] = APP_PROTOCOL_SOF0;
  frame[1] = APP_PROTOCOL_SOF1;
  frame[2] = APP_PROTOCOL_VERSION;
  frame[3] = type;
  app_protocol_put_u16_le(&frame[4], sequence);
  app_protocol_put_u16_le(&frame[6], payload_length);

  if (payload_length != 0U)
  {
    (void)memcpy(&frame[8], payload, payload_length);
  }

  /* CRC 从 version 开始，覆盖 type、sequence、length 与 payload。 */
  crc = app_protocol_crc16_ccitt(&frame[2], body_length);
  app_protocol_put_u16_le(&frame[2U + body_length], crc);
  *frame_length = required_length;

  return HAL_OK;
}

void app_protocol_pack_env_report(const app_protocol_env_report_t *report,
                                  uint8_t payload[APP_PROTOCOL_ENV_REPORT_PAYLOAD_LENGTH])
{
  if ((report == NULL) || (payload == NULL))
  {
    return;
  }

  app_protocol_put_u32_le(&payload[0], report->uptime_ms);
  app_protocol_put_u32_le(&payload[4], report->sample_sequence);
  app_protocol_put_u32_le(&payload[8], (uint32_t)report->temperature_milli_c);
  app_protocol_put_u32_le(&payload[12], report->humidity_milli_rh);
  app_protocol_put_u32_le(&payload[16], report->illuminance_milli_lux);
  payload[20] = report->valid_flags;
  payload[21] = report->aht20_status;
  payload[22] = report->bh1750_status;
  payload[23] = 0U;
  app_protocol_put_u32_le(&payload[24], report->aht20_comm_errors);
  app_protocol_put_u32_le(&payload[28], report->bh1750_comm_errors);
  app_protocol_put_u32_le(&payload[32], report->aht20_data_errors);
  app_protocol_put_u32_le(&payload[36], report->bh1750_data_errors);
}

void app_protocol_parser_init(app_protocol_parser_t *parser)
{
  app_protocol_parser_reset(parser);
}

void app_protocol_parser_reset(app_protocol_parser_t *parser)
{
  if (parser == NULL)
  {
    return;
  }

  parser->state = APP_PROTOCOL_RX_WAIT_SOF0;
  parser->body_length = 0U;
  parser->payload_length = 0U;
  parser->crc_low = 0U;
}

app_protocol_parse_result_t app_protocol_parser_input(app_protocol_parser_t *parser,
                                                       uint8_t byte,
                                                       app_protocol_frame_t *frame)
{
  uint16_t received_crc;
  uint16_t calculated_crc;

  if ((parser == NULL) || (frame == NULL))
  {
    return APP_PROTOCOL_PARSE_NONE;
  }

  switch (parser->state)
  {
    case APP_PROTOCOL_RX_WAIT_SOF0:
      if (byte == APP_PROTOCOL_SOF0)
      {
        parser->state = APP_PROTOCOL_RX_WAIT_SOF1;
      }
      break;

    case APP_PROTOCOL_RX_WAIT_SOF1:
      if (byte == APP_PROTOCOL_SOF1)
      {
        parser->state = APP_PROTOCOL_RX_READ_BODY;
        parser->body_length = 0U;
      }
      else if (byte != APP_PROTOCOL_SOF0)
      {
        parser->state = APP_PROTOCOL_RX_WAIT_SOF0;
      }
      break;

    case APP_PROTOCOL_RX_READ_BODY:
      parser->body[parser->body_length++] = byte;

      if (parser->body_length == APP_PROTOCOL_BODY_HEADER_LENGTH)
      {
        if (parser->body[0] != APP_PROTOCOL_VERSION)
        {
          app_protocol_parser_reset(parser);
          return APP_PROTOCOL_PARSE_VERSION_ERROR;
        }

        parser->payload_length = app_protocol_get_u16_le(&parser->body[4]);
        if (parser->payload_length > APP_PROTOCOL_MAX_PAYLOAD_LENGTH)
        {
          app_protocol_parser_reset(parser);
          return APP_PROTOCOL_PARSE_LENGTH_ERROR;
        }

        if (parser->payload_length == 0U)
        {
          parser->state = APP_PROTOCOL_RX_READ_CRC_LOW;
        }
      }
      else if (parser->body_length == (APP_PROTOCOL_BODY_HEADER_LENGTH + parser->payload_length))
      {
        parser->state = APP_PROTOCOL_RX_READ_CRC_LOW;
      }
      break;

    case APP_PROTOCOL_RX_READ_CRC_LOW:
      parser->crc_low = byte;
      parser->state = APP_PROTOCOL_RX_READ_CRC_HIGH;
      break;

    case APP_PROTOCOL_RX_READ_CRC_HIGH:
      received_crc = (uint16_t)parser->crc_low | ((uint16_t)byte << 8U);
      calculated_crc = app_protocol_crc16_ccitt(parser->body, parser->body_length);

      if (received_crc != calculated_crc)
      {
        app_protocol_parser_reset(parser);
        return APP_PROTOCOL_PARSE_CRC_ERROR;
      }

      frame->type = parser->body[1];
      frame->sequence = app_protocol_get_u16_le(&parser->body[2]);
      frame->payload_length = parser->payload_length;
      if (parser->payload_length != 0U)
      {
        (void)memcpy(frame->payload, &parser->body[APP_PROTOCOL_BODY_HEADER_LENGTH],
                     parser->payload_length);
      }

      app_protocol_parser_reset(parser);
      return APP_PROTOCOL_PARSE_FRAME;

    default:
      app_protocol_parser_reset(parser);
      break;
  }

  return APP_PROTOCOL_PARSE_NONE;
}
