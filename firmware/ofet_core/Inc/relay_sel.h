/* Коммутатор транзисторов (CLAUDE.md, п. 3.5). Неблокирующий.
 * OFET_SEL_BISTABLE = 1: бистабильные реле, импульсы SET/RESET по очереди.
 * OFET_SEL_BISTABLE = 0: моностабильные реле, на линии SET держится уровень (B = включено, A = выключено). */
#ifndef RELAY_SEL_H
#define RELAY_SEL_H

#include <stdbool.h>

void relay_sel_init(void);          /* все группы в положение A (RESET или обмотки обесточены) */
bool relay_sel_select(int n);       /* транзистор 1..5; false = неверный номер */
bool relay_sel_busy(void);          /* импульсы или пауза после них ещё идут */
void relay_sel_poll(void);

#endif
