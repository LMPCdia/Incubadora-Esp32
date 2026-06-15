/*
 * wifi_ap.h - WiFi en modo Access Point
 *
 * El ESP32 levanta su propia red WiFi. El usuario se conecta con celular
 * o notebook y abre http://192.168.4.1/
 */

#ifndef WIFI_AP_H
#define WIFI_AP_H

void wifi_ap_start(void);

#endif /* WIFI_AP_H */
