/* Коммутатор транзисторов на бистабильных реле (CLAUDE.md, п. 3.5). Неблокирующий. */
#ifndef RELAY_SEL_H
#define RELAY_SEL_H

#include <stdbool.h>

void relay_sel_init(void);          /* поставить в очередь RESET всех трёх групп */
bool relay_sel_select(int n);       /* транзистор 1..5; false = неверный номер */
bool relay_sel_busy(void);          /* импульсы или пауза после них ещё идут */
void relay_sel_poll(void);

#endif
