/*
 * triac_ctrl.h - Control del TRIAC por angulo de fase
 *
 * Engancha una ISR al pin de paso por cero (SFH620AA) y arma un esp_timer
 * one-shot para disparar la puerta del TRIAC despues de t_dis. El pulso
 * tiene un ancho fijo (TRIAC_PULSE_US) y luego se libera la puerta.
 */

#ifndef TRIAC_CTRL_H
#define TRIAC_CTRL_H

#include <stdint.h>
#include <stdbool.h>

/* Inicializa GPIOs, ISR de paso por cero y timer one-shot. */
void triac_init(void);

/* Setea el retardo de disparo despues del paso por cero.
 * Rango valido: 0..10000 us. Valores fuera de rango se recortan.
 *  - 0    us  => 100% de potencia
 *  - 10000 us => 0%  de potencia (no se dispara) */
void triac_set_delay_us(uint32_t us);

/* Devuelve el ultimo retardo configurado (us). */
uint32_t triac_get_delay_us(void);

/* Devuelve true si en los ultimos N ms hubo cruces por cero. Sirve para
 * que la pagina web indique "red OK / sin red". */
bool triac_zc_alive(void);

/* Contador de cruces por cero (para debug). */
uint32_t triac_zc_count(void);

#endif /* TRIAC_CTRL_H */
