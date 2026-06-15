/*
 * web_server.c - HTTP server + pagina web embebida
 *
 * La pagina se sirve completa desde la flash (s_index_html).
 * Hace polling a /api cada segundo y muestra:
 *   - temperatura medida (con grafico tipo sparkline)
 *   - setpoint (slider + numero)
 *   - barra de potencia 0..100
 *   - indicador de red y sensor
 */

#include "web_server.h"
#include "config.h"

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

/* ------------ pagina HTML embebida ------------ */
static const char s_index_html[] =
"<!DOCTYPE html>"
"<html lang=\"es\"><head><meta charset=\"utf-8\">"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
"<title>Incubadora ESP32</title>"
"<style>"
"  :root{--bg:#0f1620;--card:#1b2533;--ink:#e8eef7;--mut:#7d8aa0;"
"        --ok:#3ddc97;--warn:#ff8c42;--err:#ff5470;--acc:#5aa0ff;}"
"  *{box-sizing:border-box}"
"  body{margin:0;font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;"
"       background:var(--bg);color:var(--ink);padding:16px;max-width:720px;"
"       margin:auto}"
"  h1{font-size:1.3rem;margin:0 0 16px;display:flex;align-items:center;gap:8px}"
"  .dot{width:10px;height:10px;border-radius:50%;background:var(--mut)}"
"  .dot.ok{background:var(--ok)} .dot.err{background:var(--err)}"
"  .card{background:var(--card);border-radius:14px;padding:16px;margin:12px 0;"
"        box-shadow:0 2px 8px rgba(0,0,0,.3)}"
"  .row{display:flex;justify-content:space-between;align-items:baseline;gap:8px}"
"  .big{font-size:2.6rem;font-weight:600}"
"  .unit{color:var(--mut);font-size:1rem}"
"  .label{color:var(--mut);font-size:.85rem;text-transform:uppercase;"
"         letter-spacing:.5px}"
"  .bar{height:14px;background:#0a0f17;border-radius:10px;overflow:hidden;"
"       margin-top:8px}"
"  .bar>i{display:block;height:100%;background:linear-gradient(90deg,#3ddc97,"
"        #ffd166,#ff5470);transition:width .3s}"
"  input[type=range]{width:100%}"
"  input[type=number]{width:80px;background:#0a0f17;color:var(--ink);"
"         border:1px solid #2a3548;border-radius:8px;padding:6px;font-size:1rem}"
"  button{background:var(--acc);color:#fff;border:0;border-radius:10px;"
"         padding:10px 18px;font-size:1rem;cursor:pointer}"
"  button:active{transform:translateY(1px)}"
"  svg{width:100%;height:100px;display:block}"
"  .grid{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-top:8px}"
"  .pill{background:#0a0f17;border:1px solid #2a3548;border-radius:8px;"
"        padding:6px 10px;font-size:.85rem;text-align:center}"
"</style></head><body>"
"<h1>Incubadora <span id=\"dotNet\" class=\"dot\"></span>"
"  <span id=\"dotSens\" class=\"dot\"></span></h1>"

"<div class=\"card\">"
"  <div class=\"label\">Temperatura medida</div>"
"  <div class=\"row\"><div class=\"big\"><span id=\"tmed\">--</span>"
"    <span class=\"unit\">&deg;C</span></div>"
"    <div class=\"unit\">setpoint <span id=\"tsetView\">--</span> &deg;C</div></div>"
"  <svg id=\"plot\" viewBox=\"0 0 300 100\" preserveAspectRatio=\"none\">"
"    <polyline id=\"line\" fill=\"none\" stroke=\"#5aa0ff\" stroke-width=\"2\"/>"
"  </svg>"
"</div>"

