/* Два ЦАП AD5693R по I2C. */
#ifndef DAC_H
#define DAC_H

#include <stdbool.h>
#include <stdint.h>

typedef enum { DAC_GATE = 0, DAC_DRAIN = 1 } dac_ch_t;

#define DAC_ADDR_GATE   0x4C    /* A0 = GND, 7-битный адрес */
#define DAC_ADDR_DRAIN  0x4E    /* A0 = VDD */

/* Регистр управления обоих ЦАП: Gain = 2, внутренний опорник включён. */
bool dac_init(void);

/* Код для заданного Uвых по калибровке, ограничен 0..65535 */
uint16_t dac_code_for(dac_ch_t ch, float uout);

/* Записать Uвых в канал. false = нет ответа по I2C. */
bool dac_set_uout(dac_ch_t ch, float uout);

#endif
