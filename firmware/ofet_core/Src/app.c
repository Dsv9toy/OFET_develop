#include "app.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "calib.h"
#include "dac.h"
#include "meas.h"
#include "ofet_config.h"
#include "ofet_hw.h"
#include "proto.h"
#include "relay_range.h"
#include "relay_sel.h"
#include "sweep.h"

/* ------------------------------------------------------------ состояние */

static enum { B_DAC, B_UBIAS, B_RELAYS, B_READY } s_boot;
static uint32_t s_boot_t0;

static float s_ugs, s_uds;          /* установленные напряжения относительно истока */
static int s_selected;              /* 0 = не выбран */

static char s_queue[OFET_QUEUE_LEN][OFET_LINE_MAX];
static int s_qhead, s_qcount;

static enum { EX_NONE, EX_SEL, EX_RANGE, EX_MEAS } s_exec;
static int s_exec_arg;
static uint32_t s_exec_t0;

static bool s_connected;
static uint32_t s_last_rx;
static bool s_link_lost;

float app_ugs(void) { return s_ugs; }
float app_uds(void) { return s_uds; }
bool app_ready(void) { return s_boot == B_READY; }

/* ------------------------------------------------------------ напряжения */

int app_set_voltages(float ugs, float uds)
{
    if (ugs < OFET_U_USER_MIN || ugs > OFET_U_USER_MAX ||
        uds < OFET_U_USER_MIN || uds > OFET_U_USER_MAX)
        return 2;
    float ub = meas_ubias();
    float og = ugs + ub, od = uds + ub;
    if (fabsf(og) > OFET_UOUT_HW_LIMIT || fabsf(od) > OFET_UOUT_HW_LIMIT)
        return 2;
    bool ok = dac_set_uout(DAC_DRAIN, od);
    ok = dac_set_uout(DAC_GATE, og) && ok;
    if (!ok)
        return 3;
    s_ugs = ugs;
    s_uds = uds;
    return 0;
}

void app_safe_state(void)
{
    app_set_voltages(0.0f, 0.0f);   /* оба выхода = Uсм; реле не трогаем */
}

/* ------------------------------------------------------------ ответы */

static void send(const char *s) { hw_send_line(s); }

static void sendf_err(const char *cmd, int code)
{
    char buf[48];
    snprintf(buf, sizeof buf, "ERR;%s;%d", cmd, code);
    send(buf);
}

static const char *mode_name(void)
{
    if (s_boot != B_READY) return "BOOT";
    if (sweep_active()) {
        if (sweep_paused()) return "PAUSE";
        return sweep_is_output() ? "OUTP" : "TRANS";
    }
    return "IDLE";
}

/* ------------------------------------------------------------ команды */

static bool get_f(char *f[], int n, int idx, float *v) { return idx < n && proto_float(f[idx], v); }
static bool get_i(char *f[], int n, int idx, long *v) { return idx < n && proto_int(f[idx], v); }
static bool u_ok(float u) { return u >= OFET_U_USER_MIN && u <= OFET_U_USER_MAX; }

static void cmd_sweep(char *f[], int n, bool output)
{
    const char *cmd = output ? "OUTP" : "TRANS";
    sweep_params_t p;
    memset(&p, 0, sizeof p);
    p.output = output;
    int need = output ? 12 : 10;
    if (n != need || (strcmp(f[1], "n") != 0 && strcmp(f[1], "p") != 0)) {
        sendf_err(cmd, 1);
        return;
    }
    p.type = f[1][0];
    int k = 2;
    bool ok = true;
    if (!output) {
        ok = get_f(f, n, k++, &p.uds);
    } else {
        ok = get_f(f, n, k, &p.o_start) && get_f(f, n, k + 1, &p.o_stop) && get_f(f, n, k + 2, &p.o_step);
        k += 3;
    }
    ok = ok && get_f(f, n, k, &p.i_start) && get_f(f, n, k + 1, &p.i_stop) && get_f(f, n, k + 2, &p.i_step);
    k += 3;
    long delay, window, rev;
    ok = ok && get_i(f, n, k, &delay) && get_i(f, n, k + 1, &window)
            && get_f(f, n, k + 2, &p.ilim) && get_i(f, n, k + 3, &rev);
    if (!ok || delay < 100 || delay > 60000 || window < 20 || window > 2000 || window % 20
        || p.ilim <= 0.0f || (rev != 0 && rev != 1) || p.i_step <= 0.0f || p.i_start == p.i_stop
        || (output && (p.o_step <= 0.0f || p.o_start == p.o_stop))) {
        sendf_err(cmd, 1);
        return;
    }
    if (!u_ok(p.i_start) || !u_ok(p.i_stop) ||
        (!output && !u_ok(p.uds)) || (output && (!u_ok(p.o_start) || !u_ok(p.o_stop)))) {
        sendf_err(cmd, 2);
        return;
    }
    long total = sweep_count(p.i_start, p.i_stop, p.i_step) * (rev ? 2 : 1)
               * (output ? sweep_count(p.o_start, p.o_stop, p.o_step) : 1);
    if (total > OFET_MAX_POINTS) {
        sendf_err(cmd, 1);
        return;
    }
    if (s_selected == 0) {
        sendf_err(cmd, 5);
        return;
    }
    p.delay_ms = (uint32_t)delay;
    p.window_ms = (uint32_t)window;
    p.reverse = rev != 0;

    app_safe_state();
    send(output ? "ACK;OUTP" : "ACK;TRANS");
    sweep_start(&p);
}

