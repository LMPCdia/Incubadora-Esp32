/*
 * ========================================================================
 *  Fecha hora y cambios realizados
 * ========================================================================
 *
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *  *** BRANCH: feat/bangbang-test (NO MERGEAR A MAIN sin revisar) ***
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *  2026-06-22 (rev 2) - Control proporcional con Slow-PWM
 *
 *  Reemplaza el bang-bang por control proporcional sin necesidad de ZC.
 *  Usa Slow-PWM sobre el gate del TRIAC:
 *    - ventana = 5 s
 *    - duty = error_C * 25%/C, recortado [0, 100%]
 *    - loop a 4 Hz para resolucion fina del PWM (5%)
 *    - zona muerta de 0.2 C para evitar oscilacion al setpoint
 *  Sigue saltando ZC (usa triac_force_on/0).
 *
 *  Cambios respecto a rev 1 (bang-bang):
 *    - control_task: bang-bang -> Slow-PWM proporcional.
 *    - Constantes: CTRL_KP_PERCENT_PER_C, CTRL_PWM_PERIOD_MS,
 *      CTRL_LOOP_MS, CTRL_DEADBAND_C, CTRL_KEEPALIVE_MS.
 *    - Log marcado con [P-PWM] una vez por segundo (aunque el loop es 4 Hz).
 *
 *  Limitacion conocida: control P puro tiene offset en regimen
 *  permanente. Si la T final queda > 0.5 C debajo del setpoint,
 *  agregar termino integral (PI) con anti-windup.
 *
 *  2026-06-22 (rev 1) - Modo bang-bang con histeresis (test sin ZC)
 *  - Reemplazo del control proporcional por ON/OFF con histeresis de 4 C.
 *  - Sirvio para confirmar que el lado de potencia (MOC3021 + TRIAC +
 *    calefactor) anda bien y que el problema esta solo en el ZC.
 *
 *  Para volver al control de fase original:  git checkout main
 *
 *  -------------------------------------------------------------------
 *  2026-06-15 - WiFi provisioning APSTA + UI rediseñada + Vercel
 *  -------------------------------------------------------------------
 *  Motivacion: poder controlar la incubadora desde una pagina web
 *  hosteada en Vercel (acceso por internet) y dejar de depender del
 *  SoftAP fijo para uso normal.
 *
 *  Cambios:
 *   - wifi_ap.c/h reemplazado por wifi_mgr.c/h:
 *       * Modo APSTA: SoftAP siempre activo (192.168.4.1) + cliente STA
 *         si hay credenciales guardadas en NVS (namespace "wifi_creds").
 *       * Si STA no se conecta tras 6 reintentos pasa a STA_FAILED pero
 *         el AP queda disponible para reconfigurar.
 *       * Aplicacion de credenciales nuevas SIN reboot (set_config +
 *         esp_wifi_connect en caliente).
 *
 *   - web_server.c:
 *       * 3 endpoints nuevos:
 *           GET  /wifi/status  -> JSON {state, ssid, ip, rssi}
 *           POST /wifi         -> body ssid=X&pass=Y, guarda en NVS
 *           POST /wifi/forget  -> borra credenciales
 *       * CORS (Access-Control-Allow-Origin: *) en /api y /setpoint
 *         para que la UI de Vercel pueda hacer fetch cross-origin.
 *       * Handlers OPTIONS para preflight.
 *       * La pagina HTML se embebe via EMBED_FILES desde ../web/index.html
 *         (single source of truth: el mismo archivo va al ESP32 y a Vercel).
 *
 *   - web/index.html (nueva UI):
 *       * Rediseño completo: tematica incubadora microbiologica,
 *         glassmorphism, color de fondo que muta segun (Tmed - Tset)
 *         (azul=frio, verde=en setpoint, naranja/rojo=sobre-temp).
 *       * Presets: Ambiente 25, Levaduras 30, E.coli 37, Termofilos 42.
 *       * Modal de settings con dos secciones:
 *           (1) URL base del ESP32 (para uso desde Vercel).
 *           (2) Provisioning WiFi: SSID + pass + status en vivo.
 *       * Modo demo automatico si /api no es alcanzable (datos simulados).
 *
 *   - main/CMakeLists.txt:
 *       * EMBED_FILES "${CMAKE_CURRENT_SOURCE_DIR}/../web/index.html"
 *       * SRCS: wifi_ap.c -> wifi_mgr.c
 *
 *  -------------------------------------------------------------------
 *  2026-06-14 - Refactor a arquitectura multi-tarea + fix DS18B20
 *  -------------------------------------------------------------------
 *  Motivacion: el DS18B20 retornaba constantemente "no responde al reset".
 *  El analisis mostro dos problemas combinados:
 *    (a) El driver hacia gpio_set_direction() en cada bit. Cada cambio de
 *        direccion en ESP-IDF cuesta decenas de microsegundos -> se perdia
 *        la ventana de sample del pulso de presencia.
 *    (b) Todo corria en una sola task junto con WiFi+HTTP en el mismo core.
 *        Las ISRs del SoftAP estiraban los retardos us del bit-banging.
 *
 *  Cambios:
 *   - ds18b20.c:
 *       * Pin permanente en INPUT_OUTPUT_OD; los callbacks solo togglean
 *         nivel (drive 0 / release 1). No mas gpio_set_direction por bit.
 *       * Toda transaccion OneWire envuelta en portENTER_CRITICAL para que
 *         las ISRs de WiFi no estiren el timing.
 *       * Validacion CRC8 del scratchpad antes de aceptar la temperatura.
 *
 *   - ds18b20_probe.c/h (nuevo): diagnostico bit-level del bus OneWire
 *     para debugging del sensor. Imprime un "oscilograma" ASCII de los
 *     niveles muestreados despues del pulso de reset.
 *
 *   - main.c (este archivo): separacion de responsabilidades en tareas
 *     FreeRTOS independientes:
 *         sensor_task    - lee el DS18B20 a 1 Hz (core 1, prio 6)
 *         control_task   - aplica ley proporcional + UI (core 1, prio 5)
 *         heartbeat_task - parpadeo de LED segun estado (core 0, prio 1)
 *         triac_ctrl     - ISR de zero-cross + esp_timer (en IRAM)
 *         httpd          - pagina web (task interna de esp_http_server)
 *
 *   - Estado compartido (temperatura medida + flags) protegido por mutex.
 *     Cada task accede via getters/setters thread-safe.
 *
 *   - Tareas WiFi/httpd quedan en core 0 (PRO_CPU) por defecto; las tareas
 *     de control y sensor van pineadas a core 1 (APP_CPU) para minimizar
 *     contencion de interrupciones con el stack WiFi.
 * ========================================================================
 */

