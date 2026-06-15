/*
 * ds18b20.c - Driver del DS18B20 sobre la libreria ONEWire.
 *
 * Cambios clave respecto a la version anterior:
 *  1) El pin se configura UNA sola vez como INPUT_OUTPUT_OD y los
 *     callbacks de la libreria solo togglean el nivel (no cambian
 *     direccion en cada bit, que era demasiado lento para OneWire).
 *  2) Las secuencias bit-banged se ejecutan dentro de una seccion
 *     critica (portENTER_CRITICAL) para que las ISRs del WiFi/SoftAP
 *     no estiren los retardos en microsegundos y se pierda el pulso
 *     de presencia del DS18B20 (ventana de 60-240 us tras el reset).
 *  3) Validacion CRC8 del scratchpad antes de aceptar la temperatura,
 *     evitando lecturas espureas cuando la linea tiene ruido.
 *
 * NOTA: las funciones publicas siguen siendo bloqueantes. Un reset
 * dura ~1 ms, una transaccion completa de lectura (~9 bytes) toma
 * unos ~8 ms. La seccion critica solo afecta al core donde corre la
 * tarea que invoca al driver -- por eso conviene pinear la tarea del
 * sensor al core 1 (APP_CPU), dejando el core 0 (PRO_CPU) libre para
 * el stack WiFi.
 */

#include "ds18b20.h"
#include "ONEWire.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "rom/ets_sys.h"
#include "esp_log.h"

#include <math.h>
#include <string.h>

static const char *TAG = "ds18b20";

/* Comandos del DS18B20 (datasheet, Maxim) */
#define DS18B20_CMD_SKIP_ROM     0xCC   /* salta direccionamiento ROM (1 unico sensor) */
#define DS18B20_CMD_CONVERT_T    0x44   /* inicia conversion de temperatura */
#define DS18B20_CMD_READ_SCRATCH 0xBE   /* lee los 9 bytes del scratchpad */

/* Handle global del bus OneWire para este driver (un solo sensor). */
static _sOWHandle s_ow;
static gpio_num_t s_pin = GPIO_NUM_NC;

/* Mutex de seccion critica. portMUX_TYPE bloquea las interrupciones de
 * un core (donde se invoca) y sincroniza el acceso multi-core. Es lo
 * que necesitamos para que ets_delay_us no se estire por una ISR. */
static portMUX_TYPE s_ow_mux = portMUX_INITIALIZER_UNLOCKED;

/* ============================================================
 * Callbacks que la libreria ONEWire usa para manipular el pin.
 * El pin esta en GPIO_MODE_INPUT_OUTPUT_OD permanente:
 *   nivel 0 -> drive low  (ESP32 hunde la linea)
 *   nivel 1 -> high-Z     (pull-up tira a alto, la linea queda libre)
 * ============================================================ */

static void ow_set_input(void)
{
    /* "Set input" en open-drain = liberar la linea */
    gpio_set_level(s_pin, 1);
}

static void ow_set_output(void)
{
    /* No-op: el pin ya esta en INPUT_OUTPUT_OD desde ds18b20_init.
     * Cambiar la direccion en cada bit (como hacia la version vieja)
     * mete una latencia de decenas de us que rompe la ventana de
     * sample de OneWire. */
}

static void ow_write_bit(uint8_t value)
{
    gpio_set_level(s_pin, value ? 1 : 0);
}

static uint8_t ow_read_bit(void)
{
    /* En INPUT_OUTPUT_OD el input buffer queda habilitado siempre,
     * asi que gpio_get_level lee el nivel real de la linea. */
    return (uint8_t)gpio_get_level(s_pin);
}

static int ow_delay_us(int us)
{
    if (us > 0) {
        ets_delay_us((uint32_t)us);
    }
    return us;
}

/* ============================================================
 * CRC8 - X^8 + X^5 + X^4 + 1, polinomio Dallas/Maxim.
 * Usado para validar los 9 bytes del scratchpad del DS18B20.
 * (El ultimo byte es el CRC sobre los 8 anteriores.)
 * ============================================================ */
static uint8_t ds_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        uint8_t b = data[i];
        for (uint8_t j = 0; j < 8; j++) {
            uint8_t mix = (crc ^ b) & 0x01;
            crc >>= 1;
            if (mix) crc ^= 0x8C;
            b >>= 1;
        }
    }
    return crc;
}

