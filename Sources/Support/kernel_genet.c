// Runtime V67: BCM2711 GENET register probe (SYS_REV + bounded MDIO/link).
// Sources/Support/kernel_genet.c
//
// ARM low-peripheral GENET base 0xFD580000 (same 1GB Device block as PCIe
// at 0xFD500000; mmu.c l1[3]). Boot-time only — never from CNTP IRQ or
// secondary workers (HDMI blit lesson: hot-path MMIO stalls UART).
// Fail-closed: a hung MDIO poll times out; we do not reset UMAC or program DMA.

#include "include/Support.h"
#include <stdint.h>

#define GENET_BASE 0xFD580000UL
#define G32(off) (*(volatile uint32_t *)(GENET_BASE + (unsigned long)(off)))

#define SYS_REV_CTRL 0x0000U
#define EXT_OFF 0x0080U
#define EXT_RGMII_OOB_CTRL (EXT_OFF + 0x000CU)
#define RGMII_LINK (1U << 4)

#define UMAC_OFF 0x0800U
#define UMAC_MDIO_CMD (UMAC_OFF + 0x614U)
#define MDIO_START_BUSY (1U << 29)
#define MDIO_READ_FAIL (1U << 28)
#define MDIO_RD (2U << 26)
#define MDIO_PMD_SHIFT 21
#define MDIO_REG_SHIFT 16

#define PHY_ADDR 1U
#define MII_BMSR 1U
#define BMSR_LSTATUS (1U << 2)

static int genet_probed;
static int genet_ok_val;
static unsigned int genet_rev_val;
static unsigned int genet_mdio_val;
static unsigned int genet_link_val;

static void genet_udelay(unsigned int us) {
    uint64_t freq, start, now;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(start));
    uint64_t ticks = (uint64_t)us * freq / 1000000ULL;
    do {
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(now));
    } while (now - start < ticks);
}

// Bounded MDIO read. Returns 1 and writes *out on success; 0 on timeout/fail.
static int genet_mdio_read(unsigned int phy, unsigned int reg, unsigned int *out) {
    uint32_t cmd = MDIO_RD | ((phy & 0x1FU) << MDIO_PMD_SHIFT) |
                   ((reg & 0x1FU) << MDIO_REG_SHIFT) | MDIO_START_BUSY;
    G32(UMAC_MDIO_CMD) = cmd;
    __asm__ volatile("dsb sy" ::: "memory");
    for (int i = 0; i < 200; i++) {
        uint32_t v = G32(UMAC_MDIO_CMD);
        if ((v & MDIO_START_BUSY) == 0U) {
            if (v & MDIO_READ_FAIL) return 0;
            *out = v & 0xFFFFU;
            return 1;
        }
        genet_udelay(10);  // 2ms cap
    }
    return 0;
}

int kernel_genet_selftest(void) {
    if (genet_probed) return genet_ok_val;
    genet_probed = 1;
    genet_ok_val = 0;
    genet_rev_val = 0;
    genet_mdio_val = 0;
    genet_link_val = 0;

    uint32_t rev = G32(SYS_REV_CTRL);
    genet_rev_val = (unsigned int)rev;
    if (rev == 0U || rev == 0xFFFFFFFFU || rev == 0xDEADDEADU) return 0;

    unsigned int bmsr = 0;
    if (genet_mdio_read(PHY_ADDR, MII_BMSR, &bmsr)) {
        genet_mdio_val = 1;
        if (bmsr & BMSR_LSTATUS) genet_link_val = 1;
    } else {
        uint32_t oob = G32(EXT_RGMII_OOB_CTRL);
        if (oob & RGMII_LINK) genet_link_val = 1;
    }

    genet_ok_val = 1;
    return 1;
}

int          kernel_genet_ok(void)   { return genet_ok_val;   }
unsigned int kernel_genet_rev(void)  { return genet_rev_val;  }
unsigned int kernel_genet_mdio(void) { return genet_mdio_val; }
unsigned int kernel_genet_link(void) { return genet_link_val; }
