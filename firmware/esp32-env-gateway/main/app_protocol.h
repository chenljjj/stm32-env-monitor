#ifndef APP_PROTOCOL_H
#define APP_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define APP_PROTOCOL_SOF0                       0xA5U
#define APP_PROTOCOL_SOF1                       0x5AU
#define APP_PROTOCOL_VERSION                    0x01U
#define APP_PROTOCOL_MAX_PAYLOAD_LENGTH         64U
#define APP_PROTOCOL_BODY_HEADER_LENGTH         6U
#define APP_PROTOCOL_CRC_LENGTH                 2U
#define APP_PROTOCOL_MAX_FRAME_LENGTH           (2U + APP_PROTOCOL_BODY_HEADER_LENGTH + \
                                                 APP_PROTOCOL_MAX_PAYLOAD_LENGTH + APP_PROTOCOL_CRC_LENGTH)
#define APP_PROTOCOL_ENV_REPORT_PAYLOAD_LENGTH  40U

typedef enum
{
    APP_PROTOCOL_TYPE_ENV_REPORT = 0x01U,
    APP_PROTOCOL_TYPE_PING       = 0x02U,
    APP_PROTOCOL_TYPE_ACK        = 0x80U,
    APP_PROTOCOL_TYPE_NET_STATUS = 0x81U,
    APP_PROTOCOL_TYPE_PONG       = 0x82U
} app_protocol_type_t;

typedef struct
{
    uint8_t type;
    uint16_t sequence;
    uint16_t payload_length;
    uint8_t payload[APP_PROTOCOL_MAX_PAYLOAD_LENGTH];
} app_protocol_frame_t;

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

typedef struct
{
    app_protocol_rx_state_t state;
    uint8_t body[APP_PROTOCOL_BODY_HEADER_LENGTH + APP_PROTOCOL_MAX_PAYLOAD_LENGTH];
    uint16_t body_length;
    uint16_t payload_length;
    uint8_t crc_low;
} app_protocol_parser_t;

uint16_t app_protocol_crc16_ccitt(const uint8_t *data, size_t length);

bool app_protocol_encode(uint8_t type,
                         uint16_t sequence,
                         const uint8_t *payload,
                         uint16_t payload_length,
                         uint8_t *frame,
                         size_t frame_capacity,
                         uint16_t *frame_length);

void app_protocol_parser_init(app_protocol_parser_t *parser);
app_protocol_parse_result_t app_protocol_parser_input(app_protocol_parser_t *parser,
                                                       uint8_t byte,
                                                       app_protocol_frame_t *frame);

bool app_protocol_unpack_env_report(const app_protocol_frame_t *frame,
                                    app_protocol_env_report_t *report);

#endif /* APP_PROTOCOL_H */
