/* Конечный автомат развёрток (CLAUDE.md, п. 6). */
#ifndef SWEEP_H
#define SWEEP_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool output;            /* false = проходная (TRANS), true = выходная (OUTP) */
    char type;              /* 'n' или 'p' (на алгоритм не влияет) */
    float uds;              /* TRANS: постоянное Uси */
    float o_start, o_stop, o_step;  /* OUTP: семейство Uзи */
    float i_start, i_stop, i_step;  /* развёртываемое: Uзи (TRANS) или Uси (OUTP) */
    uint32_t delay_ms, window_ms;
    float ilim;
    bool reverse;
} sweep_params_t;

/* Число точек и значение i-й точки развёртки; последняя точка всегда stop */
long  sweep_count(float start, float stop, float step);
float sweep_value(float start, float stop, float step, long i);

void sweep_start(const sweep_params_t *p);   /* параметры уже проверены */
void sweep_poll(void);
void sweep_pause(void);
void sweep_resume(void);
void sweep_abort(void);                       /* без сообщений, вызывающий шлёт ABORT */
bool sweep_active(void);
bool sweep_paused(void);
bool sweep_is_output(void);

#endif
