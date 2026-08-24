#ifndef APP_MQTT_H
#define APP_MQTT_H

#include <stdbool.h>
#include <stdint.h>

#include "app_protocol.h"

#define APP_MQTT_TELEMETRY_TOPIC "stm32-env-monitor/telemetry"
#define APP_MQTT_STATUS_TOPIC    "stm32-env-monitor/status"

typedef struct
{
    uint32_t submitted_count;
    uint32_t queue_overflow_count;
    uint32_t publish_accepted_count;
    uint32_t publish_error_count;
    uint32_t abandoned_count;
    uint32_t puback_count;
    uint32_t pending_count;
} app_mqtt_stats_t;

/* 创建遥测队列和发布任务，应在开始接收 UART 数据前调用。 */
void app_mqtt_init(void);

/* 启动带 TLS 和自动重连的 MQTT 客户端。 */
void app_mqtt_start(void);

/* 将 STM32 环境采样加入有界队列，由发布任务按 QoS 1 发送。 */
bool app_mqtt_submit_env_report(const app_protocol_env_report_t *report);

/* 返回 MQTT 会话当前是否已完成连接。 */
bool app_mqtt_is_connected(void);

/* 获取队列、发布和 Broker PUBACK 的累计统计。 */
void app_mqtt_get_stats(app_mqtt_stats_t *stats);

#endif /* APP_MQTT_H */
