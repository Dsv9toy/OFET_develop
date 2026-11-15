#include "meas.h"

#include <math.h>

#include "calib.h"
#include "ofet_config.h"
#include "ofet_hw.h"
#include "relay_range.h"

bool g_auto_range = true;

static enum { M_IDLE, M_WAIT_RELAY, M_SETTLE, M_WINDOW } s_state;
static uint32_t s_window, s_t0;
static int s_steps;
static float s_ubias = OFET_UBIAS_NOM;

float meas_ubias(void) { return s_ubias; }
void meas_set_ubias(float u) { s_ubias = u; }

static void begin_window(void)
{
    hw_adc_begin(s_window);
    s_state = M_WINDOW;
}

void meas_start(uint32_t window_ms)
{
    s_window = window_ms;
    s_steps = 0;
    if (relay_range_busy()) {
        s_state = M_WAIT_RELAY;
    } else {
        begin_window();
    }
}

void meas_cancel(void)
{
    s_state = M_IDLE;
}

bool meas_busy(void)
{
    return s_state != M_IDLE;
}

bool meas_poll(meas_result_t *r)
{
    uint32_t now = hw_millis();
    switch (s_state) {
    case M_IDLE:
        return false;
    case M_WAIT_RELAY:
        if (!relay_range_busy()) {
            s_state = M_SETTLE;
            s_t0 = now;
        }
        return false;
    case M_SETTLE:
        if (now - s_t0 >= OFET_RELAY_SETTLE_MS)
            begin_window();
        return false;
    case M_WINDOW: {
        float utia, ub;
        if (!hw_adc_poll(&utia, &ub))
            return false;
        s_ubias = ub;
        float du = fabsf(utia - ub);
        int range = relay_range_get();
        if (g_auto_range && s_steps < OFET_AUTORANGE_MAX_STEPS) {
            int next = range;
            if (du > OFET_AUTORANGE_HI && range > 1) {
                next = range - 1;               /* менее чувствительный (ток не известен, по шагу) */
            } else if (du < OFET_AUTORANGE_LO && range < OFET_N_RANGES) {
                /* Более чувствительный: сразу на диапазон, где ожидаемый сигнал не больше 1 В */
                next = range + 1;
                while (next < OFET_N_RANGES &&
                       du * g_calib.rf[next] / g_calib.rf[range - 1] < 1.0f)
                    next++;
            }
            if (next != range) {
                s_steps++;
                relay_range_set(next);
                s_state = M_WAIT_RELAY;         /* затем пауза 200 мс и повтор точки */
                return false;
            }
        }
        r->utia = utia;
        r->ubias = ub;
        r->range = range;
        r->flags = 0;
        if (du > OFET_OVERLOAD_U)
            r->flags |= MEAS_FLAG_OVERLOAD;
        if (du < OFET_AUTORANGE_LO && range == OFET_N_RANGES)
            r->flags |= MEAS_FLAG_LOW;
        r->i = (ub - utia) / g_calib.rf[range - 1];
        s_state = M_IDLE;
        return true;
    }
    }
    return false;
}
