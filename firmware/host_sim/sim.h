#ifndef SIM_H
#define SIM_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t now;           /* виртуальное время, мс (режим сценария) */
    bool realtime;
    bool pwr18;
    bool i2c_fail;
    bool ntype;
    bool big_current;
    float ubias_true;
    int violations;
} sim_t;

extern sim_t g_sim;

void  sim_init(void);
void  sim_violation(const char *fmt, const char *a, int b);
void  sim_check_feedback(bool booted);
int   sim_transistor(void);
float sim_ugs(void);
float sim_uds(void);

#endif
