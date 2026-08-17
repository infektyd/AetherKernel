// Runtime V94: BCM2711 GPIO26 PUP_PDN write+readback after GENET.
// REG1 only (pins 16-31). Do not touch REG0 (UART GPIO14/15).
// Control-register readback, not pad voltage. Restore. No GPFSEL/SET/CLR.
// No EL0 enter (I-abort esr=0xbf000002 elr=0x100002000 after GENET DMA).
// No boot event emit. No jumper.

#include "Support.h"
#include <stdint.h>

#define GPIO_BASE 0xFE200000UL
#define G32(off) (*(volatile uint32_t *)(GPIO_BASE + (unsigned long)(off)))

#define PUP_PDN1 0xE8U /* GPIO_PUP_PDN_CNTRL_REG1, pins 16-31 */

#define GPIO3_PIN        26U
#define GPIO3_SHIFT      20U /* (26-16)*2 */
#define GPIO3_MASK       3U
#define GPIO3_PULL_UP    1U
#define GPIO3_PULL_DOWN  2U

static int gpio3_probed;
static int gpio3_ok_val;
static unsigned int gpio3_pin_val;
static unsigned int gpio3_up_val;
static unsigned int gpio3_dn_val;

static unsigned int gpio3_field(unsigned int reg) {
    return (reg >> GPIO3_SHIFT) & GPIO3_MASK;
}

int kernel_gpio3_selftest(void) {
    if (gpio3_probed) return gpio3_ok_val;
    gpio3_probed = 1;
    gpio3_ok_val = 0;
    gpio3_pin_val = GPIO3_PIN;
    gpio3_up_val = 0;
    gpio3_dn_val = 0;

    if (kernel_gpio_selftest() == 0) return 0;

    unsigned int saved = G32(PUP_PDN1);
    if (saved == 0xFFFFFFFFU) return 0;

    unsigned int down = saved;
    down &= ~(GPIO3_MASK << GPIO3_SHIFT);
    down |= (GPIO3_PULL_DOWN << GPIO3_SHIFT);
    G32(PUP_PDN1) = down;
    if (gpio3_field(G32(PUP_PDN1)) == GPIO3_PULL_DOWN) gpio3_dn_val = 1;

    unsigned int up = saved;
    up &= ~(GPIO3_MASK << GPIO3_SHIFT);
    up |= (GPIO3_PULL_UP << GPIO3_SHIFT);
    G32(PUP_PDN1) = up;
    if (gpio3_field(G32(PUP_PDN1)) == GPIO3_PULL_UP) gpio3_up_val = 1;

    G32(PUP_PDN1) = saved;

    if (gpio3_up_val == 1U && gpio3_dn_val == 1U) {
        gpio3_ok_val = 1;
        return 1;
    }
    return 0;
}

int          kernel_gpio3_ok(void)  { return gpio3_ok_val; }
unsigned int kernel_gpio3_pin(void) { return gpio3_pin_val; }
unsigned int kernel_gpio3_up(void)  { return gpio3_up_val; }
unsigned int kernel_gpio3_dn(void)  { return gpio3_dn_val; }
