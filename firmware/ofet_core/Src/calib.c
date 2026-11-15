#include "calib.h"

calib_t g_calib;

void calib_defaults(void)
{
    static const float rf[OFET_N_RANGES] = {
        1e3f, 1e4f, 1e5f, 1e6f, 1e7f,
        1e6f + 1e4f + 1e6f * 1e4f / 100.0f,     /* Т-цепь, R1 = 1 МОм: около 101 МОм */
        1e7f + 1e4f + 1e7f * 1e4f / 100.0f,     /* Т-цепь, R1 = 10 МОм: около 1,01 ГОм */
    };
    for (int ch = 0; ch < 2; ++ch) {
        g_calib.a[ch] = -OFET_UOUT_SPAN / 32768.0f;
        g_calib.b[ch] = OFET_UOUT_SPAN;
    }
    for (int i = 0; i < OFET_N_RANGES; ++i)
        g_calib.rf[i] = rf[i];
}
