#include "app_net_status.h"

#include <string.h>

#include "freertos/FreeRTOS.h"

#include "app_protocol.h"

static portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;
static app_net_status_snapshot_t s_status;
static app_net_status_changed_handler_t s_on_changed;

static void app_net_status_notify(const app_net_status_snapshot_t *status,
                                  app_net_status_changed_handler_t on_changed)
{
    if (on_changed != NULL)
    {
        on_changed(status);
    }
}

static void app_net_status_set_connected(uint8_t flag,
                                         bool connected,
                                         app_protocol_net_reason_t up_reason,
                                         app_protocol_net_reason_t down_reason)
{
    app_net_status_snapshot_t snapshot;
    app_net_status_changed_handler_t on_changed;
    bool was_connected;

    portENTER_CRITICAL(&s_status_lock);
    was_connected = (s_status.flags & flag) != 0U;
    if (was_connected == connected)
    {
        portEXIT_CRITICAL(&s_status_lock);
        return;
    }

    if (connected)
    {
        s_status.flags |= flag;
        s_status.reason = (uint8_t)up_reason;
    }
    else
    {
        s_status.flags &= (uint8_t)~flag;
        s_status.reason = (uint8_t)down_reason;
        if (flag == APP_PROTOCOL_NET_FLAG_WIFI_CONNECTED)
        {
            s_status.wifi_disconnect_count++;
        }
        else if (flag == APP_PROTOCOL_NET_FLAG_MQTT_CONNECTED)
        {
            s_status.mqtt_disconnect_count++;
        }
    }

    snapshot = s_status;
    on_changed = s_on_changed;
    portEXIT_CRITICAL(&s_status_lock);

    app_net_status_notify(&snapshot, on_changed);
}

void app_net_status_init(app_net_status_changed_handler_t on_changed)
{
    app_net_status_snapshot_t snapshot;

    portENTER_CRITICAL(&s_status_lock);
    (void)memset(&s_status, 0, sizeof(s_status));
    s_status.reason = APP_PROTOCOL_NET_REASON_STARTUP;
    s_on_changed = on_changed;
    snapshot = s_status;
    portEXIT_CRITICAL(&s_status_lock);

    app_net_status_notify(&snapshot, on_changed);
}

void app_net_status_set_wifi_connected(bool connected)
{
    app_net_status_set_connected(APP_PROTOCOL_NET_FLAG_WIFI_CONNECTED,
                                 connected,
                                 APP_PROTOCOL_NET_REASON_WIFI_UP,
                                 APP_PROTOCOL_NET_REASON_WIFI_DOWN);
}

void app_net_status_set_mqtt_connected(bool connected)
{
    app_net_status_set_connected(APP_PROTOCOL_NET_FLAG_MQTT_CONNECTED,
                                 connected,
                                 APP_PROTOCOL_NET_REASON_MQTT_UP,
                                 APP_PROTOCOL_NET_REASON_MQTT_DOWN);
}

void app_net_status_get(app_net_status_snapshot_t *status)
{
    if (status == NULL)
    {
        return;
    }

    portENTER_CRITICAL(&s_status_lock);
    *status = s_status;
    portEXIT_CRITICAL(&s_status_lock);
}
