/*
 * wifi_mgr.c - Implementacion del manager WiFi APSTA con NVS.
 *
 * Almacenamiento NVS:
 *   namespace : "wifi_creds"
 *   keys      : "ssid", "pass"
 *
 * Reintentos STA: hasta STA_RETRY_MAX antes de pasar a STA_FAILED.
 * El handler de WIFI_EVENT_STA_DISCONNECTED dispara el reintento.
 */

#include "wifi_mgr.h"
#include "config.h"

#include <string.h>

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "wifi_mgr";

#define NVS_NS         "wifi_creds"
#define NVS_KEY_SSID   "ssid"
#define NVS_KEY_PASS   "pass"
#define STA_RETRY_MAX  6

static volatile wifi_mgr_state_t s_state = WIFI_MGR_AP_ONLY;
static int          s_sta_retries = 0;
static char         s_sta_ssid[33] = {0};
static esp_netif_t *s_sta_netif = NULL;

/* ---------- Event handlers ---------- */

static void on_wifi_event(void *arg, esp_event_base_t base,
                          int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        switch (id) {
            case WIFI_EVENT_AP_STACONNECTED: {
                wifi_event_ap_staconnected_t *e = data;
                ESP_LOGI(TAG, "Cliente AP+: " MACSTR, MAC2STR(e->mac));
                break;
            }
            case WIFI_EVENT_AP_STADISCONNECTED: {
                wifi_event_ap_stadisconnected_t *e = data;
                ESP_LOGI(TAG, "Cliente AP-: " MACSTR, MAC2STR(e->mac));
                break;
            }
            case WIFI_EVENT_STA_START:
                if (strlen(s_sta_ssid) > 0) {
                    ESP_LOGI(TAG, "STA start -> connect '%s'", s_sta_ssid);
                    s_state = WIFI_MGR_STA_CONNECTING;
                    esp_wifi_connect();
                }
                break;
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "STA asociado a '%s', esperando IP...", s_sta_ssid);
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                s_sta_retries++;
                if (s_sta_retries <= STA_RETRY_MAX) {
                    ESP_LOGW(TAG, "STA desconectada, reintento %d/%d",
                             s_sta_retries, STA_RETRY_MAX);
                    s_state = WIFI_MGR_STA_CONNECTING;
                    esp_wifi_connect();
                } else {
                    ESP_LOGE(TAG, "STA fallo tras %d intentos", STA_RETRY_MAX);
                    s_state = WIFI_MGR_STA_FAILED;
                }
                break;
            default: break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = data;
        ESP_LOGI(TAG, "STA got IP: " IPSTR, IP2STR(&e->ip_info.ip));
        s_sta_retries = 0;
        s_state = WIFI_MGR_STA_CONNECTED;
    }
}

/* ---------- NVS helpers ---------- */

static esp_err_t load_credentials(char *ssid, size_t ssid_sz,
                                  char *pass, size_t pass_sz)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err != ESP_OK) return err;

    size_t s = ssid_sz;
    err = nvs_get_str(h, NVS_KEY_SSID, ssid, &s);
    if (err == ESP_OK) {
        s = pass_sz;
        err = nvs_get_str(h, NVS_KEY_PASS, pass, &s);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            /* Red abierta: SSID guardado sin pass */
            pass[0] = 0;
            err = ESP_OK;
        }
    }
    nvs_close(h);
    return err;
}

