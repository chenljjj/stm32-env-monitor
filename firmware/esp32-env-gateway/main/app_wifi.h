#ifndef APP_WIFI_H
#define APP_WIFI_H

#include <stdbool.h>

typedef void (*app_wifi_connected_handler_t)(void);

/* 启动 STA 模式；断线后由事件回调自动发起重连。 */
void app_wifi_start(app_wifi_connected_handler_t on_connected);

/* 返回当前是否已获得 IPv4 地址。 */
bool app_wifi_is_connected(void);

#endif /* APP_WIFI_H */
