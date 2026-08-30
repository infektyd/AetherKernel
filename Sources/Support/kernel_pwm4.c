// Runtime V141: BCM2711 PWM1 channel-1 program after GENET.
// Fail-closed RNG1/DAT1/PWEN1 write+readback on the second PWM block
// (0xFE20C800). Leftover restore. No pin-mux. No output/audio claim.
// No PWM0 writes (V93). No CM_PWM (V93 restored leftover).
// No EL0 enter (I-abort esr=0xbf000002 elr=0x100002000 after GENET DMA).
// No boot event emit.

#include "Support.h"
#include <stdint.h>

#define PWM1_BASE 0xFE20C800UL
#define G32(off) (*(volatile uint32_t *)(PWM1_BASE + (unsigned long)(off)))

#define PWM_CTL  0x00U
#define PWM_RNG1 0x10U
#define PWM_DAT1 0x14U

#define PWM_PWEN1 (1U << 0)
#define PWM_MSEN1 (1U << 7)

#define PWM4_RNG 32U
#define PWM4_DAT 8U

static int pwm4_probed;
static int pwm4_ok_val;
static unsigned int pwm4_pwm1_val;
static unsigned int pwm4_en_val;
static unsigned int pwm4_restore_val;

int kernel_pwm4_selftest(void) {
    if (pwm4_probed) return pwm4_ok_val;
    pwm4_probed = 1;
    pwm4_ok_val = 0;
    pwm4_pwm1_val = 0;
    pwm4_en_val = 0;
    pwm4_restore_val = 0;

    if (kernel_pwm3_selftest() == 0) return 0;

    unsigned int saved_ctl = G32(PWM_CTL);
    unsigned int saved_rng = G32(PWM_RNG1);
    unsigned int saved_dat = G32(PWM_DAT1);
    if (saved_ctl == 0xFFFFFFFFU || saved_rng == 0xFFFFFFFFU || saved_dat == 0xFFFFFFFFU) {
        return 0;
    }
    pwm4_pwm1_val = 1;

    G32(PWM_RNG1) = PWM4_RNG;
    G32(PWM_DAT1) = PWM4_DAT;
    G32(PWM_CTL) = (saved_ctl & ~PWM_PWEN1) | PWM_PWEN1 | PWM_MSEN1;

    unsigned int ctl = G32(PWM_CTL);
    unsigned int rng = G32(PWM_RNG1);
    unsigned int dat = G32(PWM_DAT1);
    if ((ctl & PWM_PWEN1) != 0U && rng == PWM4_RNG && dat == PWM4_DAT) {
        pwm4_en_val = 1;
    }

    G32(PWM_CTL) = saved_ctl;
    G32(PWM_RNG1) = saved_rng;
    G32(PWM_DAT1) = saved_dat;
    if (G32(PWM_CTL) == saved_ctl && G32(PWM_RNG1) == saved_rng && G32(PWM_DAT1) == saved_dat) {
        pwm4_restore_val = 1;
    }

    if (pwm4_pwm1_val == 1U && pwm4_en_val == 1U && pwm4_restore_val == 1U) {
        pwm4_ok_val = 1;
        return 1;
    }
    return 0;
}

int          kernel_pwm4_ok(void)      { return pwm4_ok_val; }
unsigned int kernel_pwm4_pwm1(void)    { return pwm4_pwm1_val; }
unsigned int kernel_pwm4_en(void)      { return pwm4_en_val; }
unsigned int kernel_pwm4_restore(void) { return pwm4_restore_val; }
