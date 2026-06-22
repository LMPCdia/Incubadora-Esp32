/*
 * web_server.c - HTTP server + pagina web embebida
 *
 * La pagina vive en web/index.html (un unico archivo HTML+CSS+JS).
 * Se incrusta en flash via EMBED_FILES (ver main/CMakeLists.txt).
 *
 * Endpoints:
 *   GET  /          -> index.html (la UI)
 *   GET  /api       -> snapshot JSON con t, set, pot, td, net, sens
 *   POST /setpoint  -> body "t=37.5" para fijar setpoint
 *   OPTIONS *       -> CORS preflight (responde con headers permisivos)
 *
 * CORS: agregamos Access-Control-Allow-Origin: * en /api y /setpoint
 * para permitir que la misma UI hosteada en Vercel (HTTPS, otro origen)
 * pueda hacer fetch al ESP32. El usuario debe permitir mixed-content
 * en el browser para que el HTTPS de Vercel pueda llamar HTTP del ESP32.
 */

#include "web_server.h"
#include "config.h"
#include "wifi_mgr.h"
#include "triac_ctrl.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "web";

static SemaphoreHandle_t s_lock;
static ctrl_status_t     s_status;
static float             s_setpoint_c = SETPOINT_DEFAULT_C;

/* HTML embebido desde web/index.html (ver main/CMakeLists.txt) */
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");

/* ------------ Helpers CORS ------------ */

static void cors_set_headers(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin",  "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    httpd_resp_set_hdr(req, "Access-Control-Max-Age",       "600");
}

/* Handler OPTIONS para preflight. Algunos browsers mandan preflight para
 * POST con Content-Type que no sea text/plain (el nuestro es form-urlencoded
 * que NO requiere preflight, pero igual respondemos por las dudas). */
static esp_err_t options_any(httpd_req_t *req)
{
    cors_set_headers(req);
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

/* ------------ Handlers HTTP ------------ */

static esp_err_t index_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    /* Cache moderado para que el browser no recargue todo cada vez,
     * pero permita actualizar tras OTA / reflash. */
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, max-age=60");
    const size_t len = index_html_end - index_html_start;
    return httpd_resp_send(req, (const char *)index_html_start, len);
}

static esp_err_t api_get(httpd_req_t *req)
{
    ctrl_status_t snap;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    snap = s_status;
    xSemaphoreGive(s_lock);

    char buf[192];
    float t = isnan(snap.t_med_c) ? 0.0f : snap.t_med_c;
    int n = snprintf(buf, sizeof(buf),
        "{\"t\":%.2f,\"set\":%.2f,\"pot\":%.1f,\"td\":%u,"
        "\"net\":%s,\"sens\":%s}",
        t, snap.t_set_c, snap.potencia_pct,
        (unsigned)snap.t_dis_us,
        snap.red_ok ? "true" : "false",
        snap.sensor_ok ? "true" : "false");
    if (n <= 0) return ESP_FAIL;

    cors_set_headers(req);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, buf, n);
}

static esp_err_t setpoint_post(httpd_req_t *req)
{
    char body[64];
    int len = req->content_len;
    if (len <= 0 || len >= (int)sizeof(body)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad len");
        return ESP_FAIL;
    }
    int got = httpd_req_recv(req, body, len);
    if (got <= 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv");
        return ESP_FAIL;
    }
    body[got] = 0;

    /* parsea "t=37.5" */
    char *p = strstr(body, "t=");
    if (!p) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing t");
        return ESP_FAIL;
    }
    float v = strtof(p + 2, NULL);
    if (v < TEMP_MIN_C) v = TEMP_MIN_C;
    if (v > TEMP_MAX_C) v = TEMP_MAX_C;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_setpoint_c = v;
    xSemaphoreGive(s_lock);

    ESP_LOGI(TAG, "Nuevo setpoint: %.2f C", v);

    cors_set_headers(req);
    return httpd_resp_send(req, "OK", 2);
}

/* ------------ Endpoints WiFi (provisioning) ------------ */