/* Обычные команды: выполняются по очереди, во время развёртки недоступны */
static void exec_command(char *line)
{
    char *f[PROTO_MAX_FIELDS];
    int n = proto_split(line, f, PROTO_MAX_FIELDS);
    const char *cmd = f[0];
    const char *name = strcmp(cmd, "MEAS?") == 0 ? "MEAS" : cmd;

    bool known = !strcmp(cmd, "SEL") || !strcmp(cmd, "SETV") || !strcmp(cmd, "RANGE")
              || !strcmp(cmd, "MEAS?") || !strcmp(cmd, "TRANS") || !strcmp(cmd, "OUTP");
    if (!known) {
        sendf_err(name, 1);
        return;
    }
    if (sweep_active()) {
        sendf_err(name, 4);
        return;
    }

    if (!strcmp(cmd, "SEL")) {
        long t;
        if (n != 2 || !get_i(f, n, 1, &t) || t < 1 || t > 5) {
            sendf_err("SEL", 1);
            return;
        }
        app_safe_state();                   /* переключать только в безопасном состоянии */
        s_selected = 0;
        relay_sel_select((int)t);
        s_exec = EX_SEL;
        s_exec_arg = (int)t;
    } else if (!strcmp(cmd, "SETV")) {
        float g, d;
        if (n != 3 || !get_f(f, n, 1, &g) || !get_f(f, n, 2, &d)) {
            sendf_err("SETV", 1);
            return;
        }
        int err = app_set_voltages(g, d);
        if (err) {
            sendf_err("SETV", err);
            return;
        }
        char a[16], b[16], buf[48];
        proto_fmt_u(s_ugs, a);
        proto_fmt_u(s_uds, b);
        snprintf(buf, sizeof buf, "ACK;SETV;%s;%s", a, b);
        send(buf);
    } else if (!strcmp(cmd, "RANGE")) {
        long r;
        if (n != 2 || !get_i(f, n, 1, &r) || r < 0 || r > OFET_N_RANGES) {
            sendf_err("RANGE", 1);
            return;
        }
        g_auto_range = (r == 0);
        if (r > 0)
            relay_range_set((int)r);
        s_exec = EX_RANGE;
        s_exec_arg = (int)r;
        s_exec_t0 = 0;
    } else if (!strcmp(cmd, "MEAS?")) {
        meas_start(OFET_MEAS_WINDOW_MS);
        s_exec = EX_MEAS;
    } else {
        cmd_sweep(f, n, !strcmp(cmd, "OUTP"));
    }
}

static void exec_poll(void)
{
    char buf[96];
    uint32_t now = hw_millis();
    switch (s_exec) {
    case EX_NONE:
        if (s_boot == B_READY && s_qcount > 0) {
            char line[OFET_LINE_MAX];
            strcpy(line, s_queue[s_qhead]);
            s_qhead = (s_qhead + 1) % OFET_QUEUE_LEN;
            s_qcount--;
            exec_command(line);
        }
        break;
    case EX_SEL:
        if (!relay_sel_busy()) {
            s_selected = s_exec_arg;
            snprintf(buf, sizeof buf, "ACK;SEL;%d", s_selected);
            send(buf);
            s_exec = EX_NONE;
        }
        break;
    case EX_RANGE:
        if (relay_range_busy()) {
            s_exec_t0 = 0;
        } else if (s_exec_t0 == 0) {
            s_exec_t0 = now | 1u;       /* реле отработали, ждём установления */
        } else if (now - s_exec_t0 >= OFET_RELAY_SETTLE_MS) {
            snprintf(buf, sizeof buf, "ACK;RANGE;%d", s_exec_arg);
            send(buf);
            s_exec = EX_NONE;
        }
        break;
    case EX_MEAS: {
        meas_result_t r;
        if (meas_poll(&r)) {
            char ub[16], g[16], d[16], i[16];
            proto_fmt_u(r.ubias, ub);
            proto_fmt_u(s_ugs, g);
            proto_fmt_u(s_uds, d);
            proto_fmt_e(r.i, i);
            snprintf(buf, sizeof buf, "MEAS;%s;%s;%s;%s;%d;%d", ub, g, d, i, r.range, r.flags);
            send(buf);
            s_exec = EX_NONE;
        }
        break;
    }
    }
}

