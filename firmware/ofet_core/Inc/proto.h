/* Разбор строк команд и форматирование чисел (без printf для float). */
#ifndef PROTO_H
#define PROTO_H

#include <stdbool.h>

#define PROTO_MAX_FIELDS 16

/* Разбить строку на поля по ';' (строка изменяется). Возвращает число полей. */
int proto_split(char *line, char *fields[], int max_fields);

/* Строгий разбор числа: всё поле должно быть числом. */
bool proto_float(const char *s, float *out);
bool proto_int(const char *s, long *out);

/* "-1.2345" (4 знака после точки) */
void proto_fmt_u(float v, char *buf);
/* "-1.2345E-11" */
void proto_fmt_e(float v, char *buf);

#endif
