// Runtime V143: BCM2711 AUX SPI1 enable after GENET.
// New unused block (AUX 0xFE215000 / SPI1 0xFE215080).
// Fail-closed AUXENB SPI1 bit write+readback + leftover restore.
// Does not enable mini-UART. Does not start a transfer. No pin-mux.
// Do not probe BSC0 after GENET (AXI hang, UART dies).
// Do not assert SPI0 TA after GENET (hung boot).
// No EL0 enter (I-abort esr=0xbf000002 elr=0x100002000 after GENET DMA).
// No boot event emit.

#include "Support.h"
#include <stdint.h>

#define AUX_BASE   0xFE215000UL
#define SPI1_BASE  0xFE215080UL /* do not start a transfer; address is the block id */
#define G32(base, off) (*(volatile uint32_t *)((base) + (unsigned long)(off)))

#define AUXIRQ  0x00U
#define AUXENB  0x04U

#define AUX_SPI1_EN (1U << 1)
#define AUX_EN_MASK 0x7U

static int auxspi1_probed;
static int auxspi1_ok_val;
static unsigned int auxspi1_en_val;
static unsigned int auxspi1_restore_val;

int kernel_auxspi1_selftest(void) {
    if (auxspi1_probed) return auxspi1_ok_val;
    auxspi1_probed = 1;
    auxspi1_ok_val = 0;
    auxspi1_en_val = 0;
    auxspi1_restore_val = 0;

    unsigned int irq0 = G32(AUX_BASE, AUXIRQ);
    unsigned int enb0 = G32(AUX_BASE, AUXENB);
    unsigned int enb1 = G32(AUX_BASE, AUXENB);
    if (irq0 == 0xFFFFFFFFU || enb0 == 0xFFFFFFFFU || enb0 != enb1) {
        return 0;
    }
    /* AUXIRQ / AUXENB are 3-bit fields; high bits are RAZ on a live AUX. */
    if ((irq0 & ~AUX_EN_MASK) != 0U) return 0;
    if ((enb0 & ~AUX_EN_MASK) != 0U) return 0;

    unsigned int want = enb0 | AUX_SPI1_EN;
    G32(AUX_BASE, AUXENB) = want;
    unsigned int enb = G32(AUX_BASE, AUXENB);
    if ((enb & AUX_SPI1_EN) != 0U) {
        auxspi1_en_val = 1;
    }
    (void)SPI1_BASE;

    G32(AUX_BASE, AUXENB) = enb0;
    if ((G32(AUX_BASE, AUXENB) & AUX_EN_MASK) == (enb0 & AUX_EN_MASK)) {
        auxspi1_restore_val = 1;
    }

    if (auxspi1_en_val == 1U && auxspi1_restore_val == 1U) {
        auxspi1_ok_val = 1;
        return 1;
    }
    return 0;
}

int          kernel_auxspi1_ok(void)      { return auxspi1_ok_val; }
unsigned int kernel_auxspi1_en(void)      { return auxspi1_en_val; }
unsigned int kernel_auxspi1_restore(void) { return auxspi1_restore_val; }
