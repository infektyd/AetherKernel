// Runtime V80: BCM2711 BSC1 + SPI0 register probe.
// Read-only. No extra hardware. No boot event emit. No pin-mux writes.

#include "Support.h"
#include <stdint.h>

#define BSC1_BASE 0xFE804000UL
#define SPI0_BASE 0xFE204000UL

#define G32(base, off) (*(volatile uint32_t *)((base) + (unsigned long)(off)))

#define BSC_C   0x00U
#define BSC_DIV 0x14U
#define SPI_CS  0x00U
#define SPI_CLK 0x08U

static int i2c_probed;
static int i2c_ok_val;
static unsigned int i2c_bsc_val;
static unsigned int i2c_div_val;
static unsigned int i2c_spi_val;

int kernel_i2c_selftest(void) {
    if (i2c_probed) return i2c_ok_val;
    i2c_probed = 1;
    i2c_ok_val = 0;
    i2c_bsc_val = 0;
    i2c_div_val = 0;
    i2c_spi_val = 0;

    unsigned int c0 = G32(BSC1_BASE, BSC_C);
    unsigned int c1 = G32(BSC1_BASE, BSC_C);
    unsigned int d0 = G32(BSC1_BASE, BSC_DIV);
    unsigned int d1 = G32(BSC1_BASE, BSC_DIV);
    unsigned int s0 = G32(SPI0_BASE, SPI_CS);
    unsigned int s1 = G32(SPI0_BASE, SPI_CS);
    unsigned int k0 = G32(SPI0_BASE, SPI_CLK);
    unsigned int k1 = G32(SPI0_BASE, SPI_CLK);

    i2c_div_val = d0;

    // Dead AXI: all-ones. Unstable read: not a live block.
    if (c0 == 0xFFFFFFFFU || c0 != c1) return 0;
    if (d0 == 0xFFFFFFFFU || d0 != d1) return 0;
    if (s0 == 0xFFFFFFFFU || s0 != s1) return 0;
    if (k0 == 0xFFFFFFFFU || k0 != k1) return 0;

    // BSC C / DIV are 16-bit fields; high half is RAZ on a live BSC.
    if ((c0 & 0xFFFF0000U) != 0U) return 0;
    if ((d0 & 0xFFFF0000U) != 0U) return 0;
    i2c_bsc_val = 1;

    // SPI0 CS bits [31:26] are reserved RAZ on BCM SPI.
    if ((s0 & 0xFC000000U) != 0U) return 0;
    i2c_spi_val = 1;

    i2c_ok_val = 1;
    return 1;
}

int          kernel_i2c_ok(void)  { return i2c_ok_val; }
unsigned int kernel_i2c_bsc(void) { return i2c_bsc_val; }
unsigned int kernel_i2c_div(void) { return i2c_div_val; }
unsigned int kernel_i2c_spi(void) { return i2c_spi_val; }
