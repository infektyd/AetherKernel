// Runtime V83: one bounded SPI0 byte. DONE is success. loop=1 only if RX==TX.
// No MOSI-MISO jumper expected: loop=0 is honest, not a failure.
// No boot event emit. No EL0 enter after GENET DMA (I-abort landmine).
// GPIO7-11 are muxed to ALT0 for the transfer and restored afterward.

#include "Support.h"
#include <stdint.h>

#define SPI0_BASE 0xFE204000UL
#define GPIO_BASE 0xFE200000UL

#define G32(base, off) (*(volatile uint32_t *)((base) + (unsigned long)(off)))

#define SPI_CS   0x00U
#define SPI_FIFO 0x04U
#define SPI_CLK  0x08U

#define SPI_CS_CLEAR (3U << 4)
#define SPI_CS_TA    (1U << 7)
#define SPI_CS_DONE  (1U << 16)
#define SPI_CS_RXD   (1U << 17)
#define SPI_CS_TXD   (1U << 18)

#define GPFSEL0   0x00U
#define GPFSEL1   0x04U
#define FSEL_MASK 7U
#define FSEL_ALT0 4U

#define SPI2_TX            0x5AU
#define SPI2_CLK_DEFAULT   128U
#define SPI2_TIMEOUT_TICKS 540000UL /* ~10ms @ 54MHz CNTPCT */

static int spi2_probed;
static int spi2_ok_val;
static unsigned int spi2_done_val;
static unsigned int spi2_loop_val;
static unsigned int spi2_rx_val;

static void spi2_fsel_set(unsigned int *reg, unsigned int pin, unsigned int fn) {
    unsigned int shift = (pin % 10U) * 3U;
    *reg &= ~(FSEL_MASK << shift);
    *reg |= (fn << shift);
}

static void spi2_mux_restore(unsigned int fsel0, unsigned int fsel1) {
    G32(GPIO_BASE, GPFSEL0) = fsel0;
    G32(GPIO_BASE, GPFSEL1) = fsel1;
}

static void spi2_mux_spi0(unsigned int *saved0, unsigned int *saved1) {
    unsigned int f0 = G32(GPIO_BASE, GPFSEL0);
    unsigned int f1 = G32(GPIO_BASE, GPFSEL1);
    *saved0 = f0;
    *saved1 = f1;
    spi2_fsel_set(&f0, 7U, FSEL_ALT0);  /* CE1 */
    spi2_fsel_set(&f0, 8U, FSEL_ALT0);  /* CE0 */
    spi2_fsel_set(&f0, 9U, FSEL_ALT0);  /* MISO */
    spi2_fsel_set(&f1, 10U, FSEL_ALT0); /* MOSI */
    spi2_fsel_set(&f1, 11U, FSEL_ALT0); /* SCLK */
    G32(GPIO_BASE, GPFSEL0) = f0;
    G32(GPIO_BASE, GPFSEL1) = f1;
}

int kernel_spi2_selftest(void) {
    if (spi2_probed) return spi2_ok_val;
    spi2_probed = 1;
    spi2_ok_val = 0;
    spi2_done_val = 0;
    spi2_loop_val = 0;
    spi2_rx_val = 0;

    if (kernel_i2c_selftest() == 0 || kernel_i2c_spi() == 0U) return 0;

    unsigned int clk = G32(SPI0_BASE, SPI_CLK);
    if (clk == 0U || clk == 0xFFFFFFFFU) {
        G32(SPI0_BASE, SPI_CLK) = SPI2_CLK_DEFAULT;
    }

    unsigned int saved0 = 0;
    unsigned int saved1 = 0;
    spi2_mux_spi0(&saved0, &saved1);

    G32(SPI0_BASE, SPI_CS) = SPI_CS_CLEAR;
    G32(SPI0_BASE, SPI_CS) = SPI_CS_TA;

    unsigned long start = read_cntpct();
    unsigned int cs = 0;
    int wrote = 0;
    for (;;) {
        cs = G32(SPI0_BASE, SPI_CS);
        if (wrote == 0 && (cs & SPI_CS_TXD) != 0U) {
            G32(SPI0_BASE, SPI_FIFO) = SPI2_TX;
            wrote = 1;
        }
        if ((cs & SPI_CS_DONE) != 0U && wrote != 0) break;
        if ((read_cntpct() - start) >= SPI2_TIMEOUT_TICKS) break;
    }

    if ((cs & SPI_CS_RXD) != 0U) {
        spi2_rx_val = G32(SPI0_BASE, SPI_FIFO) & 0xFFU;
    }

    G32(SPI0_BASE, SPI_CS) = SPI_CS_CLEAR;
    spi2_mux_restore(saved0, saved1);

    if (wrote != 0 && (cs & SPI_CS_DONE) != 0U) {
        spi2_done_val = 1;
        if (spi2_rx_val == SPI2_TX) spi2_loop_val = 1;
        spi2_ok_val = 1;
        return 1;
    }
    return 0;
}

int          kernel_spi2_ok(void)   { return spi2_ok_val; }
unsigned int kernel_spi2_done(void) { return spi2_done_val; }
unsigned int kernel_spi2_loop(void) { return spi2_loop_val; }
unsigned int kernel_spi2_rx(void)   { return spi2_rx_val; }
