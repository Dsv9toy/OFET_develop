/* Измерение одной точки с автовыбором диапазона (CLAUDE.md, п. 3.6). Неблокирующее. */
#ifndef MEAS_H
#define MEAS_H

#include <stdbool.h>
#include <stdint.h>

#define MEAS_FLAG_OVERLOAD  1   /* |Uтиа - Uсм| > 1,55 В */
#define MEAS_FLAG_LOW       2   /* < 0,12 В на диапазоне 7 */

typedef struct {
    float i;        /* ток стока, А */
    float utia;
    float ubias;
    int range;
    int flags;
} meas_result_t;

extern bool g_auto_range;

void  meas_start(uint32_t window_ms);
bool  meas_poll(meas_result_t *r);      /* true один раз, когда точка готова */
void  meas_cancel(void);
bool  meas_busy(void);
float meas_ubias(void);                 /* последнее измеренное Uсм */
void  meas_set_ubias(float u);

#endif
