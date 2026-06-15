/*
 * ds18b20.h - Driver del sensor DS18B20 sobre la libreria ONEWire
 *
 * Modo de uso (un unico sensor en el bus, sin ROM-search):
 *   ds18b20_init(PIN_DS18B20);
 *   ds18b20_start_conversion();         // dispara la conversion
 *   vTaskDelay(pdMS_TO_TICKS(800));     // 12 bits => ~750 ms
 *   float t = ds18b20_read_temp();      // NAN si hay error
 */

#ifndef DS18B20_H
#define DS18B20_H

#include "driver/gpio.h"
#include <stdbool.h>

/* Inicializa el bus OneWire en el pin indicado. Devuelve true si el
 * sensor responde al pulso de reset. */
bool ds18b20_init(gpio_num_t pin);

/* Envia Skip-ROM + ConvertT. La conversion tarda hasta 750 ms a 12 bits. */
bool ds18b20_start_conversion(void);

/* Lee el scratchpad y devuelve la temperatura en grados Celsius.
 * Devuelve NAN si la lectura falla. */
float ds18b20_read_temp(void);

#endif /* DS18B20_H */
