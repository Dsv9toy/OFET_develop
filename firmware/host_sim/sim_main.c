/* Симулятор платы OFET: настоящая логика прошивки (ofet_core) + модель железа.
 *
 * Режим сценария (виртуальное время, для автотестов):
 *   ./ofet_sim [--n] < scenario.txt
 *   строки сценария: команда протокола, либо
 *     #wait <мс>                    идти вперёд по времени
 *     #until <префикс> [<мс>]       идти, пока плата не пришлёт строку с таким началом
 *     #connect / #disconnect        подключение ПК
 *     #i2cfail 0|1, #bigcurrent 0|1 внести неисправность
 *     #state                        напечатать Uзи, Uси, транзистор
 *
 * Режим TCP (реальное время, для программы ПК):
 *   ./ofet_sim --tcp 5000 [--n]
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app.h"
#include "ofet_hw.h"
#include "sim.h"

static char s_last_out[256];

uint32_t hw_millis(void)
{
    if (g_sim.realtime) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
    }
    return g_sim.now;
}

/* ------------------------------------------------------------ сценарий */

static void step(void)
{
    g_sim.now++;
    app_poll();
    sim_check_feedback(app_ready());
}

static int s_client = -1;
void tcp_send(const char *line);

void hw_send_line(const char *line)
{
    snprintf(s_last_out, sizeof s_last_out, "%s", line);
    if (g_sim.realtime) {
        tcp_send(line);
        return;
    }
    printf("%u < %s\n", (unsigned)g_sim.now, line);
    fflush(stdout);
}

static int run_script(void)
{
    char line[512];
    app_init();
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!line[0]) continue;
        if (line[0] == '#') {
            char what[32], arg[64] = "";
            long ms = 0;
            int n = sscanf(line + 1, "%31s %63s %ld", what, arg, &ms);
            if (!strcmp(what, "wait")) {
                long w = atol(arg);
                for (long i = 0; i < w; ++i) step();
            } else if (!strcmp(what, "until")) {
                long limit = n >= 3 ? ms : 600000;
                s_last_out[0] = 0;
                long i = 0;
                for (; i < limit && strncmp(s_last_out, arg, strlen(arg)) != 0; ++i) step();
                if (i >= limit) printf("%u ?? timeout waiting for %s\n", (unsigned)g_sim.now, arg);
            } else if (!strcmp(what, "connect")) {
                app_on_connect();
            } else if (!strcmp(what, "disconnect")) {
                app_on_disconnect();
            } else if (!strcmp(what, "i2cfail")) {
                g_sim.i2c_fail = atoi(arg) != 0;
            } else if (!strcmp(what, "bigcurrent")) {
                g_sim.big_current = atoi(arg) != 0;
            } else if (!strcmp(what, "state")) {
                printf("%u == Ugs=%.4f Uds=%.4f T=%d pwr=%d\n", (unsigned)g_sim.now,
                       sim_ugs(), sim_uds(), sim_transistor(), g_sim.pwr18);
            }
            continue;
        }
        printf("%u > %s\n", (unsigned)g_sim.now, line);
        app_on_line(line);
        step();
    }
    printf("%u END violations=%d\n", (unsigned)g_sim.now, g_sim.violations);
    return g_sim.violations ? 1 : 0;
}

/* ------------------------------------------------------------ TCP */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

void tcp_send(const char *line)
{
    printf("< %s\n", line);
    fflush(stdout);
    if (s_client < 0) return;
    char buf[300];
    int n = snprintf(buf, sizeof buf, "%s\n", line);
    if (send(s_client, buf, (size_t)n, MSG_NOSIGNAL) < 0) {
        close(s_client);
        s_client = -1;
        app_on_disconnect();
    }
}

static int run_tcp(int port)
{
    g_sim.realtime = true;
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a = { 0 };
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(srv, (struct sockaddr *)&a, sizeof a) < 0 || listen(srv, 1) < 0) {
        perror("bind");
        return 1;
    }
    fcntl(srv, F_SETFL, O_NONBLOCK);
    printf("ofet_sim: TCP порт %d\n", port);
    fflush(stdout);
    app_init();
    char rx[1024];
    size_t rxn = 0;
    for (;;) {
        if (s_client < 0) {
            int c = accept(srv, NULL, NULL);
            if (c >= 0) {
                fcntl(c, F_SETFL, O_NONBLOCK);
                s_client = c;
                rxn = 0;
                app_on_connect();
                printf("клиент подключён\n");
            }
        } else {
            ssize_t n = recv(s_client, rx + rxn, sizeof rx - 1 - rxn, 0);
            if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                close(s_client);
                s_client = -1;
                app_on_disconnect();
                printf("клиент отключён\n");
            } else if (n > 0) {
                rxn += (size_t)n;
                rx[rxn] = 0;
                char *nl;
                while ((nl = strchr(rx, '\n'))) {
                    *nl = 0;
                    printf("> %s\n", rx);
                    app_on_line(rx);
                    size_t used = (size_t)(nl - rx) + 1;
                    memmove(rx, nl + 1, rxn - used + 1);
                    rxn -= used;
                }
                if (rxn >= sizeof rx - 1) rxn = 0;
            }
        }
        app_poll();
        sim_check_feedback(app_ready());
        usleep(500);
    }
}

int main(int argc, char **argv)
{
    int port = 0;
    sim_init();
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--tcp") && i + 1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--n")) g_sim.ntype = true;
    }
    return port ? run_tcp(port) : run_script();
}
