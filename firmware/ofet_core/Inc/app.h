/* Верхний уровень прошивки OFET: старт, очередь команд, безопасность.
 *
 * Платформа вызывает:
 *   app_init()            один раз после инициализации периферии;
 *   app_poll()            в главном цикле как можно чаще;
 *   app_on_line(s)        на каждую принятую строку (без LF);
 *   app_on_connect() / app_on_disconnect()  при подключении и отключении ПК.
 */
#ifndef APP_H
#define APP_H

#include <stdbool.h>

void app_init(void);
void app_poll(void);
void app_on_line(const char *line);
void app_on_connect(void);
void app_on_disconnect(void);
bool app_ready(void);

/* Для модуля sweep */
int   app_set_voltages(float ugs, float uds);   /* 0 или код ошибки протокола (2, 3) */
void  app_safe_state(void);
float app_ugs(void);
float app_uds(void);

#endif
