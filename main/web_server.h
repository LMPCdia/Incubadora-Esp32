/*
 * web_server.h - Servidor HTTP con pagina embebida + API JSON
 *
 * Endpoints:
 *   GET  /         pagina HTML con grafico, lecturas y formulario
 *   GET  /api      JSON con t_actual, t_set, potencia, estado
 *   POST /setpoint body "t=37.5"  fija el setpoint manualmente
 */

#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <stdbool.h>
#include <stdint.h>

/* Estructura compartida con el loop de control */
typedef struct {
    float    t_med_c;       /* temperatura medida */
    float    t_set_c;       /* setpoint actual */
    float    potencia_pct;  /* 0..100 */
    uint32_t t_dis_us;      /* delay de disparo */
    bool     red_ok;        /* hay paso por cero */
    bool     sensor_ok;     /* sensor responde */
} ctrl_status_t;

/* Lanza el HTTP server */
void web_server_start(void);

/* Llamada desde el loop de control con la lectura mas reciente */
void web_server_update_status(const ctrl_status_t *s);

/* Devuelve el setpoint que llego por POST (o setpoint default si nada). */
float web_server_get_setpoint(void);

#endif /* WEB_SERVER_H */
