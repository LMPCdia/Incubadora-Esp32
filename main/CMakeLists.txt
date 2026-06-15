# Componente principal de la incubadora.
# Listamos explicitamente todos los .c y los componentes IDF necesarios.
#
# La pagina web vive en ../web/index.html y se embebe en flash al build
# via EMBED_FILES (queda accesible como _binary_index_html_start/_end).

idf_component_register(
    SRCS
        "main.c"
        "ds18b20.c"
        "ds18b20_probe.c"
        "ONEWire.c"
        "triac_ctrl.c"
        "wifi_mgr.c"
        "web_server.c"
    INCLUDE_DIRS
        "."
    EMBED_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/../web/index.html"
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