"<div class=\"card\">"
"  <div class=\"label\">Setpoint</div>"
"  <div class=\"row\" style=\"margin:8px 0\">"
"    <input type=\"range\" id=\"sl\" min=\"20\" max=\"50\" step=\"0.1\" value=\"37.5\">"
"    <input type=\"number\" id=\"nm\" min=\"20\" max=\"50\" step=\"0.1\" value=\"37.5\">"
"    <button id=\"go\">Aplicar</button>"
"  </div>"
"  <div class=\"grid\">"
"    <div class=\"pill\" onclick=\"setSp(37.0)\">Pato 37.0&deg;</div>"
"    <div class=\"pill\" onclick=\"setSp(37.5)\">Gallina 37.5&deg;</div>"
"    <div class=\"pill\" onclick=\"setSp(38.0)\">Codorniz 38.0&deg;</div>"
"    <div class=\"pill\" onclick=\"setSp(39.0)\">Canario 39.0&deg;</div>"
"  </div>"
"</div>"

"<div class=\"card\">"
"  <div class=\"label\">Potencia entregada</div>"
"  <div class=\"row\"><div class=\"big\"><span id=\"pot\">0</span>"
"    <span class=\"unit\">%</span></div>"
"    <div class=\"unit\">t<sub>dis</sub> <span id=\"td\">0</span> ms</div></div>"
"  <div class=\"bar\"><i id=\"barf\" style=\"width:0%\"></i></div>"
"</div>"

"<script>"
"const buf=[];const N=120;"
"const $=id=>document.getElementById(id);"
"function setSp(v){$(\"sl\").value=v;$(\"nm\").value=v;apply();}"
"function apply(){const v=parseFloat($(\"nm\").value);"
"  fetch('/setpoint',{method:'POST',headers:{'Content-Type':"
"  'application/x-www-form-urlencoded'},body:'t='+v});}"
"$(\"sl\").oninput=()=>$(\"nm\").value=$(\"sl\").value;"
"$(\"nm\").oninput=()=>$(\"sl\").value=$(\"nm\").value;"
"$(\"go\").onclick=apply;"
"async function refresh(){try{"
"  const r=await fetch('/api');const j=await r.json();"
"  $(\"tmed\").textContent=j.t.toFixed(2);"
"  $(\"tsetView\").textContent=j.set.toFixed(2);"
"  $(\"pot\").textContent=j.pot.toFixed(0);"
"  $(\"td\").textContent=(j.td/1000).toFixed(1);"
"  $(\"barf\").style.width=j.pot.toFixed(0)+'%';"
"  $(\"dotNet\").className='dot '+(j.net?'ok':'err');"
"  $(\"dotSens\").className='dot '+(j.sens?'ok':'err');"
"  buf.push(j.t);if(buf.length>N)buf.shift();"
"  if(buf.length>1){const min=Math.min.apply(null,buf)-0.5;"
"    const max=Math.max.apply(null,buf)+0.5;const sp=max-min||1;"
"    const pts=buf.map((v,i)=>(i*(300/(N-1)))+','+"
"      (100-(v-min)/sp*100).toFixed(1)).join(' ');"
"    $(\"line\").setAttribute('points',pts);}"
"}catch(e){}}"
"refresh();setInterval(refresh,1000);"
"</script></body></html>";

/* ------------ Handlers HTTP ------------ */

static esp_err_t index_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, s_index_html, HTTPD_RESP_USE_STRLEN);
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

    httpd_resp_set_type(req, "application/json");
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
    return httpd_resp_send(req, "OK", 2);
}

/* ------------ Inicializacion ------------ */

void web_server_start(void)
{
    s_lock = xSemaphoreCreateMutex();
    memset(&s_status, 0, sizeof(s_status));
    s_status.t_set_c = SETPOINT_DEFAULT_C;

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    cfg.max_uri_handlers = 8;

    httpd_handle_t srv = NULL;
    if (httpd_start(&srv, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start fallo");
        return;
    }

    httpd_uri_t idx  = {.uri="/",         .method=HTTP_GET,  .handler=index_get};
    httpd_uri_t api  = {.uri="/api",      .method=HTTP_GET,  .handler=api_get};
    httpd_uri_t sp   = {.uri="/setpoint", .method=HTTP_POST, .handler=setpoint_post};
    httpd_register_uri_handler(srv, &idx);
    httpd_register_uri_handler(srv, &api);
    httpd_register_uri_handler(srv, &sp);

    ESP_LOGI(TAG, "HTTP server arriba");
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