/* GET /wifi/status -> JSON con estado de WiFi:
 *   { "state": "ap_only|connecting|connected|failed",
 *     "ssid":  "...",      (vacio si no configurado)
 *     "ip":    "...",      (vacio si no conectado)
 *     "rssi":  -54         (0 si no conectado) }
 */
static esp_err_t wifi_status_get(httpd_req_t *req)
{
    char ssid[33] = {0}, ip[20] = {0};
    int  rssi = 0;
    wifi_mgr_get_info(ssid, sizeof(ssid), ip, sizeof(ip), &rssi);
    wifi_mgr_state_t st = wifi_mgr_get_state();

    const char *state_str = "ap_only";
    switch (st) {
        case WIFI_MGR_STA_CONNECTING: state_str = "connecting"; break;
        case WIFI_MGR_STA_CONNECTED:  state_str = "connected";  break;
        case WIFI_MGR_STA_FAILED:     state_str = "failed";     break;
        default: break;
    }

    char buf[256];
    int n = snprintf(buf, sizeof(buf),
        "{\"state\":\"%s\",\"ssid\":\"%s\",\"ip\":\"%s\",\"rssi\":%d}",
        state_str, ssid, ip, rssi);
    if (n <= 0) return ESP_FAIL;

    cors_set_headers(req);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, buf, n);
}

/* Parsea valor de un campo form-urlencoded "key=...&key2=..." dentro de buf.
 * Hace url-decoding basico (espacios, %xx).  out queda null-terminated. */
static bool form_get(const char *body, const char *key,
                     char *out, size_t out_sz)
{
    size_t klen = strlen(key);
    const char *p = body;
    while (p && *p) {
        if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
            p += klen + 1;
            size_t i = 0;
            while (*p && *p != '&' && i + 1 < out_sz) {
                char c = *p++;
                if (c == '+') c = ' ';
                else if (c == '%' && p[0] && p[1]) {
                    char h[3] = { p[0], p[1], 0 };
                    c = (char)strtol(h, NULL, 16);
                    p += 2;
                }
                out[i++] = c;
            }
            out[i] = 0;
            return true;
        }
        /* avanzar al proximo & */
        const char *amp = strchr(p, '&');
        if (!amp) break;
        p = amp + 1;
    }
    out[0] = 0;
    return false;
}

/* POST /wifi  body "ssid=MiRed&pass=clave123"
 * Guarda credenciales en NVS y aplica al toque (no requiere reboot).
 */
static esp_err_t wifi_post(httpd_req_t *req)
{
    char body[160];
    int len = req->content_len;
    if (len <= 0 || len >= (int)sizeof(body)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad len");
        return ESP_FAIL;
    }
    int got = httpd_req_recv(req, body, len);
    if (got <= 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv");
        return ESP_FAIL;
    }
    body[got] = 0;

    char ssid[33] = {0}, pass[65] = {0};
    if (!form_get(body, "ssid", ssid, sizeof(ssid)) || strlen(ssid) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing ssid");
        return ESP_FAIL;
    }
    form_get(body, "pass", pass, sizeof(pass));  /* opcional (red abierta) */

    if (!wifi_mgr_set_credentials(ssid, pass)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "save failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Provisioning OK -> conectando a '%s'", ssid);
    cors_set_headers(req);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req,
        "{\"ok\":true,\"state\":\"connecting\"}",
        HTTPD_RESP_USE_STRLEN);
}

/* POST /wifi/forget -> borra credenciales, vuelve a AP-only */
static esp_err_t wifi_forget_post(httpd_req_t *req)
{
    wifi_mgr_clear_credentials();
    cors_set_headers(req);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
}

/* POST /triac/test  body "ms=5000"
 * Fuerza el gate del TRIAC en HIGH continuo durante ms milisegundos
 * para verificar el hardware downstream del GPIO. Pasar ms=0 para
 * cancelar el test en curso.
 *
 * USO: ponete el multimetro entre GPIO 4 y GND, dispara este endpoint
 * con ms=5000, en esos 5 segundos el multimetro debe leer ~3.3V firme.
 * Si lee 3.3V -> control bien, problema en MOC3021/TRIAC/carga.
 * Si lee 0V o menos de 3.0V -> problema en el GPIO o en el ESP32.
 */