/* ============================================================
 * API publica
 * ============================================================ */

bool ds18b20_init(gpio_num_t pin)
{
    s_pin = pin;

    /* Configuracion del pin una unica vez:
     *  - INPUT_OUTPUT_OD: open-drain con input buffer habilitado.
     *  - Pull-up interno como respaldo (el real es el de 4.7k externo).
     *  - Sin interrupciones de GPIO.
     */
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level(pin, 1);          /* idle = linea liberada (alto) */

    /* Asociamos los callbacks de bajo nivel al handle de la libreria */
    memset(&s_ow, 0, sizeof(s_ow));
    s_ow.SETPinInput  = ow_set_input;
    s_ow.SETPinOutput = ow_set_output;
    s_ow.WritePinBit  = ow_write_bit;
    s_ow.ReadPinBit   = ow_read_bit;
    s_ow.DELAYus      = ow_delay_us;

    ONEWire_Init(&s_ow);

    /* Pulso de reset bajo seccion critica: si el SoftAP esta corriendo
     * cualquier ISR estira los retardos. */
    bool present = false;
    portENTER_CRITICAL(&s_ow_mux);
    _eONEWIREStatus st = ONEWireReset(&s_ow);
    if (st == ONEWIRE_ST_OK) {
        /* La libreria almacena el nivel leido tras el reset:
         *   isPresent == 0 -> habia presencia (linea fue a 0)
         *   isPresent == 1 -> nadie respondio */
        present = (s_ow.taskData.flags.bit.isPresent == 0);
    }
    portEXIT_CRITICAL(&s_ow_mux);

    if (!present) {
        ESP_LOGW(TAG, "DS18B20 no responde al reset (GPIO %d)", pin);
    } else {
        ESP_LOGI(TAG, "DS18B20 detectado en GPIO %d", pin);
    }
    return present;
}

bool ds18b20_start_conversion(void)
{
    /* Toda la transaccion (reset + 2 bytes) bajo seccion critica.
     * Duracion: ~480 us reset + ~80 us presence + ~400 us recovery +
     * 2 bytes * ~520 us = ~2 ms. Aceptable para bloquear ISRs.
     */
    bool ok = false;
    portENTER_CRITICAL(&s_ow_mux);
    if (ONEWireReset(&s_ow) == ONEWIRE_ST_OK &&
        s_ow.taskData.flags.bit.isPresent == 0) {
        ONEWireWriteByte(&s_ow, DS18B20_CMD_SKIP_ROM);
        ONEWireWriteByte(&s_ow, DS18B20_CMD_CONVERT_T);
        ok = true;
    }
    portEXIT_CRITICAL(&s_ow_mux);
    return ok;
}

float ds18b20_read_temp(void)
{
    uint8_t scratch[9] = {0};
    bool present = false;

    /* Reset + comandos + 9 bytes del scratchpad bajo seccion critica.
     * Duracion total ~7-8 ms. */
    portENTER_CRITICAL(&s_ow_mux);
    if (ONEWireReset(&s_ow) == ONEWIRE_ST_OK &&
        s_ow.taskData.flags.bit.isPresent == 0) {
        ONEWireWriteByte(&s_ow, DS18B20_CMD_SKIP_ROM);
        ONEWireWriteByte(&s_ow, DS18B20_CMD_READ_SCRATCH);
        for (int i = 0; i < 9; i++) {
            ONEWireReadByte(&s_ow, &scratch[i]);
        }
        present = true;
    }
    portEXIT_CRITICAL(&s_ow_mux);

    if (!present) {
        ESP_LOGW(TAG, "read_temp: sin presencia");
        return NAN;
    }

    /* Validar CRC: si el ultimo byte no coincide, hubo ruido en la linea. */
    uint8_t crc = ds_crc8(scratch, 8);
    if (crc != scratch[8]) {
        ESP_LOGW(TAG, "read_temp: CRC malo (calc=0x%02X, leido=0x%02X)",
                 crc, scratch[8]);
        return NAN;
    }

    int16_t raw = (int16_t)((scratch[1] << 8) | scratch[0]);

    /* Lecturas extremas tipicas de bus desconectado */
    if (raw == (int16_t)0xFFFF || raw == 0) return NAN;

    return raw / 16.0f;
}
