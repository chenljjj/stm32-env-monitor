idf_component_register(SRCS "main.c" "app_protocol.c" "app_net_status.c" "app_wifi.c" "app_mqtt.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_driver_gpio esp_driver_uart esp_event esp_netif esp_wifi mqtt nvs_flash
                    EMBED_TXTFILES "emqxsl-ca.crt")
