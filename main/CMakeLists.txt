# Componente principal de la incubadora.
# Listamos explicitamente todos los .c y los componentes IDF necesarios.

idf_component_register(
    SRCS
        "main.c"
        "ds18b20.c"
        "ds18b20_probe.c"
        "ONEWire.c"
        "triac_ctrl.c"
        "wifi_ap.c"
        "web_server.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        driver
        esp_timer
        esp_wifi
        esp_event
        esp_netif
        esp_http_server
        nvs_flash
        freertos
)
