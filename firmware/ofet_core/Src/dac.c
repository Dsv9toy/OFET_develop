#include "dac.h"

#include <math.h>

#include "calib.h"
#include "ofet_hw.h"

#define CMD_WRITE_DAC_INPUT 0x30
#define CMD_WRITE_CONTROL   0x40
#define CTRL_GAIN2          (1u << 11)  /* D11 = GAIN; D12 = REF (0 = внутренний вкл.); D14-D13 = 0: работа */

static uint8_t addr_of(dac_ch_t ch)
{
    return ch == DAC_GATE ? DAC_ADDR_GATE : DAC_ADDR_DRAIN;
}

static bool write16(dac_ch_t ch, uint8_t cmd, uint16_t data)
{
    uint8_t buf[3] = { cmd, (uint8_t)(data >> 8), (uint8_t)(data & 0xFF) };
    return hw_i2c_write(addr_of(ch), buf, 3);
}

bool dac_init(void)
{
    bool ok = write16(DAC_GATE, CMD_WRITE_CONTROL, CTRL_GAIN2);
    ok = write16(DAC_DRAIN, CMD_WRITE_CONTROL, CTRL_GAIN2) && ok;
    return ok;
}

uint16_t dac_code_for(dac_ch_t ch, float uout)
{
    float d = (uout - g_calib.b[ch]) / g_calib.a[ch];
    if (d < 0.0f) d = 0.0f;
    if (d > 65535.0f) d = 65535.0f;
    return (uint16_t)lroundf(d);
}

bool dac_set_uout(dac_ch_t ch, float uout)
{
    return write16(ch, CMD_WRITE_DAC_INPUT, dac_code_for(ch, uout));
}
