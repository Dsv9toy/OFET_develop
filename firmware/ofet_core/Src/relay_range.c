#include "relay_range.h"

#include "ofet_config.h"
#include "ofet_hw.h"

typedef struct {
    signed char range, coil, level;
    unsigned short wait_ms;         /* пауза после действия */
} step_t;

#define QMAX 40
static step_t s_q[QMAX];
static int s_qn, s_qi;
static int s_range = 1;
static bool s_waiting;
static uint32_t s_t0, s_wait;

static void push(int range, int coil, int level, unsigned wait_ms)
{
    if (s_qn < QMAX) {
        step_t s = { (signed char)range, (signed char)coil, (signed char)level, (unsigned short)wait_ms };
        s_q[s_qn++] = s;
    }
}

#if OFET_RANGE_BISTABLE
static void pulse(int range, int coil)
{
    push(range, coil, 1, OFET_RELAY_PULSE_MS);
    push(range, coil, 0, OFET_RELAY_GAP_MS);
}
#endif

void relay_range_init(void)
{
    for (int r = 1; r <= OFET_N_RANGES; ++r) {
        hw_range_coil(r, 0, false);
        hw_range_coil(r, 1, false);
    }
    s_qn = s_qi = 0;
    s_waiting = false;
    s_range = 1;
#if OFET_RANGE_BISTABLE
    pulse(1, 0);                            /* SET диапазона 1 */
    for (int r = 2; r <= OFET_N_RANGES; ++r)
        pulse(r, 1);                        /* RESET остальных */
#else
    push(1, 0, 1, OFET_RANGE_OVERLAP_MS);
#endif
}

void relay_range_set(int r)
{
    if (r < 1 || r > OFET_N_RANGES || r == s_range)
        return;
    if (!relay_range_busy())
        s_qn = s_qi = 0;
    int old = s_range;
    s_range = r;
#if OFET_RANGE_BISTABLE
    pulse(r, 0);        /* SET нового */
    pulse(old, 1);      /* RESET старого */
#else
    push(r, 0, 1, OFET_RANGE_OVERLAP_MS);
    push(old, 0, 0, 0);
#endif
}

int relay_range_get(void)
{
    return s_range;
}

bool relay_range_busy(void)
{
    return s_waiting || s_qi < s_qn;
}

void relay_range_poll(void)
{
    uint32_t now = hw_millis();
    while (true) {
        if (s_waiting) {
            if (now - s_t0 < s_wait)
                return;
            s_waiting = false;
        }
        if (s_qi >= s_qn) {
            s_qn = s_qi = 0;
            return;
        }
        step_t s = s_q[s_qi++];
        hw_range_coil(s.range, s.coil, s.level != 0);
        if (s.wait_ms) {
            s_waiting = true;
            s_t0 = now;
            s_wait = s.wait_ms;
        }
    }
}
