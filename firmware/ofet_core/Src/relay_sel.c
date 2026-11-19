#include "relay_sel.h"

#include "ofet_config.h"
#include "ofet_hw.h"

/* Группа g (0 = реле 1 и 4, 1 = реле 2, 2 = реле 3): канал SET = 2g+1, RESET = 2g+2. */
/* Требуемые состояния групп: 'A' = после RESET (моностабильное: обесточено),
 * 'B' = после SET (моностабильное: обмотка под напряжением), '-' = не важно
 * (моностабильное реле в этом случае обесточиваем: меньше тока и нагрева в камере). */
static const char k_table[5][3] = {
    { 'A', 'A', '-' },  /* 1 */
    { 'B', 'A', '-' },  /* 2 */
    { '-', 'B', 'A' },  /* 3 */
    { 'A', 'B', 'B' },  /* 4 */
    { 'B', 'B', 'B' },  /* 5 */
};

#define QMAX 8
static int s_queue[QMAX];
static int s_qn, s_qi;
static enum { S_IDLE, S_PULSE, S_GAP, S_SETTLE } s_state;
static uint32_t s_t0;
static int s_ch;

static inline void push(int ch)
{
    if (s_qn < QMAX)
        s_queue[s_qn++] = ch;
}

static inline void start_if_idle(void)
{
    if (s_state == S_IDLE && s_qi < s_qn) {
        s_state = S_GAP;            /* первый импульс начнётся в poll */
        s_t0 = hw_millis() - OFET_RELAY_GAP_MS;
    }
}

#if !OFET_SEL_BISTABLE
/* Моностабильные реле: сразу выставить уровни на линиях SET, линии RESET всегда 0. */
static void apply_levels(int n)
{
    for (int g = 0; g < 3; ++g) {
        const bool on = n > 0 && k_table[n - 1][g] == 'B';
        hw_sel_line(2 * g + 2, false);
        hw_sel_line(2 * g + 1, on);
    }
    s_qn = s_qi = 0;
    s_state = S_SETTLE;             /* пауза 200 мс на срабатывание и дребезг */
    s_t0 = hw_millis();
}

void relay_sel_init(void)
{
    apply_levels(0);                /* все обесточены = положение A */
}

bool relay_sel_select(int n)
{
    if (n < 1 || n > 5)
        return false;
    apply_levels(n);
    return true;
}

#else /* бистабильные */

void relay_sel_init(void)
{
    for (int ch = 1; ch <= 6; ++ch)
        hw_sel_line(ch, false);
    s_qn = s_qi = 0;
    s_state = S_IDLE;
    for (int g = 0; g < 3; ++g)
        push(2 * g + 2);            /* RESET всех групп */
    start_if_idle();
}

bool relay_sel_select(int n)
{
    if (n < 1 || n > 5)
        return false;
    if (s_state == S_IDLE) {
        s_qn = s_qi = 0;
    }
    for (int g = 0; g < 3; ++g) {
        char want = k_table[n - 1][g];
        if (want == 'A') push(2 * g + 2);
        else if (want == 'B') push(2 * g + 1);
    }
    start_if_idle();
    return true;
}

#endif

bool relay_sel_busy(void)
{
    return s_state != S_IDLE;
}

void relay_sel_poll(void)
{
    uint32_t now = hw_millis();
    switch (s_state) {
    case S_IDLE:
        break;
    case S_PULSE:
        if (now - s_t0 >= OFET_RELAY_PULSE_MS) {
            hw_sel_line(s_ch, false);
            s_state = S_GAP;
            s_t0 = now;
        }
        break;
    case S_GAP:
        if (now - s_t0 >= OFET_RELAY_GAP_MS) {
            if (s_qi < s_qn) {
                s_ch = s_queue[s_qi++];
                hw_sel_line(s_ch, true);    /* одновременно включена только одна линия */
                s_state = S_PULSE;
            } else {
                s_state = S_SETTLE;
            }
            s_t0 = now;
        }
        break;
    case S_SETTLE:
        if (now - s_t0 >= OFET_RELAY_SETTLE_MS) {
            s_state = S_IDLE;
            s_qn = s_qi = 0;
        }
        break;
    }
}