/* Срочные команды: выполняются сразу, в том числе во время развёртки */
static bool urgent_command(char *line)
{
    char buf[96];
    if (!strcmp(line, "PING")) {
        send("PONG");
    } else if (!strcmp(line, "STAT?")) {
        char ub[16];
        proto_fmt_u(meas_ubias(), ub);
        snprintf(buf, sizeof buf, "STAT;%s;%s;%d;%d;%d", mode_name(), ub, s_selected,
                 relay_range_get(), g_auto_range ? 1 : 0);
        send(buf);
    } else if (!strcmp(line, "STOP")) {
        sweep_abort();
        if (s_boot == B_READY)
            app_safe_state();
        send("ABORT");
    } else if (!strcmp(line, "ZERO")) {
        if (sweep_active()) {
            sweep_abort();
            send("ABORT");
        }
        if (s_boot != B_READY) {
            send("ACK;ZERO");               /* при старте выходы и так в безопасном состоянии */
        } else {
            int err = app_set_voltages(0.0f, 0.0f);
            if (err) sendf_err("ZERO", err);
            else send("ACK;ZERO");
        }
    } else if (!strcmp(line, "PAUSE")) {
        sweep_pause();
        send("ACK;PAUSE");
    } else if (!strcmp(line, "RESUME")) {
        sweep_resume();
        send("ACK;RESUME");
    } else {
        return false;
    }
    return true;
}

/* ------------------------------------------------------------ вход */

void app_on_line(const char *line)
{
    s_last_rx = hw_millis();
    s_link_lost = false;

    char buf[OFET_LINE_MAX];
    size_t len = strlen(line);
    while (len && (line[len - 1] == '\r' || line[len - 1] == ' ' || line[len - 1] == '\n'))
        len--;
    if (len == 0)
        return;
    if (len >= sizeof buf) {
        sendf_err("LINE", 1);
        return;
    }
    memcpy(buf, line, len);
    buf[len] = '\0';

    if (urgent_command(buf))
        return;
    if (s_qcount >= OFET_QUEUE_LEN) {
        char *f[PROTO_MAX_FIELDS];
        proto_split(buf, f, PROTO_MAX_FIELDS);
        sendf_err(f[0], 4);
        return;
    }
    int tail = (s_qhead + s_qcount) % OFET_QUEUE_LEN;
    strcpy(s_queue[tail], buf);
    s_qcount++;
}

void app_on_connect(void)
{
    s_connected = true;
    s_last_rx = hw_millis();
    s_link_lost = false;
}

static void link_lost(void)
{
    sweep_abort();
    if (s_boot == B_READY)
        app_safe_state();
    s_qcount = 0;
}

void app_on_disconnect(void)
{
    s_connected = false;
    link_lost();
}

/* ------------------------------------------------------------ старт и цикл */

void app_init(void)
{
    calib_defaults();
    meas_set_ubias(OFET_UBIAS_NOM);
    s_boot = B_DAC;
    s_boot_t0 = hw_millis() - 1000;
    s_exec = EX_NONE;
    s_qhead = s_qcount = 0;
    s_selected = 0;
    g_auto_range = true;
    hw_pwr18(false);
}

static void boot_poll(void)
{
    uint32_t now = hw_millis();
    switch (s_boot) {
    case B_DAC:
        /* Без ответа ЦАП питание ±18 В не включаем: на выходах было бы +17 В */
        if (now - s_boot_t0 >= 500) {
            s_boot_t0 = now;
            if (dac_init() && app_set_voltages(0.0f, 0.0f) == 0) {
                hw_pwr18(true);
                hw_adc_begin(OFET_STARTUP_WINDOW_MS);
                s_boot = B_UBIAS;
            }
        }
        break;
    case B_UBIAS: {
        float utia, ub;
        if (hw_adc_poll(&utia, &ub)) {
            (void)utia;
            meas_set_ubias(ub);
            app_safe_state();               /* пересчёт кодов по измеренному Uсм */
            relay_sel_init();
            relay_range_init();
            s_boot = B_RELAYS;
        }
        break;
    }
    case B_RELAYS:
        if (!relay_sel_busy() && !relay_range_busy())
            s_boot = B_READY;
        break;
    case B_READY:
        break;
    }
}

void app_poll(void)
{
    relay_sel_poll();
    relay_range_poll();
    if (s_boot != B_READY) {
        boot_poll();
        return;
    }
    sweep_poll();
    exec_poll();

    if (s_connected && !s_link_lost && hw_millis() - s_last_rx > OFET_LINK_TIMEOUT_MS) {
        s_link_lost = true;                 /* повторно сработает только после новой строки */
        link_lost();
    }
}