static esp_err_t triac_test_post(httpd_req_t *req)
{
    char body[32] = {0};
    int len = req->content_len;
    if (len > 0 && len < (int)sizeof(body)) {
        int got = httpd_req_recv(req, body, len);
        if (got > 0) body[got] = 0;
    }
    char ms_str[16] = "0";
    form_get(body, "ms", ms_str, sizeof(ms_str));
    long ms = strtol(ms_str, NULL, 10);
    if (ms < 0)      ms = 0;
    if (ms > 30000)  ms = 30000;     /* tope de 30 s por seguridad */

    triac_force_on((uint32_t)ms);

    char resp[64];
    snprintf(resp, sizeof(resp), "{\"ok\":true,\"forced_ms\":%ld}", ms);
    cors_set_headers(req);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
}

/* ------------ Inicializacion ------------ */

void web_server_start(void)
{
    s_lock = xSemaphoreCreateMutex();
    memset(&s_status, 0, sizeof(s_status));
    s_status.t_set_c = SETPOINT_DEFAULT_C;

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    cfg.max_uri_handlers = 16;

    httpd_handle_t srv = NULL;
    if (httpd_start(&srv, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start fallo");
        return;
    }

    httpd_uri_t idx        = {.uri="/",             .method=HTTP_GET,     .handler=index_get};
    httpd_uri_t api        = {.uri="/api",          .method=HTTP_GET,     .handler=api_get};
    httpd_uri_t sp         = {.uri="/setpoint",     .method=HTTP_POST,    .handler=setpoint_post};
    httpd_uri_t wifi_st    = {.uri="/wifi/status",  .method=HTTP_GET,     .handler=wifi_status_get};
    httpd_uri_t wifi_set   = {.uri="/wifi",         .method=HTTP_POST,    .handler=wifi_post};
    httpd_uri_t wifi_fgt   = {.uri="/wifi/forget",  .method=HTTP_POST,    .handler=wifi_forget_post};
    httpd_uri_t triac_t    = {.uri="/triac/test",   .method=HTTP_POST,    .handler=triac_test_post};
    httpd_uri_t opt_triac  = {.uri="/triac/test",   .method=HTTP_OPTIONS, .handler=options_any};
    httpd_uri_t opt_api    = {.uri="/api",          .method=HTTP_OPTIONS, .handler=options_any};
    httpd_uri_t opt_sp     = {.uri="/setpoint",     .method=HTTP_OPTIONS, .handler=options_any};
    httpd_uri_t opt_wifi   = {.uri="/wifi",         .method=HTTP_OPTIONS, .handler=options_any};
    httpd_uri_t opt_wifi_s = {.uri="/wifi/status",  .method=HTTP_OPTIONS, .handler=options_any};
    httpd_uri_t opt_wifi_f = {.uri="/wifi/forget",  .method=HTTP_OPTIONS, .handler=options_any};

    httpd_register_uri_handler(srv, &idx);
    httpd_register_uri_handler(srv, &api);
    httpd_register_uri_handler(srv, &sp);
    httpd_register_uri_handler(srv, &wifi_st);
    httpd_register_uri_handler(srv, &wifi_set);
    httpd_register_uri_handler(srv, &wifi_fgt);
    httpd_register_uri_handler(srv, &triac_t);
    httpd_register_uri_handler(srv, &opt_api);
    httpd_register_uri_handler(srv, &opt_sp);
    httpd_register_uri_handler(srv, &opt_wifi);
    httpd_register_uri_handler(srv, &opt_wifi_s);
    httpd_register_uri_handler(srv, &opt_wifi_f);
    httpd_register_uri_handler(srv, &opt_triac);

    ESP_LOGI(TAG, "HTTP server arriba (CORS habilitado)");
}

void web_server_update_status(const ctrl_status_t *s)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status = *s;
    xSemaphoreGive(s_lock);
}

float web_server_get_setpoint(void)
{
    float v;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    v = s_setpoint_c;
    xSemaphoreGive(s_lock);
    return v;
}