/*
 * main.c - Incubadora con control proporcional por TRIAC + ESP32
 *
 * Pipeline de datos:
 *
 *      [DS18B20] --(OneWire)--> sensor_task --(mutex)--> shared_state
 *                                                              |
 *                                                              v
 *      [Web slider] -> httpd ---(web_server_get_setpoint)--> control_task
 *                                                              |
 *                                              t_dis_us calculado
 *                                                              |
 *                                                              v
 *      [Red 220 VAC] -> ZC ISR -> esp_timer --(t_dis)--> [Gate TRIAC]
 *                                                              |
 *                                                              v
 *                                                       [Calefactor 100W]
 *
 * El loop de control corre a 1 Hz, suficiente porque la inercia termica
 * de la incubadora es del orden de minutos. El sensor tambien actualiza
 * a 1 Hz (la conversion a 12 bits del DS18B20 ya toma ~750 ms).
 */

#include "config.h"
#include "ds18b20.h"
#include "ds18b20_probe.h"
#include "triac_ctrl.h"
#include "wifi_mgr.h"
#include "web_server.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <math.h>

static const char *TAG = "main";

/* LED on-board del NodeMCU ESP32 (heartbeat de estado del sistema) */
#define PIN_LED_HEARTBEAT       GPIO_NUM_2

/* Cores: en ESP32, PRO_CPU=0 (WiFi/BT por default) y APP_CPU=1.
 * Mantenemos sensor y control en core 1 para no pelear contra WiFi. */
#define CORE_CONTROL            1
#define CORE_HEARTBEAT          0

/* Prioridades de las tareas (a mayor numero, mayor prioridad en FreeRTOS) */
#define PRIO_SENSOR             6
#define PRIO_CONTROL            5
#define PRIO_HEARTBEAT          1

/* Tamanos de pila (bytes). Conservadores para que no haya stack-overflow. */
#define STACK_SENSOR            4096
#define STACK_CONTROL           4096
#define STACK_HEARTBEAT         2048

/* ============================================================
 * Estado compartido entre tasks
 * ------------------------------------------------------------
 * Cualquier campo accedido por mas de una tarea se lee/escribe
 * tomando s_mutex. La idea es mantener este bloque chico y
 * centralizar el locking.
 * ============================================================ */
typedef struct {
    float    t_med_c;       /* ultima temperatura valida (NAN si nunca leyo) */
    uint32_t t_med_age_ms;  /* edad de la lectura (ms desde el ultimo update) */
    bool     sensor_ok;     /* el sensor respondio en la ultima lectura */
} shared_state_t;

