/*
 * ds18b20_probe.h - Diagnostico bit-level del bus OneWire.
 *
 * Pensado para usarse UNA vez al boot. Realiza un pulso de reset
 * manual y muestrea la linea de datos durante ~300 us, imprimiendo
 * un "oscilograma" en ASCII por el monitor serie.
 *
 *   _ = linea en bajo (alguien hunde a 0)
 *   # = linea en alto (idle, pull-up)
 *
 * Lo que esperamos ver con un DS18B20 sano:
 *
 *   t=0us    release           ###############___________######...
 *                              |<-- ~15-60us  ->|<- 60-240us ->|
 *                              presence pulse del sensor
 *
 * Si la salida es toda '#' => el sensor esta MUDO (no responde).
 * Si hay un tramo '_' en algun lado => el sensor esta VIVO y entonces
 * el problema es de timing del driver.
 */

#ifndef DS18B20_PROBE_H
#define DS18B20_PROBE_H

#include "driver/gpio.h"

/* Ejecuta 3 intentos de reset + sampleo. Bloquea ~5 ms. */
void ds18b20_probe_run(gpio_num_t pin);

#endif /* DS18B20_PROBE_H */
