// Runtime V71: BCM2711 GPIO register probe (GPFSEL + 2711 PUP_PDN).
// Sources/Support/kernel_gpio.c
//
// ARM low-peripheral GPIO base 0xFE200000. Boot/shell only — never from
// CNTP IRQ or secondary workers (HDMI blit lesson). Read-only: do not
// write GPFSEL or PUP_PDN. UART pin mux stays in GPIO.swift.

#include "include/Support.h"
#include <stdint.h>

#define GPIO_BASE 0xFE200000UL
#define G32(off) (*(volatile uint32_t *)(GPIO_BASE + (unsigned long)(off)))

#define GPFSEL0 0x00U
#define GPFSEL1 0x04U
#define PUP_PDN0 0xE4U  // GPIO_PUP_PDN_CNTRL_REG0, BCM2711-only

#define FSEL_MASK 7U
#define FSEL_ALT0 4U

static int gpio_probed;
static int gpio_ok_val;
static unsigned int gpio_fsel_val;
static unsigned int gpio_pup_val;
static unsigned int gpio_uart_val;

int kernel_gpio_selftest(void) {
    if (gpio_probed) return gpio_ok_val;
    gpio_probed = 1;
    gpio_ok_val = 0;
    gpio_fsel_val = 0;
    gpio_pup_val = 0;
    gpio_uart_val = 0;

    unsigned int fsel0 = G32(GPFSEL0);
    unsigned int fsel1 = G32(GPFSEL1);
    unsigned int pup_a = G32(PUP_PDN0);
    unsigned int pup_b = G32(PUP_PDN0);

    gpio_fsel_val = fsel0;
    gpio_pup_val = pup_a;

    // Reserved GPFSEL0[31:30] are RAZ on BCM GPIO. All-ones is a dead bus.
    if ((fsel0 & 0xC0000000U) != 0U) return 0;
    if (pup_a == 0xFFFFFFFFU || pup_a != pup_b) return 0;

    // Non-vacuity: UART0 mux (GPIO14/15 ALT0) that uartPinsInit programmed.
    unsigned int fsel14 = (fsel1 >> 12) & FSEL_MASK;
    unsigned int fsel15 = (fsel1 >> 15) & FSEL_MASK;
    if (fsel14 == FSEL_ALT0 && fsel15 == FSEL_ALT0) {
        gpio_uart_val = 1;
    }
    if (gpio_uart_val == 0U) return 0;

    gpio_ok_val = 1;
    return 1;
}

int          kernel_gpio_ok(void)   { return gpio_ok_val;   }
unsigned int kernel_gpio_fsel(void) { return gpio_fsel_val; }
unsigned int kernel_gpio_pup(void)  { return gpio_pup_val;  }
unsigned int kernel_gpio_uart(void) { return gpio_uart_val; }