static shared_state_t s_state = {
    .t_med_c      = NAN,
    .t_med_age_ms = 0,
    .sensor_ok    = false,
};
static SemaphoreHandle_t s_mutex = NULL;

/* Marca temporal de la ultima lectura, en ticks. Se actualiza solo desde
 * sensor_task; otras tareas la pueden leer (es uint32_t atomico en lectura). */
static volatile TickType_t s_last_sensor_tick = 0;

/* Setter llamado desde sensor_task */
static void state_set_temp(float t, bool ok)
{
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        s_state.t_med_c   = t;
        s_state.sensor_ok = ok;
        xSemaphoreGive(s_mutex);
    }
    s_last_sensor_tick = xTaskGetTickCount();
}

/* Getter usado por control_task y heartbeat_task */
static void state_get(float *t_out, bool *ok_out, uint32_t *age_ms_out)
{
    float t = NAN;
    bool  ok = false;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        t  = s_state.t_med_c;
        ok = s_state.sensor_ok;
        xSemaphoreGive(s_mutex);
    }
    if (t_out)  *t_out  = t;
    if (ok_out) *ok_out = ok;
    if (age_ms_out) {
        TickType_t now = xTaskGetTickCount();
        *age_ms_out = (uint32_t)((now - s_last_sensor_tick) * portTICK_PERIOD_MS);
    }
}

/* ============================================================
 * Task 1: SENSOR
 * ------------------------------------------------------------
 * Unica duena del bus OneWire. Cada 1 s:
 *   1. Dispara conversion (Skip-ROM + ConvertT)
 *   2. Espera 800 ms (la conv. a 12 bits toma 750 ms maximo)
 *   3. Lee el scratchpad y valida CRC
 *   4. Publica el resultado en shared_state
 *
 * Si el sensor falla, intenta re-inicializarlo. Mientras no haya
 * lectura valida, t_med queda en NAN y control_task aplica el
 * fail-safe (potencia 0%).
 * ============================================================ */
