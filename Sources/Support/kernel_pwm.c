// Runtime V81: BCM2711 PWM0 + PWM1 register probe.
// Read-only. No extra hardware. No boot event emit. No pin-mux or CTL writes.
// Do not enter EL0 here: a second EL0 enter after GENET DMA I-aborts
// (esr=0xbf000002 elr=0x100002000) and watchdog-resets.

#include "Support.h"
#include <stdint.h>

#define PWM0_BASE 0xFE20C000UL
#define PWM1_BASE 0xFE20C800UL

#define G32(base, off) (*(volatile uint32_t *)((base) + (unsigned long)(off)))

#define PWM_CTL 0x00U
#define PWM_STA 0x04U

#define PWM_STA_EMPT1 (1U << 1)

static int pwm_probed;
static int pwm_ok_val;
static unsigned int pwm_ctl_val;
static unsigned int pwm_sta_val;
static unsigned int pwm_pwm1_val;

int kernel_pwm_selftest(void) {
    if (pwm_probed) return pwm_ok_val;
    pwm_probed = 1;
    pwm_ok_val = 0;
    pwm_ctl_val = 0;
    pwm_sta_val = 0;
    pwm_pwm1_val = 0;

    unsigned int c0 = G32(PWM0_BASE, PWM_CTL);
    unsigned int c1 = G32(PWM0_BASE, PWM_CTL);
    unsigned int s0 = G32(PWM0_BASE, PWM_STA);
    unsigned int s1 = G32(PWM0_BASE, PWM_STA);
    unsigned int p0 = G32(PWM1_BASE, PWM_CTL);
    unsigned int p1 = G32(PWM1_BASE, PWM_CTL);

    pwm_ctl_val = c0;
    pwm_sta_val = s0;

    // Dead AXI: all-ones. Unstable read: not a live block.
    if (c0 == 0xFFFFFFFFU || c0 != c1) return 0;
    if (s0 == 0xFFFFFFFFU || s0 != s1) return 0;
    if (p0 == 0xFFFFFFFFU || p0 != p1) return 0;

    // PWM CTL is a 16-bit field; high half is RAZ on a live block.
    if ((c0 & 0xFFFF0000U) != 0U) return 0;
    if ((p0 & 0xFFFF0000U) != 0U) return 0;
    // STA bits [31:13] reserved RAZ. Idle FIFO empty is non-vacuous.
    if ((s0 & 0xFFFFE000U) != 0U) return 0;
    if ((s0 & PWM_STA_EMPT1) == 0U) return 0;

    pwm_pwm1_val = 1;
    pwm_ok_val = 1;
    return 1;
}

int          kernel_pwm_ok(void)   { return pwm_ok_val; }
unsigned int kernel_pwm_ctl(void)  { return pwm_ctl_val; }
unsigned int kernel_pwm_sta(void)  { return pwm_sta_val; }
unsigned int kernel_pwm_pwm1(void) { return pwm_pwm1_val; }
