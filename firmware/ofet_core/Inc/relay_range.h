/* Реле диапазонов ТИА: моностабильные или бистабильные (OFET_RANGE_BISTABLE). Неблокирующий.
 * Моностабильные: нужное реле держится под напряжением. Бистабильные: импульс SET / RESET.
 * Обратная связь не размыкается: сначала замыкается новое реле, потом размыкается старое. */
#ifndef RELAY_RANGE_H
#define RELAY_RANGE_H

#include <stdbool.h>

void relay_range_init(void);        /* диапазон 1, остальные разомкнуты */
void relay_range_set(int r);        /* 1..7 */
int  relay_range_get(void);         /* текущий (целевой) диапазон */
bool relay_range_busy(void);
void relay_range_poll(void);

#endif