static void sensor_task(void *arg)
{
    ESP_LOGI(TAG, "sensor_task arrancando (core %d)", xPortGetCoreID());

    bool sensor_ok = ds18b20_init(PIN_DS18B20);
    int  fail_count = 0;

    while (1) {
        /* Si no hay sensor, reintenta cada 2 s */
        if (!sensor_ok) {
            fail_count++;

            /* Cada 5 fallos consecutivos corremos el probe bit-level.
             * Asi siempre se ve en el monitor, no solo al boot. */
            if (fail_count % 5 == 1) {
                ds18b20_probe_run(PIN_DS18B20);
            }

            ESP_LOGW(TAG, "sensor_task: reintentando init... (fallo #%d)",
                     fail_count);
            sensor_ok = ds18b20_init(PIN_DS18B20);
            if (!sensor_ok) {
                state_set_temp(NAN, false);
                vTaskDelay(pdMS_TO_TICKS(2000));
                continue;
            }
            fail_count = 0;
        }

        /* 1) Disparar conversion */
        if (!ds18b20_start_conversion()) {
            ESP_LOGW(TAG, "sensor_task: start_conversion fallo");
            sensor_ok = false;
            state_set_temp(NAN, false);
            continue;
        }

        /* 2) Esperar a que el sensor termine. Durante este delay el bus
         *    queda en alto (idle); no tocamos nada. */
        vTaskDelay(pdMS_TO_TICKS(800));

        /* 3) Leer */
        float t = ds18b20_read_temp();
        if (isnan(t)) {
            ESP_LOGW(TAG, "sensor_task: read_temp fallo (NAN)");
            sensor_ok = false;
            state_set_temp(NAN, false);
            /* el continue arriba reintentara init en la proxima iteracion */
            continue;
        }

        /* 4) Publicar */
        state_set_temp(t, true);
        ESP_LOGD(TAG, "sensor_task: T=%.2f C", t);

        /* Periodo total ~1 s: 800 ms de conversion + ~10 ms lectura + 200 ms */
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

/* ============================================================
 * Task 2: CONTROL  [PROPORCIONAL SLOW-PWM - branch feat/bangbang-test]
 * ------------------------------------------------------------
 * Control proporcional sin zero-cross usando SLOW-PWM sobre el gate
 * del TRIAC.
 *
 *   error = T_set - T_med
 *   duty  = clamp(error * Kp, 0, 100) %        si error > DEADBAND
 *   duty  = 0                                  si error <= DEADBAND
 *
 * Slow-PWM:
 *   - El gate se enciende/apaga en ventanas de CTRL_PWM_PERIOD_MS (5 s).
 *   - Dentro de cada ventana, esta ON el (duty * 5s) primero, OFF el
 *     resto. Como la inercia termica del calefactor es de minutos,
 *     ventanas de segundos son perfectas.
 *
 * Loop a 4 Hz (250 ms) para tener resolucion fina del PWM (5%) sin
 * cambiar el periodo total. El sensor solo actualiza a 1 Hz pero esta
 * task usa la ultima lectura disponible en cada iteracion.
 *
 * Limitacion: control P puro tiene offset en regimen permanente. Si
 * la temperatura final queda > 0.5 C debajo del setpoint, agregar un
 * termino integral (PI) con anti-windup.
 *
 * Para volver al control proporcional original (phase-angle):
 *     git checkout main
 * ============================================================ */

#define CTRL_KP_PERCENT_PER_C    25.0f   /* ganancia: 25% de duty por C de error */
#define CTRL_PWM_PERIOD_MS       5000    /* ventana del slow-PWM */
#define CTRL_LOOP_MS             250     /* periodo del loop de control */
#define CTRL_DEADBAND_C          0.2f    /* error pequeno -> apaga (evita chasing) */
#define CTRL_KEEPALIVE_MS        500     /* timeout del modo TEST por refresh */

static void control_task(void *arg)
{
    ESP_LOGI(TAG, "control_task arrancando (core %d) [P SLOW-PWM Kp=%.0f%%/C]",
             xPortGetCoreID(), CTRL_KP_PERCENT_PER_C);

    const TickType_t period = pdMS_TO_TICKS(CTRL_LOOP_MS);
    TickType_t last_wake = xTaskGetTickCount();

    /* Estado del slow-PWM. Trabajamos en tiempo absoluto (esp_timer)
     * para que la ventana no dependa de la precision del scheduler. */
    int64_t pwm_window_start_us = esp_timer_get_time();
    float   current_duty = 0.0f;     /* fijado al inicio de cada ventana */
    bool    gate_on      = false;

    /* Para imprimir log solo cada 1 s (no cada 250 ms) */
    int log_skip = 0;

    while (1) {
        /* Leer estado del sensor (actualizado por sensor_task) */
        float    t_med = NAN;
        bool     sensor_ok = false;
        uint32_t age_ms = 0;
        state_get(&t_med, &sensor_ok, &age_ms);

        bool sensor_fresh = sensor_ok && !isnan(t_med) && age_ms < 3000;
        float t_set = web_server_get_setpoint();

        /* === Calculo del duty deseado === */
        float duty_target = 0.0f;
        float error = 0.0f;
        if (sensor_fresh) {
            error = t_set - t_med;
            if (error > CTRL_DEADBAND_C) {
                duty_target = error * (CTRL_KP_PERCENT_PER_C / 100.0f);
                if (duty_target > 1.0f) duty_target = 1.0f;
            }
            /* error <= deadband -> duty 0 (apagado) */
        }
        /* Sensor caido -> duty 0 (fail-safe) */

        /* === Slow-PWM === */
        int64_t now_us = esp_timer_get_time();
        int64_t elapsed_ms = (now_us - pwm_window_start_us) / 1000;

        if (elapsed_ms >= CTRL_PWM_PERIOD_MS) {
            /* Nueva ventana: actualizo el duty para los proximos 5 s */
            pwm_window_start_us = now_us;
            elapsed_ms = 0;
            current_duty = duty_target;
        }

        uint32_t on_time_ms = (uint32_t)(current_duty * CTRL_PWM_PERIOD_MS);
        bool should_be_on = (elapsed_ms < on_time_ms) && (current_duty > 0.0f);

        /* Aplicar al TRIAC (solo si cambia el estado, para no spamear) */
        if (should_be_on != gate_on) {
            gate_on = should_be_on;
            if (gate_on) {
                triac_force_on(CTRL_KEEPALIVE_MS);
            } else {
                triac_force_on(0);
            }
        } else if (gate_on) {
            /* Mantener vivo el modo TEST refrescando el timeout cada loop */
            triac_force_on(CTRL_KEEPALIVE_MS);
        }

        /* === UI === */
        float pot_pct = current_duty * 100.0f;
        ctrl_status_t st = {
            .t_med_c      = t_med,
            .t_set_c      = t_set,
            .potencia_pct = pot_pct,
            .t_dis_us     = gate_on ? 0 : 10000,
            .red_ok       = triac_zc_alive(),
            .sensor_ok    = sensor_fresh,
        };
        web_server_update_status(&st);

        /* Log cada ~1 s */
        if (++log_skip >= (1000 / CTRL_LOOP_MS)) {
            log_skip = 0;
            ESP_LOGI(TAG,
                "[P-PWM] T=%.2f Tset=%.2f err=%+.2f duty=%.0f%% gate=%s "
                "win=%lldms/%dms ZC=%u",
                isnan(t_med) ? 0.0 : t_med, t_set, error,
                pot_pct, gate_on ? "ON" : "OFF",
                elapsed_ms, CTRL_PWM_PERIOD_MS,
                (unsigned)triac_zc_count());
        }

        vTaskDelayUntil(&last_wake, period);
    }
}

/* ============================================================
 * Task 3: HEARTBEAT
 * ------------------------------------------------------------
 * Hace parpadear el LED on-board con una frecuencia que codifica
 * el estado del sistema:
 *    sensor_fail   -> 5 Hz   (100 ms on / 100 ms off)
 *    sobre-temp    -> 20 Hz  (25 ms / 25 ms)
 *    calentando    -> 10 Hz  (50 ms / 50 ms)
 *    en setpoint   -> 2 Hz   (250 ms / 250 ms)
 *
 * Asi el operario sabe que pasa solo mirando el LED, sin abrir
 * la pagina web.
 * ============================================================ */
static void heartbeat_task(void *arg)
{
    ESP_LOGI(TAG, "heartbeat_task arrancando (core %d)", xPortGetCoreID());

    gpio_reset_pin(PIN_LED_HEARTBEAT);
    gpio_set_direction(PIN_LED_HEARTBEAT, GPIO_MODE_OUTPUT);

    bool led = false;
    while (1) {
        float    t_med = NAN;
        bool     sensor_ok = false;
        uint32_t age_ms = 0;
        state_get(&t_med, &sensor_ok, &age_ms);
        float t_set = web_server_get_setpoint();

        uint32_t period_ms;
        if (!sensor_ok || isnan(t_med) || age_ms > 3000) {
            period_ms = 100;            /* 5 Hz - sin sensor */
        } else if (t_med > t_set + 0.5f) {
            period_ms = 25;             /* 20 Hz - sobre-temperatura */
        } else if (t_med < t_set - 0.5f) {
            period_ms = 50;             /* 10 Hz - calentando */
        } else {
            period_ms = 250;            /* 2 Hz - en setpoint */
        }

        led = !led;
        gpio_set_level(PIN_LED_HEARTBEAT, led ? 1 : 0);
        vTaskDelay(pdMS_TO_TICKS(period_ms));
    }
}

/* ============================================================
 * app_main - arranque del sistema
 * ============================================================ */
void app_main(void)
{
    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, "  Incubadora ESP32 - arrancando");
    ESP_LOGI(TAG, "  Build: " __DATE__ " " __TIME__);
    ESP_LOGI(TAG, "============================================");

    /* Mutex de estado compartido */
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "No se pudo crear el mutex - aborto");
        return;
    }

    /* === DIAGNOSTICO PRE-WIFI ===
     * Probe del bus OneWire ANTES de levantar WiFi/HTTPD. Si el sensor
     * responde aca pero no despues, el problema es interferencia de las
     * ISRs del SoftAP. Si no responde ni aca, el problema es del sensor
     * o del cableado. */
    ds18b20_probe_run(PIN_DS18B20);

    /* Modulos de hardware/red.
     * IMPORTANTE: triac_init() engancha la ISR de zero-cross; debe estar
     * arriba para que cuando arranque el control ya este capturando. */
    triac_init();
    wifi_mgr_start();
    web_server_start();

    /* === DIAGNOSTICO POST-WIFI ===
     * Mismo probe con WiFi ya activo. Comparar contra el anterior. */
    vTaskDelay(pdMS_TO_TICKS(500));   /* dejar que WiFi termine de bootear */
    ds18b20_probe_run(PIN_DS18B20);

    /* Lanzamos las tareas. El orden no es critico, pero arrancamos el
     * sensor primero para tener una lectura cuanto antes. */
    xTaskCreatePinnedToCore(sensor_task,    "sensor",
                            STACK_SENSOR,    NULL,
                            PRIO_SENSOR,     NULL, CORE_CONTROL);

    xTaskCreatePinnedToCore(control_task,   "control",
                            STACK_CONTROL,   NULL,
                            PRIO_CONTROL,    NULL, CORE_CONTROL);

    xTaskCreatePinnedToCore(heartbeat_task, "heartbeat",
                            STACK_HEARTBEAT, NULL,
                            PRIO_HEARTBEAT,  NULL, CORE_HEARTBEAT);

    ESP_LOGI(TAG, "Tareas creadas - sistema en marcha");
}
