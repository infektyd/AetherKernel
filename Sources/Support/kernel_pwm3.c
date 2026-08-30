// Runtime V139: BCM2711 PWM GPIO12 ALT0 pin-mux after GENET.
// Fail-closed GPFSEL1 readback of ALT0, then leftover restore.
// UART GPIO14/15 FSEL bits must stay unchanged. No PUP/PDN writes
// (do not touch REG0). No PWM CTL/DAT (V93 already poked those).
// No output claim. No EL0 enter (I-abort esr=0xbf000002 elr=0x100002000
// after GENET DMA). No boot event emit.

#include "Support.h"
#include <stdint.h>

#define GPIO_BASE 0xFE200000UL
#define G32(off) (*(volatile uint32_t *)(GPIO_BASE + (unsigned long)(off)))

#define GPFSEL1 0x04U /* GPIO10..19 */

#define PWM3_PIN         12U
#define PWM3_FSEL_SHIFT  6U /* (12-10)*3 */
#define FSEL_MASK        7U
#define FSEL_INPUT       0U
#define FSEL_ALT0        4U
#define UART_FSEL_MASK   (0x3FU << 12) /* GPIO14+15 in GPFSEL1 */

static int pwm3_probed;
static int pwm3_ok_val;
static unsigned int pwm3_pin_val;
static unsigned int pwm3_alt_val;
static unsigned int pwm3_restore_val;

int kernel_pwm3_selftest(void) {
    if (pwm3_probed) return pwm3_ok_val;
    pwm3_probed = 1;
    pwm3_ok_val = 0;
    pwm3_pin_val = PWM3_PIN;
    pwm3_alt_val = 0;
    pwm3_restore_val = 0;

    if (kernel_pwm2_selftest() == 0) return 0;

    unsigned int saved = G32(GPFSEL1);
    if (saved == 0xFFFFFFFFU) return 0;

    unsigned int uart_bits = saved & UART_FSEL_MASK;
    unsigned int fsel12 = (saved >> PWM3_FSEL_SHIFT) & FSEL_MASK;

    /* Non-vacuous: if leftover is already ALT0, prove the field can move. */
    if (fsel12 == FSEL_ALT0) {
        unsigned int tmp = saved & ~(FSEL_MASK << PWM3_FSEL_SHIFT);
        tmp |= (FSEL_INPUT << PWM3_FSEL_SHIFT);
        G32(GPFSEL1) = tmp;
        unsigned int rb = G32(GPFSEL1);
        if (((rb >> PWM3_FSEL_SHIFT) & FSEL_MASK) != FSEL_INPUT ||
            (rb & UART_FSEL_MASK) != uart_bits) {
            G32(GPFSEL1) = saved;
            return 0;
        }
    }

    unsigned int cur = G32(GPFSEL1);
    unsigned int next = cur;
    next &= ~(FSEL_MASK << PWM3_FSEL_SHIFT);
    next |= (FSEL_ALT0 << PWM3_FSEL_SHIFT);
    G32(GPFSEL1) = next;

    unsigned int muxed = G32(GPFSEL1);
    if (((muxed >> PWM3_FSEL_SHIFT) & FSEL_MASK) == FSEL_ALT0 &&
        (muxed & UART_FSEL_MASK) == uart_bits) {
        pwm3_alt_val = 1;
    }

    G32(GPFSEL1) = saved;
    if (G32(GPFSEL1) == saved) pwm3_restore_val = 1;

    if (pwm3_alt_val == 1U && pwm3_restore_val == 1U) {
        pwm3_ok_val = 1;
        return 1;
    }
    return 0;
}

int          kernel_pwm3_ok(void)      { return pwm3_ok_val; }
unsigned int kernel_pwm3_pin(void)     { return pwm3_pin_val; }
unsigned int kernel_pwm3_alt(void)     { return pwm3_alt_val; }
unsigned int kernel_pwm3_restore(void) { return pwm3_restore_val; }
