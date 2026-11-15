/* Калибровочные коэффициенты. Пока значения по умолчанию; хранение во flash - позже. */
#ifndef CALIB_H
#define CALIB_H

#include "ofet_config.h"

typedef struct {
    float a[2];                 /* Uвых = a * D + b, канал 0 = затвор, 1 = сток */
    float b[2];
    float rf[OFET_N_RANGES];    /* Rос диапазонов 1..7, Ом */
} calib_t;

extern calib_t g_calib;

void calib_defaults(void);

#endif
