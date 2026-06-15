/*
 * wifi_mgr.h - Manager de WiFi en modo APSTA con provisioning via NVS.
 *
 * Flujo:
 *   1) Boot: arranca AP (siempre activo) + STA si hay credenciales en NVS.
 *   2) Si no hay credenciales, el AP queda accesible y el usuario abre la
 *      pagina, ingresa SSID/pass desde el modal de settings y POST /wifi
 *      guarda en NVS y aplica al toque (sin necesidad de reset).
 *   3) Si la conexion STA falla varias veces, queda en STA_FAILED pero
 *      el AP sigue funcionando para reconfigurar.
 *
 * Nota: en modo APSTA el ESP32 es accesible por DOS IPs simultaneamente:
 *   - 192.168.4.1            (AP, ssid "Incubadora-ESP32")
 *   - IP_que_le_dio_el_router (STA, alcanzable desde la LAN)
 */

#ifndef WIFI_MGR_H
#define WIFI_MGR_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    WIFI_MGR_AP_ONLY,           /* sin credenciales o STA aun no inicia */
    WIFI_MGR_STA_CONNECTING,    /* intentando conectarse */
    WIFI_MGR_STA_CONNECTED,     /* conectado con IP */
    WIFI_MGR_STA_FAILED,        /* falla tras N reintentos */
} wifi_mgr_state_t;

/* Inicializa NVS + netif + WiFi en modo APSTA. */
void wifi_mgr_start(void);

/* Guarda credenciales en NVS y aplica al toque (sin reboot).
 * Devuelve false si los argumentos son invalidos o NVS falla. */
bool wifi_mgr_set_credentials(const char *ssid, const char *pass);

/* Borra credenciales y vuelve a AP-only. */
void wifi_mgr_clear_credentials(void);

/* Estado actual del cliente STA. */
wifi_mgr_state_t wifi_mgr_get_state(void);

/* Info de la conexion STA (todos los out son opcionales -> NULL si no interesa).
 *   ssid_out    : SSID configurado (cadena vacia si no hay)
 *   ip_out      : "192.168.x.x" si conectado, "" si no
 *   rssi_out    : nivel de senal en dBm (0 si no conectado)
 */
void wifi_mgr_get_info(char *ssid_out, size_t ssid_len,
                       char *ip_out,   size_t ip_len,
                       int  *rssi_out);

#endif /* WIFI_MGR_H */
