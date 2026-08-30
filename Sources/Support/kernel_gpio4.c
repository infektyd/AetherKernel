// Runtime V140: BCM2711 GPIO42 rising-edge detect after GENET.
// Fail-closed GPREN1/GPEDS1: event is clear, then SET produces GPEDS.
// Uses the kernel ACT LED pin already proven by V91. Restore leftover
// GPREN1 + GPFSEL4. No UART GPIO14/15 FSEL or pull writes. No PUP.
// No pin-mux clone. No PWM output claim. No EL0 enter (I-abort)
// esr=0xbf000002 elr=0x100002000 after GENET DMA). No boot event emit.

#include "Support.h"
#include <stdint.h>

#define GPIO_BASE 0xFE200000UL
#define G32(off) (*(volatile uint32_t *)(GPIO_BASE + (unsigned long)(off)))

#define GPFSEL4 0x10U
#define GPSET1  0x20U
#define GPCLR1  0x2CU
#define GPEDS1  0x44U /* pins 32..53; 0x40 is GPEDS0 */
#define GPREN1  0x50U /* pins 32..53; 0x4C is GPREN0 */

#define GPIO4_PIN        42U
#define GPIO4_FSEL_SHIFT 6U /* (42-40)*3 */
#define GPIO4_LEV_BIT    10U /* 42-32 */
#define FSEL_MASK        7U
#define FSEL_OUTPUT      1U

static int gpio4_probed;
static int gpio4_ok_val;
static unsigned int gpio4_pin_val;
static unsigned int gpio4_rise_val;
static unsigned int gpio4_restore_val;

int kernel_gpio4_selftest(void) {
    if (gpio4_probed) return gpio4_ok_val;
    gpio4_probed = 1;
    gpio4_ok_val = 0;
    gpio4_pin_val = GPIO4_PIN;
    gpio4_rise_val = 0;
    gpio4_restore_val = 0;

    if (kernel_gpio2_selftest() == 0) return 0;

    unsigned int saved_fsel = G32(GPFSEL4);
    unsigned int saved_ren = G32(GPREN1);
    if (saved_fsel == 0xFFFFFFFFU || saved_ren == 0xFFFFFFFFU) return 0;

    unsigned int next = saved_fsel;
    next &= ~(FSEL_MASK << GPIO4_FSEL_SHIFT);
    next |= (FSEL_OUTPUT << GPIO4_FSEL_SHIFT);
    G32(GPFSEL4) = next;

    unsigned int bit = 1U << GPIO4_LEV_BIT;

    G32(GPCLR1) = bit;
    G32(GPREN1) = saved_ren & ~bit;
    G32(GPEDS1) = bit;

    unsigned int i;
    unsigned int eds = 0u;
    for (i = 0u; i < 8u; i++) {
        eds = G32(GPEDS1);
    }
    if ((eds & bit) != 0u) {
        G32(GPREN1) = saved_ren;
        G32(GPFSEL4) = saved_fsel;
        return 0;
    }

    G32(GPREN1) = (saved_ren & ~bit) | bit;
    G32(GPSET1) = bit;

    for (i = 0u; i < 64u; i++) {
        eds = G32(GPEDS1);
        if ((eds & bit) != 0u) {
            gpio4_rise_val = 1;
            break;
        }
    }

    G32(GPEDS1) = bit;
    G32(GPREN1) = saved_ren;
    G32(GPFSEL4) = saved_fsel;
    G32(GPSET1) = bit;

    if (G32(GPREN1) == saved_ren && G32(GPFSEL4) == saved_fsel) {
        gpio4_restore_val = 1;
    }

    if (gpio4_rise_val == 1U && gpio4_restore_val == 1U) {
        gpio4_ok_val = 1;
        return 1;
    }
    return 0;
}

int          kernel_gpio4_ok(void)      { return gpio4_ok_val; }
unsigned int kernel_gpio4_pin(void)     { return gpio4_pin_val; }
unsigned int kernel_gpio4_rise(void)    { return gpio4_rise_val; }
unsigned int kernel_gpio4_restore(void) { return gpio4_restore_val; }
