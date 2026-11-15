#include "sweep.h"

#include <math.h>
#include <stdio.h>

#include "app.h"
#include "meas.h"
#include "ofet_config.h"
#include "ofet_hw.h"
#include "proto.h"

long sweep_count(float start, float stop, float step)
{
    step = fabsf(step);
    if (step <= 0.0f)
        return 0;
    float span = fabsf(stop - start);
    long n = (long)floorf(span / step + 1e-4f);
    float last = start + (stop >= start ? 1.0f : -1.0f) * step * (float)n;
    return n + 1 + (fabsf(last - stop) > 1e-4f ? 1 : 0);
}

float sweep_value(float start, float stop, float step, long i)
{
    long n = sweep_count(start, stop, step);
    if (i >= n - 1)
        return stop;
    return start + (stop >= start ? 1.0f : -1.0f) * fabsf(step) * (float)i;
}

static enum { SW_IDLE, SW_SET, SW_DELAY, SW_MEAS, SW_PAUSED } s_state;
static sweep_params_t P;
static bool s_pause_req;
static long s_ocnt, s_icnt, s_total;
static long s_o, s_k, s_n;          /* индексы: семейство, точка в проходе, номер точки (0..) */
static int s_pass, s_passes;
static uint32_t s_t0, s_last_msg;

bool sweep_active(void) { return s_state != SW_IDLE; }
bool sweep_paused(void) { return s_state != SW_IDLE && (s_pause_req || s_state == SW_PAUSED); }
bool sweep_is_output(void) { return P.output; }

static void send_prog(void)
{
    char buf[48];
    sprintf(buf, "PROG;%ld;%ld", s_n, s_total);
    hw_send_line(buf);
    s_last_msg = hw_millis();
}

void sweep_start(const sweep_params_t *p)
{
    P = *p;
    s_ocnt = P.output ? sweep_count(P.o_start, P.o_stop, P.o_step) : 1;
    s_icnt = sweep_count(P.i_start, P.i_stop, P.i_step);
    s_passes = P.reverse ? 2 : 1;
    s_total = s_ocnt * s_icnt * s_passes;
    s_o = s_k = s_n = 0;
    s_pass = 1;
    s_pause_req = false;
    s_last_msg = hw_millis();
    s_state = SW_SET;
}

static float inner_value(void)
{
    long idx = (s_pass == 1) ? s_k : (s_icnt - 1 - s_k);
    return sweep_value(P.i_start, P.i_stop, P.i_step, idx);
}

static void finish(const char *msg)
{
    meas_cancel();
    app_safe_state();
    s_state = SW_IDLE;
    if (msg)
        hw_send_line(msg);
}

void sweep_abort(void)
{
    if (s_state != SW_IDLE)
        finish(NULL);
}

void sweep_pause(void)
{
    if (s_state != SW_IDLE)
        s_pause_req = true;
}

void sweep_resume(void)
{
    s_pause_req = false;
}

void sweep_poll(void)
{
    uint32_t now = hw_millis();
    if (s_state == SW_IDLE)
        return;

    /* ПК должен получать сообщение не реже раза в секунду, даже при длинной задержке */
    if (now - s_last_msg >= OFET_PROG_PERIOD_MS)
        send_prog();

    switch (s_state) {
    case SW_IDLE:
        break;

    case SW_SET: {
        float ugs, uds;
        if (P.output) {
            ugs = sweep_value(P.o_start, P.o_stop, P.o_step, s_o);
            uds = inner_value();
        } else {
            ugs = inner_value();
            uds = P.uds;
        }
        int err = app_set_voltages(ugs, uds);   /* сначала сток, потом затвор */
        if (err) {
            char buf[32];
            sprintf(buf, "ERR;%s;%d", P.output ? "OUTP" : "TRANS", err);
            hw_send_line(buf);
            finish("ABORT");
            return;
        }
        s_t0 = now;
        s_state = SW_DELAY;
        break;
    }

    case SW_DELAY:
        if (s_pause_req) {
            s_state = SW_PAUSED;
        } else if (now - s_t0 >= P.delay_ms) {
            meas_start(P.window_ms);
            s_state = SW_MEAS;
        }
        break;

    case SW_MEAS: {
        if (s_pause_req) {
            meas_cancel();
            s_state = SW_PAUSED;
            break;
        }
        meas_result_t r;
        if (!meas_poll(&r))
            break;
        s_n++;
        char a[16], b[16], c[16], buf[96];
        if (fabsf(r.i) > P.ilim) {
            proto_fmt_e(r.i, c);
            sprintf(buf, "OVERI;%ld;%s", s_n, c);
            hw_send_line(buf);
            finish("ABORT");
            return;
        }
        proto_fmt_u(app_ugs(), a);
        proto_fmt_u(app_uds(), b);
        proto_fmt_e(r.i, c);
        sprintf(buf, "DATA;%ld;%d;%s;%s;%s;%d;%d", s_n, s_pass, a, b, c, r.range, r.flags);
        hw_send_line(buf);
        if (now - s_last_msg >= OFET_PROG_PERIOD_MS || s_n == s_total)
            send_prog();
        else
            s_last_msg = now;

        /* следующая точка */
        if (++s_k >= s_icnt) {
            s_k = 0;
            if (++s_pass > s_passes) {
                s_pass = 1;
                if (++s_o >= s_ocnt) {
                    finish(P.output ? "DONE;OUTP" : "DONE;TRANS");
                    return;
                }
            }
        }
        s_state = SW_SET;
        break;
    }

    case SW_PAUSED:
        if (!s_pause_req)
            s_state = SW_SET;       /* точку, прерванную паузой, повторяем целиком */
        break;
    }
}
