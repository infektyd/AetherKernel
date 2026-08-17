// Runtime V91: GPIO42 output + GPLEV readback after GENET.
// Kernel ACT LED pin (already owned by ledInit). SET then CLR; fail-closed
// if the pad level does not follow. Restore GPFSEL4. No UART mux. No PUP.
// No EL0 enter (I-abort esr=0xbf000002 elr=0x100002000 after GENET DMA).
// No boot event emit. No jumper.

#include "Support.h"
#include <stdint.h>

#define GPIO_BASE 0xFE200000UL
#define G32(off) (*(volatile uint32_t *)(GPIO_BASE + (unsigned long)(off)))

#define GPFSEL4 0x10U
#define GPSET1  0x20U
#define GPCLR1  0x2CU
#define GPLEV1  0x38U /* not 0x34 (GPLEV0); V91 metal set=0 was this miss */

#define GPIO2_PIN        42U
#define GPIO2_FSEL_SHIFT 6U
#define GPIO2_LEV_BIT    10U
#define FSEL_MASK        7U
#define FSEL_OUTPUT      1U

static int gpio2_probed;
static int gpio2_ok_val;
static unsigned int gpio2_pin_val;
static unsigned int gpio2_set_val;
static unsigned int gpio2_clr_val;

static unsigned int gpio2_lev_bit(void) {
    unsigned int lev = 0u;
    unsigned int i;
    for (i = 0u; i < 8u; i++) {
        lev = G32(GPLEV1);
    }
    return (lev >> GPIO2_LEV_BIT) & 1u;
}

int kernel_gpio2_selftest(void) {
    if (gpio2_probed) return gpio2_ok_val;
    gpio2_probed = 1;
    gpio2_ok_val = 0;
    gpio2_pin_val = GPIO2_PIN;
    gpio2_set_val = 0;
    gpio2_clr_val = 0;

    unsigned int saved = G32(GPFSEL4);
    if (saved == 0xFFFFFFFFU) return 0;

    unsigned int next = saved;
    next &= ~(FSEL_MASK << GPIO2_FSEL_SHIFT);
    next |= (FSEL_OUTPUT << GPIO2_FSEL_SHIFT);
    G32(GPFSEL4) = next;

    G32(GPSET1) = (1U << GPIO2_LEV_BIT);
    if (gpio2_lev_bit() == 1u) gpio2_set_val = 1;

    G32(GPCLR1) = (1U << GPIO2_LEV_BIT);
    if (gpio2_lev_bit() == 0u) gpio2_clr_val = 1;

    G32(GPFSEL4) = saved;
    G32(GPSET1) = (1U << GPIO2_LEV_BIT);

    if (gpio2_set_val == 1u && gpio2_clr_val == 1u) {
        gpio2_ok_val = 1;
        return 1;
    }
    return 0;
}

int          kernel_gpio2_ok(void)  { return gpio2_ok_val; }
unsigned int kernel_gpio2_pin(void) { return gpio2_pin_val; }
unsigned int kernel_gpio2_set(void) { return gpio2_set_val; }
unsigned int kernel_gpio2_clr(void) { return gpio2_clr_val; }
