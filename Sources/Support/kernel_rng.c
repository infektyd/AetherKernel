// Runtime V100: BCM2711 RNG200 word after GENET.
// Enable like Linux bcm2711_rng200_init, wait TOTAL_BIT_COUNT>16 and
// FIFO_COUNT>0, then read FIFO_DATA. Fail-closed on warmup/fifo timeout.
// Honest token reports the raw word (may be 0). No EL0 enter
// (I-abort esr=0xbf000002 elr=0x100002000 after GENET DMA).
// No boot event emit. Does not arm or reset the watchdog.

#include "Support.h"
#include <stdint.h>

#define RNG200_BASE 0xFE104000UL
#define RNG_CTRL                    0x00U
#define RNG_CTRL_RBGEN_MASK         0x00001FFFU
#define RNG_CTRL_RBGEN_ENABLE       0x00000001U
#define RNG_CTRL_DIV_SHIFT          13U
#define RNG_TOTAL_BIT_COUNT         0x0CU
#define RNG_TOTAL_BIT_THRESHOLD     0x10U
#define RNG_FIFO_DATA               0x20U
#define RNG_FIFO_COUNT              0x24U
#define RNG_FIFO_COUNT_MASK         0x000000FFU
#define RNG_FIFO_THRESHOLD_SHIFT    8U

#define RNG_POLL_LIMIT 2000000U

static int rng_probed;
static int rng_ok_val;
static int rng_ready_val;
static unsigned int rng_data_val;

static unsigned int rng_read(unsigned int off) {
    return mmio_read32(RNG200_BASE + (unsigned long)off);
}

static void rng_write(unsigned int off, unsigned int val) {
    mmio_write32(RNG200_BASE + (unsigned long)off, val);
}

static void rng_enable(void) {
    if ((rng_read(RNG_CTRL) & RNG_CTRL_RBGEN_MASK) != 0U) return;
    rng_write(RNG_TOTAL_BIT_THRESHOLD, 0x40000U);
    rng_write(RNG_FIFO_COUNT, 2U << RNG_FIFO_THRESHOLD_SHIFT);
    rng_write(RNG_CTRL, (0x3U << RNG_CTRL_DIV_SHIFT) | RNG_CTRL_RBGEN_MASK);
}

int kernel_rng_selftest(void) {
    if (rng_probed) return rng_ok_val;
    rng_probed = 1;
    rng_ok_val = 0;
    rng_ready_val = 0;
    rng_data_val = 0;

    rng_enable();

    unsigned int s = RNG_POLL_LIMIT;
    while (s--) {
        if (rng_read(RNG_TOTAL_BIT_COUNT) > 16U) break;
        __asm__ volatile("nop");
    }
    if (s == 0xFFFFFFFFU) return 0;

    s = RNG_POLL_LIMIT;
    unsigned int words = 0;
    while (s--) {
        words = rng_read(RNG_FIFO_COUNT) & RNG_FIFO_COUNT_MASK;
        if (words != 0U) break;
        __asm__ volatile("nop");
    }
    if (words == 0U) return 0;

    rng_data_val = rng_read(RNG_FIFO_DATA);
    rng_ready_val = 1;
    rng_ok_val = 1;
    return 1;
}

int          kernel_rng_ok(void)    { return rng_ok_val; }
int          kernel_rng_ready(void) { return rng_ready_val; }
unsigned int kernel_rng_data(void)  { return rng_data_val; }
