/* Тонкий слой железа. Эти функции реализует платформа:
 * на STM32 - файл с HAL (firmware/ofet_fw), на ПК - симулятор (firmware/host_sim). */
#ifndef OFET_HW_H
#define OFET_HW_H

#include <stdbool.h>
#include <stdint.h>

uint32_t hw_millis(void);

/* I2C: 7-битный адрес, данные. true = успех (ACK). */
bool hw_i2c_write(uint8_t addr7, const uint8_t *data, uint16_t len);

/* Коммутатор транзисторов: канал 1..6 (см. CLAUDE.md, п. 3.5) */
void hw_sel_line(int ch, bool on);

/* Реле диапазонов: range 1..7, coil 0 = SET (для обычных реле: обмотка), 1 = RESET */
void hw_range_coil(int range, int coil, bool on);

/* Питание формирователей ±18 В */
void hw_pwr18(bool on);

/* АЦП: начать усреднение Uтиа и Uсм за window_ms; poll возвращает true, когда окно закончилось
 * (значения в вольтах). Повторный begin перезапускает окно. */
void hw_adc_begin(uint32_t window_ms);
bool hw_adc_poll(float *utia, float *ubias);

/* Настройки сети платы (хранятся во flash, применяются после перезапуска платы).
 * mode 0 = авто (DHCP от роутера, без роутера адрес 169.254.x.x), 1 = постоянный адрес. */
typedef struct {
    uint8_t mode;
    uint8_t ip[4], mask[4], gw[4];
} net_cfg_t;

void hw_net_get_config(net_cfg_t *cfg);
bool hw_net_set_config(const net_cfg_t *cfg);   /* false = не удалось сохранить */
void hw_net_current_ip(uint8_t ip[4]);          /* адрес, который плата использует сейчас */

/* Отправить строку на ПК (платформа добавляет LF). Если ПК не подключён - ничего не делать. */
void hw_send_line(const char *line);

#endif
