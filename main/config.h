/*
 * config.h - Pinout y parametros globales de la incubadora
 *
 * Rango de control: 20-45 C
 * Ley proporcional: t_dis = 10 - (T_set - T_med) / 2.5  [ms]
 *   - 0% potencia  -> t_dis = 10 ms
 *   - 100% potencia -> t_dis = 0 ms
 */

#ifndef CONFIG_H
#define CONFIG_H

#include "driver/gpio.h"

/* ===== Pines ===== */
#define PIN_ZERO_CROSS      GPIO_NUM_18   /* Entrada SFH620AA (digital) */
#define PIN_TRIAC_GATE      GPIO_NUM_4    /* Salida hacia MOC3021       */
#define PIN_DS18B20         GPIO_NUM_26   /* OneWire DS18B20            */

/* ===== Rango de temperatura ===== */
#define TEMP_MIN_C          20.0f
#define TEMP_MAX_C          50.0f
#define TEMP_RANGE_C        (TEMP_MAX_C - TEMP_MIN_C)   /* 30 C */

/* ===== Ley de control ===== */
/* K = rango / semiciclo = 30 C / 10 ms = 3.0 C/ms */
#define CONTROL_K           3.0f
#define SEMICICLO_MS        10.0f
#define TRIAC_PULSE_US      80    /* ancho del pulso de gate al MOC3021 */

/* ===== WiFi AP ===== */
#define WIFI_AP_SSID        "Incubadora-ESP32"
#define WIFI_AP_PASS        "incubadora123"   /* min 8 caracteres */
#define WIFI_AP_CHANNEL     1
#define WIFI_AP_MAX_CONN    4

/* ===== Setpoint ===== */
/* Setpoint inicial al arrancar. Se modifica unicamente desde la pagina web. */
#define SETPOINT_DEFAULT_C  37.5f   /* huevos de gallina */

#endif /* CONFIG_H */
