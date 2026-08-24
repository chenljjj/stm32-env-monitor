#ifndef APP_NET_STATUS_H
#define APP_NET_STATUS_H

#include <stdbool.h>
#include <stdint.h>

#include "app_protocol.h"

typedef struct
{
    uint8_t flags;
    uint8_t reason;
    uint32_t wifi_disconnect_count;
    uint32_t mqtt_disconnect_count;
} app_net_status_snapshot_t;

typedef void (*app_net_status_changed_handler_t)(const app_net_status_snapshot_t *status);

/* 初始化状态聚合器，并立即回调一次启动状态。 */
void app_net_status_init(app_net_status_changed_handler_t on_changed);

/* 由 Wi-Fi 和 MQTT 事件回调更新各自连接状态。 */
void app_net_status_set_wifi_connected(bool connected);
void app_net_status_set_mqtt_connected(bool connected);

/* 获取适合周期回传的原子快照。 */
void app_net_status_get(app_net_status_snapshot_t *status);

#endif /* APP_NET_STATUS_H */
