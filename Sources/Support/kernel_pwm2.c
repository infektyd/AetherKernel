// Runtime V93: BCM2711 PWM clock enable + CTL poke after GENET.
// Enable CM_PWM from OSC, write PWM0 RNG1/DAT1/PWEN1, read back, restore.
// No pin-mux (do not claim PWM output). No PWM1 writes. No EL0 enter
// (I-abort esr=0xbf000002 elr=0x100002000 after GENET DMA).
// No boot event emit.

#include "Support.h"
#include <stdint.h>

#define PWM0_BASE 0xFE20C000UL
#define CM_PWMCTL 0xFE1010A0UL
#define CM_PWMDIV 0xFE1010A4UL

#define G32(base, off) (*(volatile uint32_t *)((base) + (unsigned long)(off)))
#define CM32(addr) (*(volatile uint32_t *)(addr))

#define PWM_CTL  0x00U
#define PWM_RNG1 0x10U
#define PWM_DAT1 0x14U

#define PWM_PWEN1 (1U << 0)
#define PWM_MSEN1 (1U << 7)

#define CM_PASSWD 0x5A000000U
#define CM_SRC_OSC 1U
#define CM_ENAB   (1U << 4)
#define CM_BUSY   (1U << 7)

#define PWM2_DIVI       50U
#define PWM2_WAIT_TICKS 108000UL /* ~2ms @ 54MHz CNTPCT */

static int pwm2_probed;
static int pwm2_ok_val;
static unsigned int pwm2_clk_val;
static unsigned int pwm2_en_val;

static int pwm2_wait_busy(unsigned int want) {
    unsigned long start = read_cntpct();
    while ((read_cntpct() - start) < PWM2_WAIT_TICKS) {
        unsigned int ctl = CM32(CM_PWMCTL);
        if (ctl == 0xFFFFFFFFU) return 0;
        if (want) {
            if ((ctl & CM_BUSY) != 0U) return 1;
        } else if ((ctl & CM_BUSY) == 0U) {
            return 1;
        }
    }
    return 0;
}

int kernel_pwm2_selftest(void) {
    if (pwm2_probed) return pwm2_ok_val;
    pwm2_probed = 1;
    pwm2_ok_val = 0;
    pwm2_clk_val = 0;
    pwm2_en_val = 0;

    if (kernel_pwm_selftest() == 0) return 0;

    unsigned int saved_cm_ctl = CM32(CM_PWMCTL);
    unsigned int saved_cm_div = CM32(CM_PWMDIV);
    unsigned int saved_ctl = G32(PWM0_BASE, PWM_CTL);
    unsigned int saved_rng = G32(PWM0_BASE, PWM_RNG1);
    unsigned int saved_dat = G32(PWM0_BASE, PWM_DAT1);
    if (saved_cm_ctl == 0xFFFFFFFFU || saved_cm_div == 0xFFFFFFFFU) return 0;
    if (saved_ctl == 0xFFFFFFFFU || saved_rng == 0xFFFFFFFFU) return 0;

    CM32(CM_PWMCTL) = CM_PASSWD | (saved_cm_ctl & 0x0FU);
    if (pwm2_wait_busy(0) == 0) {
        CM32(CM_PWMCTL) = CM_PASSWD | (saved_cm_ctl & 0xFFU);
        return 0;
    }

    CM32(CM_PWMDIV) = CM_PASSWD | (PWM2_DIVI << 12);
    CM32(CM_PWMCTL) = CM_PASSWD | CM_ENAB | CM_SRC_OSC;
    if (pwm2_wait_busy(1) != 0) pwm2_clk_val = 1;

    if (pwm2_clk_val == 1U) {
        G32(PWM0_BASE, PWM_RNG1) = 32U;
        G32(PWM0_BASE, PWM_DAT1) = 16U;
        G32(PWM0_BASE, PWM_CTL) = PWM_PWEN1 | PWM_MSEN1;
        unsigned int ctl = G32(PWM0_BASE, PWM_CTL);
        unsigned int rng = G32(PWM0_BASE, PWM_RNG1);
        if ((ctl & PWM_PWEN1) != 0U && rng == 32U) pwm2_en_val = 1;
    }

    G32(PWM0_BASE, PWM_CTL) = saved_ctl;
    G32(PWM0_BASE, PWM_RNG1) = saved_rng;
    G32(PWM0_BASE, PWM_DAT1) = saved_dat;

    CM32(CM_PWMCTL) = CM_PASSWD | (saved_cm_ctl & 0x0FU);
    (void)pwm2_wait_busy(0);
    CM32(CM_PWMDIV) = CM_PASSWD | (saved_cm_div & 0x00FFFFFFU);
    CM32(CM_PWMCTL) = CM_PASSWD | (saved_cm_ctl & 0xFFU);

    if (pwm2_clk_val == 1U && pwm2_en_val == 1U) {
        pwm2_ok_val = 1;
        return 1;
    }
    return 0;
}

int          kernel_pwm2_ok(void)  { return pwm2_ok_val; }
unsigned int kernel_pwm2_clk(void) { return pwm2_clk_val; }
unsigned int kernel_pwm2_en(void)  { return pwm2_en_val; }
