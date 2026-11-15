/* Модель железа для проверки прошивки на ПК.
 * Реализует ofet_hw.h и следит за правилами безопасности (печатает "!! ..." при нарушении). */
#include "sim.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ofet_config.h"
#include "ofet_hw.h"

sim_t g_sim;

void sim_violation(const char *fmt, const char *a, int b)
{
    char buf[200];
    snprintf(buf, sizeof buf, fmt, a, b);
    printf("%u !! %s\n", (unsigned)hw_millis(), buf);
    g_sim.violations++;
    fflush(stdout);
}

/* ------------------------------------------------------------ ЦАП и формирователь */

static int s_gain[2] = { 1, 1 };
static unsigned s_code[2];

static float uout(int ch)
{
    if (!g_sim.pwr18)
        return 0.0f;
    float udac = (float)s_gain[ch] * 2.5f * (float)s_code[ch] / 65536.0f;
    float u = 6.8f * (2.5f - udac);
    if (u > 17.3f) u = 17.3f;
    if (u < -17.3f) u = -17.3f;
    return u;
}

float sim_ugs(void) { return uout(0) - g_sim.ubias_true; }
float sim_uds(void) { return uout(1) - g_sim.ubias_true; }

bool hw_i2c_write(uint8_t addr7, const uint8_t *d, uint16_t len)
{
    if (g_sim.i2c_fail)
        return false;
    int ch = addr7 == 0x4C ? 0 : addr7 == 0x4E ? 1 : -1;
    if (ch < 0 || len != 3)
        return false;
    uint16_t v = (uint16_t)(d[1] << 8 | d[2]);
    if (d[0] == 0x40)
        s_gain[ch] = (v & (1u << 11)) ? 2 : 1;
    else if (d[0] == 0x30)
        s_code[ch] = v;
    return true;
}

void hw_pwr18(bool on)
{
    if (on && !g_sim.pwr18 && (s_gain[0] != 2 || s_gain[1] != 2))
        sim_violation("питание %s включено до настройки ЦАП", "±18 В", 0);
    g_sim.pwr18 = on;
}

/* ------------------------------------------------------------ коммутатор транзисторов */

static bool s_sel_on[7];
static uint32_t s_sel_t[7];
static char s_group[3] = { '?', '?', '?' };

static bool safe_now(void)
{
    return fabsf(sim_ugs()) < 0.01f && fabsf(sim_uds()) < 0.01f;
}

void hw_sel_line(int ch, bool on)
{
    if (ch < 1 || ch > 6) return;
    if (on && !s_sel_on[ch]) {
        for (int i = 1; i <= 6; ++i)
            if (i != ch && s_sel_on[i])
                sim_violation("коммутатор: одновременно включены линии (%s), канал %d", "две", ch);
        if (!safe_now())
            sim_violation("коммутатор: переключение не в безопасном состоянии%s, канал %d", "", ch);
        s_sel_t[ch] = hw_millis();
    } else if (!on && s_sel_on[ch]) {
        uint32_t dt = hw_millis() - s_sel_t[ch];
        if (dt < 20)
            sim_violation("коммутатор: импульс короче 20 мс%s, канал %d", "", ch);
        int g = (ch - 1) / 2;
        s_group[g] = (ch % 2) ? 'B' : 'A';
    }
    s_sel_on[ch] = on;
}

int sim_transistor(void)
{
    char a = s_group[0], b = s_group[1], c = s_group[2];
    if (b == 'A' && a == 'A') return 1;
    if (b == 'A' && a == 'B') return 2;
    if (b == 'B' && c == 'A') return 3;
    if (b == 'B' && c == 'B' && a == 'A') return 4;
    if (b == 'B' && c == 'B' && a == 'B') return 5;
    return 0;
}

/* ------------------------------------------------------------ реле диапазонов */

static bool s_closed[OFET_N_RANGES + 1];
static bool s_coil[OFET_N_RANGES + 1][2];
static uint32_t s_coil_t[OFET_N_RANGES + 1][2];
static void unused_guard(void) { (void)s_coil_t; }
void (*sim_unused_guard)(void) = unused_guard;

void hw_range_coil(int r, int coil, bool on)
{
    if (r < 1 || r > OFET_N_RANGES) return;
    if (on && s_coil[r][1 - coil])
        sim_violation("реле диапазона: SET и RESET одновременно%s (диапазон %d)", "", r);
    s_coil[r][coil] = on;
#if OFET_RANGE_BISTABLE
    if (on) {
        s_coil_t[r][coil] = hw_millis();
    } else if (hw_millis() - s_coil_t[r][coil] >= 4) {
        s_closed[r] = (coil == 0);
    }
#else
    if (coil == 0) s_closed[r] = on;
#endif
}

static float rf_now(void)
{
    static const float rf[8] = { 0, 1e3f, 1e4f, 1e5f, 1e6f, 1e7f, 1.01e8f, 1.0101e9f };
    float g = 0;
    for (int r = 1; r <= OFET_N_RANGES; ++r)
        if (s_closed[r]) g += 1.0f / rf[r];
    return g > 0 ? 1.0f / g : 0.0f;
}

void sim_check_feedback(bool booted)
{
    static bool reported;
    if (!booted) return;
    if (rf_now() == 0.0f) {
        if (!reported)
            sim_violation("обратная связь ТИА разомкнута%s (%d замкнутых реле)", "", 0);
        reported = true;
    } else {
        reported = false;
    }
}

/* ------------------------------------------------------------ транзистор и АЦП */

static float model_current(float ugs, float uds, int t)
{
    if (t == 0) return 0.0f;
    float k = 2e-8f * (1.0f + 0.15f * (float)(t - 3));
    float s = g_sim.ntype ? 1.0f : -1.0f;
    float uth = g_sim.ntype ? 2.0f : -2.0f;
    float vov = s * (ugs - uth), vds = s * uds, i;
    if (vov <= 0) i = 0;
    else if (vds < vov) i = k * (vov * vds - vds * vds / 2);
    else i = k / 2 * vov * vov * (1 + 0.02f * vds);
    i = s * i + 1e-11f * (uds / 5.0f);
    if (g_sim.big_current) i *= 1e4f;
    return i;
}

static uint32_t s_adc_end;
static bool s_adc_run;

void hw_adc_begin(uint32_t window_ms)
{
    s_adc_end = hw_millis() + window_ms;
    s_adc_run = true;
}

bool hw_adc_poll(float *utia, float *ubias)
{
    if (!s_adc_run || (int32_t)(hw_millis() - s_adc_end) < 0)
        return false;
    s_adc_run = false;
    float ub = g_sim.ubias_true;
    float rf = rf_now();
    float i = model_current(sim_ugs(), sim_uds(), sim_transistor());
    float u = rf > 0 ? ub - i * rf : (i > 0 ? 0.0f : 3.3f);
    if (u < 0) u = 0;
    if (u > 3.3f) u = 3.3f;
    const float q = 3.3f / 4096.0f;
    *utia = roundf(u / q) * q;
    *ubias = roundf(ub / q) * q;
    return true;
}

void sim_init(void)
{
    /* Неизвестное положение бистабильных реле после включения питания */
    s_group[0] = 'B'; s_group[1] = 'B'; s_group[2] = 'A';
#if OFET_RANGE_BISTABLE
    s_closed[3] = s_closed[6] = true;
#endif
    g_sim.ubias_true = 1.6480f;
}
