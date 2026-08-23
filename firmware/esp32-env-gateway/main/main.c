#include <inttypes.h>

#include "app_protocol.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"

#define APP_UART_PORT             UART_NUM_2
#define APP_UART_BAUD_RATE        115200
#define APP_UART_RX_PIN           GPIO_NUM_25
#define APP_UART_TX_PIN           GPIO_NUM_26
#define APP_UART_RX_BUFFER_SIZE   512
/* 未收满缓冲区时最多等待 20ms，确保 ACK 不会落后一个采样周期。 */
#define APP_UART_READ_TIMEOUT_MS  20

#define APP_PROTOCOL_FLAG_CLIMATE_VALID      0x01U
#define APP_PROTOCOL_FLAG_ILLUMINANCE_VALID  0x02U
#define APP_PROTOCOL_FLAG_DISPLAY_READY      0x04U

static const char *TAG = "env_gateway";
static uint16_t app_uart_tx_sequence;

static void app_uart_init(void)
{
    const uart_config_t config = {
        .baud_rate = APP_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_param_config(APP_UART_PORT, &config));
    ESP_ERROR_CHECK(uart_set_pin(APP_UART_PORT,
                                 APP_UART_TX_PIN,
                                 APP_UART_RX_PIN,
                                 UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(APP_UART_PORT,
                                       APP_UART_RX_BUFFER_SIZE,
                                       0,
                                       0,
                                       NULL,
                                       0));
}

/* 向 STM32 确认收到一帧；STM32 接收处理将在后续联调阶段接入。 */
static void app_uart_send_ack(uint16_t received_sequence, uint8_t received_type)
{
    uint8_t payload[4];
    uint8_t frame[APP_PROTOCOL_MAX_FRAME_LENGTH];
    uint16_t frame_length;

    payload[0] = (uint8_t)(received_sequence & 0xFFU);
    payload[1] = (uint8_t)(received_sequence >> 8U);
    payload[2] = received_type;
    payload[3] = 0U;

    if (!app_protocol_encode(APP_PROTOCOL_TYPE_ACK,
                             app_uart_tx_sequence++,
                             payload,
                             sizeof(payload),
                             frame,
                             sizeof(frame),
                             &frame_length))
    {
        ESP_LOGE(TAG, "ACK 编码失败");
        return;
    }

    if (uart_write_bytes(APP_UART_PORT, frame, frame_length) < 0)
    {
        ESP_LOGE(TAG, "ACK 发送失败");
    }
}

static void app_log_env_report(const app_protocol_frame_t *frame)
{
    app_protocol_env_report_t report;

    if (!app_protocol_unpack_env_report(frame, &report))
    {
        ESP_LOGW(TAG, "ENV_REPORT 载荷长度错误: %u", frame->payload_length);
        return;
    }

    ESP_LOGI(TAG,
             "sample=%" PRIu32 " uptime=%" PRIu32 "ms T=%" PRId32 ".%03" PRId32
             "C H=%" PRIu32 ".%03" PRIu32 "%% L=%" PRIu32 ".%03" PRIu32
             "lx valid=%c%c%c err=%" PRIu32 "/%" PRIu32 ",%" PRIu32 "/%" PRIu32,
             report.sample_sequence,
             report.uptime_ms,
             report.temperature_milli_c / 1000,
             (report.temperature_milli_c < 0 ? -report.temperature_milli_c : report.temperature_milli_c) % 1000,
             report.humidity_milli_rh / 1000U,
             report.humidity_milli_rh % 1000U,
             report.illuminance_milli_lux / 1000U,
             report.illuminance_milli_lux % 1000U,
             (report.valid_flags & APP_PROTOCOL_FLAG_CLIMATE_VALID) != 0U ? 'C' : '-',
             (report.valid_flags & APP_PROTOCOL_FLAG_ILLUMINANCE_VALID) != 0U ? 'L' : '-',
             (report.valid_flags & APP_PROTOCOL_FLAG_DISPLAY_READY) != 0U ? 'D' : '-',
             report.aht20_comm_errors,
             report.bh1750_comm_errors,
             report.aht20_data_errors,
             report.bh1750_data_errors);
}

static void app_handle_frame(const app_protocol_frame_t *frame)
{
    if (frame->type == APP_PROTOCOL_TYPE_ENV_REPORT)
    {
        /* ACK 优先于调试日志，避免日志输出拖慢协议确认。 */
        app_uart_send_ack(frame->sequence, frame->type);
        app_log_env_report(frame);
        return;
    }

    ESP_LOGW(TAG, "未处理消息: type=0x%02X sequence=%u length=%u",
             frame->type,
             frame->sequence,
             frame->payload_length);
}

void app_main(void)
{
    uint8_t rx_buffer[128];
    app_protocol_parser_t parser;
    app_protocol_frame_t frame;

    app_uart_init();
    app_protocol_parser_init(&parser);
    ESP_LOGI(TAG, "UART2 已启动: RX=GPIO%d TX=GPIO%d %d-8-N-1",
             APP_UART_RX_PIN,
             APP_UART_TX_PIN,
             APP_UART_BAUD_RATE);

    while (true)
    {
        const int received = uart_read_bytes(APP_UART_PORT,
                                             rx_buffer,
                                             sizeof(rx_buffer),
                                             pdMS_TO_TICKS(APP_UART_READ_TIMEOUT_MS));
        for (int index = 0; index < received; index++)
        {
            const app_protocol_parse_result_t result =
                app_protocol_parser_input(&parser, rx_buffer[index], &frame);

            if (result == APP_PROTOCOL_PARSE_FRAME)
            {
                app_handle_frame(&frame);
            }
            else if (result != APP_PROTOCOL_PARSE_NONE)
            {
                ESP_LOGW(TAG, "协议解析错误: %d", result);
            }
        }
    }
}
