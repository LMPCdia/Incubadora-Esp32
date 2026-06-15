/*
 * wifi_ap.c - Inicializa WiFi en modo AP (soft-AP).
 *
 * IP por defecto del AP en ESP-IDF: 192.168.4.1
 */

#include "wifi_ap.h"
#include "config.h"

#include <string.h>

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"      /* MACSTR, MAC2STR */
#include "esp_netif.h"
#include "nvs_flash.h"

static const char *TAG = "wifi_ap";

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *e = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "Cliente conectado " MACSTR " (AID=%d)",
                 MAC2STR(e->mac), e->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *e = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "Cliente desconectado " MACSTR, MAC2STR(e->mac));
    }
}

void wifi_ap_start(void)
{
    /* NVS necesaria para WiFi */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = { 0 };
    strncpy((char *)wifi_config.ap.ssid, WIFI_AP_SSID,
            sizeof(wifi_config.ap.ssid));
    wifi_config.ap.ssid_len = strlen(WIFI_AP_SSID);
    strncpy((char *)wifi_config.ap.password, WIFI_AP_PASS,
            sizeof(wifi_config.ap.password));
    wifi_config.ap.channel = WIFI_AP_CHANNEL;
    wifi_config.ap.max_connection = WIFI_AP_MAX_CONN;
    wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    if (strlen(WIFI_AP_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP listo. SSID:%s pass:%s  ->  http://192.168.4.1/",
             WIFI_AP_SSID, WIFI_AP_PASS);
}
