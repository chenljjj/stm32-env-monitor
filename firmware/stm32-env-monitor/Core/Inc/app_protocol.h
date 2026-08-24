#ifndef APP_PROTOCOL_H
#define APP_PROTOCOL_H

#include <stdint.h>

#include "stm32f1xx_hal.h"

/* STM32 与 ESP32 串口协议的固定参数。 */
#define APP_PROTOCOL_SOF0                     0xA5U
#define APP_PROTOCOL_SOF1                     0x5AU
#define APP_PROTOCOL_VERSION                  0x01U
#define APP_PROTOCOL_MAX_PAYLOAD_LENGTH       64U
#define APP_PROTOCOL_BODY_HEADER_LENGTH       6U
#define APP_PROTOCOL_CRC_LENGTH               2U
#define APP_PROTOCOL_MAX_FRAME_LENGTH         (2U + APP_PROTOCOL_BODY_HEADER_LENGTH + \
                                               APP_PROTOCOL_MAX_PAYLOAD_LENGTH + APP_PROTOCOL_CRC_LENGTH)
#define APP_PROTOCOL_ENV_REPORT_PAYLOAD_LENGTH 40U
#define APP_PROTOCOL_NET_STATUS_PAYLOAD_LENGTH 12U

#define APP_PROTOCOL_NET_FLAG_WIFI_CONNECTED 0x01U
#define APP_PROTOCOL_NET_FLAG_MQTT_CONNECTED 0x02U

typedef enum
{
  APP_PROTOCOL_TYPE_ENV_REPORT = 0x01U,
  APP_PROTOCOL_TYPE_PING       = 0x02U,
  APP_PROTOCOL_TYPE_ACK        = 0x80U,
  APP_PROTOCOL_TYPE_NET_STATUS = 0x81U,
  APP_PROTOCOL_TYPE_PONG       = 0x82U
} app_protocol_type_t;

/* ACK 载荷最后一个字节的处理结果。 */
typedef enum
{
  APP_PROTOCOL_ACK_RESULT_OK               = 0x00U,
  APP_PROTOCOL_ACK_RESULT_UNSUPPORTED_TYPE = 0x01U,
  APP_PROTOCOL_ACK_RESULT_INVALID_PAYLOAD  = 0x02U
} app_protocol_ack_result_t;

typedef enum
{
  APP_PROTOCOL_NET_REASON_STARTUP   = 0U,
  APP_PROTOCOL_NET_REASON_WIFI_UP   = 1U,
  APP_PROTOCOL_NET_REASON_WIFI_DOWN = 2U,
  APP_PROTOCOL_NET_REASON_MQTT_UP   = 3U,
  APP_PROTOCOL_NET_REASON_MQTT_DOWN = 4U
} app_protocol_net_reason_t;

/* 解码完成的一帧数据，不包含帧头和 CRC。 */
typedef struct
{
  uint8_t type;
  uint16_t sequence;
  uint16_t payload_length;
  uint8_t payload[APP_PROTOCOL_MAX_PAYLOAD_LENGTH];
} app_protocol_frame_t;

/* ENV_REPORT 固定载荷的逻辑字段，编码时显式按小端序写入。 */
typedef struct
{
  uint32_t uptime_ms;
  uint32_t sample_sequence;
  int32_t temperature_milli_c;
  uint32_t humidity_milli_rh;
  uint32_t illuminance_milli_lux;
  uint8_t valid_flags;
  uint8_t aht20_status;
  uint8_t bh1750_status;
  uint32_t aht20_comm_errors;
  uint32_t bh1750_comm_errors;
  uint32_t aht20_data_errors;
  uint32_t bh1750_data_errors;
} app_protocol_env_report_t;

/* ESP32 回传的网络状态快照。 */
typedef struct
{
  uint8_t flags;
  uint8_t reason;
  uint32_t wifi_disconnect_count;
  uint32_t mqtt_disconnect_count;
} app_protocol_net_status_t;

typedef enum
{
  APP_PROTOCOL_PARSE_NONE = 0,
  APP_PROTOCOL_PARSE_FRAME,
  APP_PROTOCOL_PARSE_VERSION_ERROR,
  APP_PROTOCOL_PARSE_LENGTH_ERROR,
  APP_PROTOCOL_PARSE_CRC_ERROR
} app_protocol_parse_result_t;

typedef enum
{
  APP_PROTOCOL_RX_WAIT_SOF0 = 0,
  APP_PROTOCOL_RX_WAIT_SOF1,
  APP_PROTOCOL_RX_READ_BODY,
  APP_PROTOCOL_RX_READ_CRC_LOW,
  APP_PROTOCOL_RX_READ_CRC_HIGH
} app_protocol_rx_state_t;

/* body 保存 version 至 payload 的原始字节，用于校验 CRC。 */
typedef struct
{
  app_protocol_rx_state_t state;
  uint8_t body[APP_PROTOCOL_BODY_HEADER_LENGTH + APP_PROTOCOL_MAX_PAYLOAD_LENGTH];
  uint16_t body_length;
  uint16_t payload_length;
  uint8_t crc_low;
} app_protocol_parser_t;

uint16_t app_protocol_crc16_ccitt(const uint8_t *data, uint16_t length);

HAL_StatusTypeDef app_protocol_encode(uint8_t type,
                                      uint16_t sequence,
                                      const uint8_t *payload,
                                      uint16_t payload_length,
                                      uint8_t *frame,
                                      uint16_t frame_capacity,
                                      uint16_t *frame_length);

void app_protocol_pack_env_report(const app_protocol_env_report_t *report,
                                  uint8_t payload[APP_PROTOCOL_ENV_REPORT_PAYLOAD_LENGTH]);

HAL_StatusTypeDef app_protocol_unpack_net_status(const app_protocol_frame_t *frame,
                                                  app_protocol_net_status_t *status);

void app_protocol_parser_init(app_protocol_parser_t *parser);
void app_protocol_parser_reset(app_protocol_parser_t *parser);

app_protocol_parse_result_t app_protocol_parser_input(app_protocol_parser_t *parser,
                                                       uint8_t byte,
                                                       app_protocol_frame_t *frame);

#endif /* APP_PROTOCOL_H */
