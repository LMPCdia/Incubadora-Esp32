/*
 * triac_ctrl.c - Control TRIAC por angulo de fase con paso por cero
 *
 * Cadena de eventos por semiciclo:
 *   1. SFH620AA detecta cruce por cero -> flanco en PIN_ZERO_CROSS
 *   2. ISR arma esp_timer one-shot con t_dis_us
 *   3. Al vencer, callback pone PIN_TRIAC_GATE en alto
 *   4. Se arma un segundo timer que apaga el gate tras TRIAC_PULSE_US
 *
 * Si t_dis_us == 0      -> dispara casi inmediatamente (100% pot.)
 * Si t_dis_us >= 10000  -> no se arma el timer (0% pot.)
 */

#include "triac_ctrl.h"
#include "config.h"

#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <inttypes.h>

static const char *TAG = "triac";

static esp_timer_handle_t s_fire_timer  = NULL;  /* one-shot: dispara gate */
static esp_timer_handle_t s_pulse_timer = NULL;  /* one-shot: apaga gate  */
static esp_timer_handle_t s_test_timer  = NULL;  /* one-shot: termina modo test */

static volatile uint32_t s_delay_us        = 10000; /* arranca apagado */
static volatile uint32_t s_zc_count        = 0;
static volatile int64_t  s_last_zc_time_us = 0;
static volatile bool     s_test_mode       = false;

/* -------- Callbacks de timers (corren en task de esp_timer) -------- */

static void IRAM_ATTR pulse_off_cb(void *arg)
{
    /* No apagar el gate si estamos en modo test (mantenerlo HIGH) */
    if (s_test_mode) return;
    gpio_set_level(PIN_TRIAC_GATE, 0);
}

static void IRAM_ATTR fire_cb(void *arg)
{
    if (s_test_mode) return;          /* en test modo no hacemos pulsos */
    gpio_set_level(PIN_TRIAC_GATE, 1);
    /* Programa el apagado del pulso */
    esp_timer_start_once(s_pulse_timer, TRIAC_PULSE_US);
}

/* Callback del timer que termina el modo test: apaga gate y vuelve al
 * control normal. */
static void test_end_cb(void *arg)
{
    s_test_mode = false;
    gpio_set_level(PIN_TRIAC_GATE, 0);
    ESP_LOGI(TAG, "Modo TEST terminado - gate vuelve al control");
}

/* -------- ISR de paso por cero -------- */

static void IRAM_ATTR zero_cross_isr(void *arg)
{
    s_zc_count++;
    s_last_zc_time_us = esp_timer_get_time();

    uint32_t d = s_delay_us;
    if (d >= 10000) {
        /* 0% potencia: no disparamos en este semiciclo */
        return;
    }
    /* Cancelamos cualquier timer pendiente por si llego un ZC con uno activo
     * (ruido, glitch). */
    esp_timer_stop(s_fire_timer);
    if (d < 50) {
        d = 50;  /* esp_timer no acepta delays muy chicos */
    }
    esp_timer_start_once(s_fire_timer, d);
}

/* -------- API publica -------- */

void triac_init(void)
{
    /* Gate del TRIAC como salida, arranca en 0 */
    gpio_config_t gate_cfg = {
        .pin_bit_mask = 1ULL << PIN_TRIAC_GATE,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&gate_cfg);
    gpio_set_level(PIN_TRIAC_GATE, 0);

    /* Entrada de paso por cero - flanco de subida */
    gpio_config_t zc_cfg = {
        .pin_bit_mask = 1ULL << PIN_ZERO_CROSS,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_POSEDGE,
    };
    gpio_config(&zc_cfg);

    /* Timers one-shot */
    const esp_timer_create_args_t fire_args = {
        .callback = &fire_cb,
        .name = "triac_fire",
        .dispatch_method = ESP_TIMER_TASK,
    };
    const esp_timer_create_args_t pulse_args = {
        .callback = &pulse_off_cb,
        .name = "triac_pulse",
        .dispatch_method = ESP_TIMER_TASK,
    };
    const esp_timer_create_args_t test_args = {
        .callback = &test_end_cb,
        .name = "triac_test",
        .dispatch_method = ESP_TIMER_TASK,
    };
    ESP_ERROR_CHECK(esp_timer_create(&fire_args,  &s_fire_timer));
    ESP_ERROR_CHECK(esp_timer_create(&pulse_args, &s_pulse_timer));
    ESP_ERROR_CHECK(esp_timer_create(&test_args,  &s_test_timer));

    /* ISR service + handler */
    gpio_install_isr_service(0);
    gpio_isr_handler_add(PIN_ZERO_CROSS, zero_cross_isr, NULL);

    ESP_LOGI(TAG, "TRIAC listo: ZC=GPIO%d  GATE=GPIO%d",
             PIN_ZERO_CROSS, PIN_TRIAC_GATE);
}

void triac_set_delay_us(uint32_t us)
{
    if (us > 10000) us = 10000;
    s_delay_us = us;
}

uint32_t triac_get_delay_us(void)
{
    return s_delay_us;
}

bool triac_zc_alive(void)
{
    int64_t now = esp_timer_get_time();
    /* Si paso mas de 100 ms sin cruce, se considera red caida */
    return (now - s_last_zc_time_us) < 100000;
}

uint32_t triac_zc_count(void)
{
    return s_zc_count;
}

void triac_force_on(uint32_t test_ms)
{
    /* Cancelar timers en curso para que no se pisen */
    esp_timer_stop(s_fire_timer);
    esp_timer_stop(s_pulse_timer);
    esp_timer_stop(s_test_timer);

    if (test_ms == 0) {
        /* Apagar manualmente */
        s_test_mode = false;
        gpio_set_level(PIN_TRIAC_GATE, 0);
        ESP_LOGI(TAG, "Modo TEST cancelado");
        return;
    }

    s_test_mode = true;
    gpio_set_level(PIN_TRIAC_GATE, 1);
    esp_timer_start_once(s_test_timer, (uint64_t)test_ms * 1000ULL);
    ESP_LOGW(TAG, "Modo TEST: gate forzado HIGH por %u ms", (unsigned)test_ms);
}

bool triac_in_test_mode(void)
{
    return s_test_mode;
}