static esp_err_t save_credentials(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    err = nvs_set_str(h, NVS_KEY_SSID, ssid);
    if (err == ESP_OK) err = nvs_set_str(h, NVS_KEY_PASS, pass ? pass : "");
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

/* ---------- API publica ---------- */

void wifi_mgr_start(void)
{
    /* NVS necesaria para WiFi y para guardar credenciales */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    /* netif + event loop */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    s_sta_netif = esp_netif_create_default_wifi_sta();

    /* WiFi init */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &on_wifi_event, NULL, NULL));

    /* AP config (siempre activo, para provisioning y fallback) */
    wifi_config_t ap = { 0 };
    strncpy((char *)ap.ap.ssid, WIFI_AP_SSID, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = strlen(WIFI_AP_SSID);
    strncpy((char *)ap.ap.password, WIFI_AP_PASS, sizeof(ap.ap.password));
    ap.ap.channel        = WIFI_AP_CHANNEL;
    ap.ap.max_connection = WIFI_AP_MAX_CONN;
    ap.ap.authmode       = (strlen(WIFI_AP_PASS) > 0)
                              ? WIFI_AUTH_WPA_WPA2_PSK
                              : WIFI_AUTH_OPEN;

    /* STA config desde NVS si existe */
    char ssid[33] = {0}, pass[65] = {0};
    bool have_creds = (load_credentials(ssid, sizeof(ssid),
                                        pass, sizeof(pass)) == ESP_OK)
                       && strlen(ssid) > 0;

    wifi_config_t sta = { 0 };
    if (have_creds) {
        strncpy((char *)sta.sta.ssid, ssid, sizeof(sta.sta.ssid));
        strncpy((char *)sta.sta.password, pass, sizeof(sta.sta.password));
        sta.sta.threshold.authmode = WIFI_AUTH_OPEN;  /* aceptar abiertas */
        strncpy(s_sta_ssid, ssid, sizeof(s_sta_ssid) - 1);
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    if (have_creds) {
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
        s_state = WIFI_MGR_STA_CONNECTING;
    } else {
        s_state = WIFI_MGR_AP_ONLY;
    }
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP up: '%s' -> http://192.168.4.1/", WIFI_AP_SSID);
    if (have_creds) {
        ESP_LOGI(TAG, "STA configurado para '%s' (intentando conectar)", ssid);
    } else {
        ESP_LOGI(TAG, "STA sin credenciales - configurar via /wifi");
    }
}

bool wifi_mgr_set_credentials(const char *ssid, const char *pass)
{
    if (!ssid) return false;
    size_t slen = strlen(ssid);
    if (slen == 0 || slen > 32) return false;
    if (pass && strlen(pass) > 64) return false;

    esp_err_t err = save_credentials(ssid, pass);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save_credentials: %s", esp_err_to_name(err));
        return false;
    }

    /* Aplica al toque sin reboot */
    wifi_config_t sta = { 0 };
    strncpy((char *)sta.sta.ssid, ssid, sizeof(sta.sta.ssid));
    if (pass) strncpy((char *)sta.sta.password, pass, sizeof(sta.sta.password));
    sta.sta.threshold.authmode = WIFI_AUTH_OPEN;

    memset(s_sta_ssid, 0, sizeof(s_sta_ssid));
    strncpy(s_sta_ssid, ssid, sizeof(s_sta_ssid) - 1);
    s_sta_retries = 0;
    s_state = WIFI_MGR_STA_CONNECTING;

    esp_wifi_disconnect();
    err = esp_wifi_set_config(WIFI_IF_STA, &sta);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_config STA: %s", esp_err_to_name(err));
        return false;
    }
    esp_wifi_connect();

    ESP_LOGI(TAG, "Credenciales actualizadas -> conectando a '%s'", ssid);
    return true;
}

void wifi_mgr_clear_credentials(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
    esp_wifi_disconnect();
    memset(s_sta_ssid, 0, sizeof(s_sta_ssid));
    s_state = WIFI_MGR_AP_ONLY;
    ESP_LOGI(TAG, "Credenciales borradas - solo AP activo");
}

wifi_mgr_state_t wifi_mgr_get_state(void)
{
    return s_state;
}

void wifi_mgr_get_info(char *ssid_out, size_t ssid_len,
                       char *ip_out,   size_t ip_len,
                       int  *rssi_out)
{
    if (ssid_out && ssid_len > 0) {
        strncpy(ssid_out, s_sta_ssid, ssid_len - 1);
        ssid_out[ssid_len - 1] = 0;
    }
    if (ip_out && ip_len > 0) {
        ip_out[0] = 0;
        if (s_state == WIFI_MGR_STA_CONNECTED && s_sta_netif) {
            esp_netif_ip_info_t ip;
            if (esp_netif_get_ip_info(s_sta_netif, &ip) == ESP_OK) {
                snprintf(ip_out, ip_len, IPSTR, IP2STR(&ip.ip));
            }
        }
    }
    if (rssi_out) {
        *rssi_out = 0;
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            *rssi_out = ap.rssi;
        }
    }
}
