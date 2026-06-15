/*
 * ds18b20_probe.c - Implementacion del diagnostico bit-level.
 *
 * Estrategia:
 *   1) Configura el pin como INPUT_OUTPUT_OD con pull-up.
 *   2) Lo deja en alto 1 ms para asegurar idle limpio.
 *   3) Hunde la linea 480 us (reset OneWire estandar).
 *   4) Libera la linea y sampleo el nivel cada SAMPLE_STEP_US
 *      durante SAMPLE_TOTAL_US (60 muestras x 5 us = 300 us).
 *   5) Imprime un string ASCII con el resultado.
 *   6) Repite 3 veces para detectar inconsistencias.
 *
 * Todo el reset + sampleo va dentro de un portENTER_CRITICAL para
 * que el timing no sea perturbado por ISRs (WiFi, timers, etc).
 */

#include "ds18b20_probe.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "rom/ets_sys.h"
#include "esp_log.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "probe";

/* Resolucion del sampleo. 5 us es suficiente para distinguir un pulso
 * de presencia (60-240 us) y no satura la salida. */
#define SAMPLE_STEP_US     5
#define SAMPLE_TOTAL_US    300
#define SAMPLE_COUNT       (SAMPLE_TOTAL_US / SAMPLE_STEP_US)   /* 60 */

#define PROBE_ATTEMPTS     3

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

/* Ejecuta UN reset + sampleo con el reset_us indicado y vuelca los niveles.
 * Probamos con varios reset para detectar sensores fuera de spec. */
static void single_probe(gpio_num_t pin, uint32_t reset_us,
                         uint8_t samples[SAMPLE_COUNT])
{
    portENTER_CRITICAL(&s_mux);

    /* Asegurar idle alto antes del reset */
    gpio_set_level(pin, 1);
    ets_delay_us(100);

    /* Pulso de reset: hundir la linea reset_us */
    gpio_set_level(pin, 0);
    ets_delay_us(reset_us);

    /* Liberar la linea -- desde aqui empieza la ventana de presencia */
    gpio_set_level(pin, 1);

    /* Sampleo en tight loop. */
    for (int i = 0; i < SAMPLE_COUNT; i++) {
        samples[i] = (uint8_t)gpio_get_level(pin);
        ets_delay_us(SAMPLE_STEP_US);
    }

    /* Recovery */
    ets_delay_us(500);

    portEXIT_CRITICAL(&s_mux);
}

/* Imprime el "oscilograma" ASCII de samples[] y un resumen */
static void print_result(int attempt, const uint8_t samples[SAMPLE_COUNT])
{
    char line[SAMPLE_COUNT + 1];
    int  low_count = 0;
    int  first_low = -1;
    int  last_low  = -1;

    for (int i = 0; i < SAMPLE_COUNT; i++) {
        if (samples[i]) {
            line[i] = '#';
        } else {
            line[i] = '_';
            low_count++;
            if (first_low < 0) first_low = i;
            last_low = i;
        }
    }
    line[SAMPLE_COUNT] = '\0';

    printf("  [%d] %s\n", attempt, line);

    if (low_count == 0) {
        printf("       => MUDO: nadie hundio la linea (sensor desconectado o muerto)\n");
    } else {
        int t_start_us = first_low * SAMPLE_STEP_US;
        int t_end_us   = (last_low + 1) * SAMPLE_STEP_US;
        int width_us   = t_end_us - t_start_us;
        printf("       => VIVO: pulso de presencia a t=%d us, ancho ~%d us\n",
               t_start_us, width_us);

        /* Validacion contra spec del DS18B20:
         *   - presence debe arrancar entre 15 y 60 us tras la release
         *   - debe durar entre 60 y 240 us */
        bool t_ok = (t_start_us >= 10 && t_start_us <= 80);
        bool w_ok = (width_us  >= 50 && width_us  <= 260);
        if (t_ok && w_ok) {
            printf("       => Timing OK segun spec del DS18B20.\n");
        } else {
            printf("       => Timing FUERA DE SPEC (t_start=%d us, ancho=%d us)\n",
                   t_start_us, width_us);
        }
    }
}

void ds18b20_probe_run(gpio_num_t pin)
{
    ESP_LOGI(TAG, "----------------------------------------------------------");
    ESP_LOGI(TAG, "Probe bit-level OneWire en GPIO %d", pin);
    ESP_LOGI(TAG, "  Cada caracter = %d us. Total %d us por intento.",
             SAMPLE_STEP_US, SAMPLE_TOTAL_US);
    ESP_LOGI(TAG, "  '#' = alto (idle), '_' = bajo (alguien hunde la linea)");

    /* Configuracion del pin igual que el driver real */
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level(pin, 1);
    vTaskDelay(pdMS_TO_TICKS(1));     /* settle */

    /* Antes que nada, sampleo el idle (sin reset) para verificar pull-up */
    uint8_t idle[SAMPLE_COUNT];
    portENTER_CRITICAL(&s_mux);
    for (int i = 0; i < SAMPLE_COUNT; i++) {
        idle[i] = (uint8_t)gpio_get_level(pin);
        ets_delay_us(SAMPLE_STEP_US);
    }
    portEXIT_CRITICAL(&s_mux);

    int idle_high = 0;
    for (int i = 0; i < SAMPLE_COUNT; i++) if (idle[i]) idle_high++;
    if (idle_high == SAMPLE_COUNT) {
        printf("  IDLE: linea siempre en alto (pull-up funcionando).\n");
    } else {
        printf("  IDLE: %d/%d muestras en alto -> linea inestable o sin pull-up!\n",
               idle_high, SAMPLE_COUNT);
    }

    /* Probamos con tres duraciones de reset distintas. La spec dice
     * 480 us minimo, pero algunos clones (o sensores con cable largo)
     * responden mejor con reset mas largo. */
    const uint32_t reset_lengths_us[] = { 500, 1000, 5000 };
    const int n_lengths = sizeof(reset_lengths_us) / sizeof(reset_lengths_us[0]);

    for (int li = 0; li < n_lengths; li++) {
        uint32_t r = reset_lengths_us[li];
        printf("  ---- Reset = %u us ----\n", (unsigned)r);
        for (int n = 0; n < PROBE_ATTEMPTS; n++) {
            uint8_t samples[SAMPLE_COUNT];
            single_probe(pin, r, samples);
            print_result(n + 1, samples);
            vTaskDelay(pdMS_TO_TICKS(50));    /* deja descansar al sensor */
        }
    }

    ESP_LOGI(TAG, "----------------------------------------------------------");
}
