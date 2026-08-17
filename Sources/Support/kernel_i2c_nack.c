// Runtime V82: one bounded BSC1 write to a vacant 7-bit address.
// Honest nack=1 when S.ERR is set. Timeout or ACK fail-closes (ok=0).
// No boot event emit. No EL0 enter after GENET DMA (I-abort landmine).
// GPIO2/3 are muxed to ALT0 for the transfer and restored afterward.

#include "Support.h"
#include <stdint.h>

#define BSC1_BASE  0xFE804000UL
#define GPIO_BASE  0xFE200000UL

#define G32(base, off) (*(volatile uint32_t *)((base) + (unsigned long)(off)))

#define BSC_C    0x00U
#define BSC_S    0x04U
#define BSC_DLEN 0x08U
#define BSC_A    0x0CU
#define BSC_FIFO 0x10U
#define BSC_DIV  0x14U

#define BSC_C_CLEAR  (1U << 4)
#define BSC_C_ST     (1U << 7)
#define BSC_C_I2CEN  (1U << 15)

#define BSC_S_DONE   (1U << 1)
#define BSC_S_ERR    (1U << 8)
#define BSC_S_CLKT   (1U << 9)

#define GPFSEL0  0x00U
#define FSEL_MASK 7U
#define FSEL_ALT0 4U
#define GPIO_SDA  2U
#define GPIO_SCL  3U

#define I2C2_ADDR          0x7FU
#define I2C2_DIV_DEFAULT   0x5DCU
#define I2C2_TIMEOUT_TICKS 540000UL /* ~10ms @ 54MHz CNTPCT */

static int i2c2_probed;
static int i2c2_ok_val;
static unsigned int i2c2_nack_val;
static unsigned int i2c2_addr_val;
static unsigned int i2c2_sta_val;

static void i2c2_mux_restore(unsigned int saved_fsel) {
    G32(GPIO_BASE, GPFSEL0) = saved_fsel;
}

static unsigned int i2c2_mux_sda_scl(void) {
    unsigned int fsel = G32(GPIO_BASE, GPFSEL0);
    unsigned int next = fsel;
    next &= ~(FSEL_MASK << (GPIO_SDA * 3U));
    next &= ~(FSEL_MASK << (GPIO_SCL * 3U));
    next |= (FSEL_ALT0 << (GPIO_SDA * 3U));
    next |= (FSEL_ALT0 << (GPIO_SCL * 3U));
    G32(GPIO_BASE, GPFSEL0) = next;
    return fsel;
}

int kernel_i2c2_selftest(void) {
    if (i2c2_probed) return i2c2_ok_val;
    i2c2_probed = 1;
    i2c2_ok_val = 0;
    i2c2_nack_val = 0;
    i2c2_addr_val = I2C2_ADDR;
    i2c2_sta_val = 0;

    if (kernel_i2c_selftest() == 0) return 0;

    unsigned int div = G32(BSC1_BASE, BSC_DIV);
    if (div == 0U || div == 0xFFFFFFFFU) {
        G32(BSC1_BASE, BSC_DIV) = I2C2_DIV_DEFAULT;
    }

    unsigned int saved_fsel = i2c2_mux_sda_scl();

    G32(BSC1_BASE, BSC_C) = BSC_C_CLEAR;
    G32(BSC1_BASE, BSC_S) = BSC_S_DONE | BSC_S_ERR | BSC_S_CLKT;
    G32(BSC1_BASE, BSC_A) = I2C2_ADDR;
    G32(BSC1_BASE, BSC_DLEN) = 1U;
    G32(BSC1_BASE, BSC_FIFO) = 0U;
    G32(BSC1_BASE, BSC_C) = BSC_C_I2CEN | BSC_C_ST;

    unsigned long start = read_cntpct();
    unsigned int sta = 0;
    for (;;) {
        sta = G32(BSC1_BASE, BSC_S);
        if ((sta & (BSC_S_DONE | BSC_S_ERR | BSC_S_CLKT)) != 0U) break;
        if ((read_cntpct() - start) >= I2C2_TIMEOUT_TICKS) break;
    }
    i2c2_sta_val = sta;

    G32(BSC1_BASE, BSC_S) = BSC_S_DONE | BSC_S_ERR | BSC_S_CLKT;
    G32(BSC1_BASE, BSC_C) = 0;
    i2c2_mux_restore(saved_fsel);

    if ((sta & BSC_S_ERR) != 0U && (sta & BSC_S_CLKT) == 0U) {
        i2c2_nack_val = 1;
        i2c2_ok_val = 1;
        return 1;
    }
    return 0;
}

int          kernel_i2c2_ok(void)   { return i2c2_ok_val; }
unsigned int kernel_i2c2_nack(void) { return i2c2_nack_val; }
unsigned int kernel_i2c2_addr(void) { return i2c2_addr_val; }
unsigned int kernel_i2c2_sta(void)  { return i2c2_sta_val; }
