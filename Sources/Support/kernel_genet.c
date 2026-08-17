// Runtime V67: BCM2711 GENET register probe (SYS_REV + bounded MDIO/link).
// Runtime V68: UMAC station MAC + leftover RX_EN + MIB snapshot (read-only).
// Runtime V69: firmware station MAC via mailbox GET_BOARD_MAC_ADDRESS.
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
#define UMAC_CMD (UMAC_OFF + 0x008U)
#define CMD_RX_EN (1U << 1)
#define UMAC_MAC0 (UMAC_OFF + 0x00CU)
#define UMAC_MAC1 (UMAC_OFF + 0x010U)
#define UMAC_MIB_RX_PKT (UMAC_OFF + 0x428U)
#define UMAC_MIB_RX_BYTES (UMAC_OFF + 0x42CU)
#define UMAC_MIB_RX_POK (UMAC_OFF + 0x464U)
#define UMAC_MDIO_CMD (UMAC_OFF + 0x614U)
#define MDIO_START_BUSY (1U << 29)
#define MDIO_READ_FAIL (1U << 28)
#define MDIO_WR (1U << 26)
#define MDIO_RD (2U << 26)
#define MDIO_PMD_SHIFT 21
#define MDIO_REG_SHIFT 16

#define PHY_ADDR 1U
#define MII_BMCR 0U
#define MII_BMSR 1U
#define MII_PHYSID1 2U
#define MII_PHYSID2 3U
#define BMSR_LSTATUS (1U << 2)

static int genet_probed;
static int genet_ok_val;
static unsigned int genet_rev_val;
static unsigned int genet_mdio_val;
static unsigned int genet_link_val;

static int genet2_probed;
static int genet2_ok_val;
static unsigned long genet2_mac_val;
static unsigned int genet2_rx_val;
static unsigned int genet2_frames_val;
static unsigned int genet2_bytes_val;

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

// Bounded MDIO write. Returns 1 when START_BUSY clears; 0 on timeout.
static int genet_mdio_write(unsigned int phy, unsigned int reg, unsigned int val) {
    uint32_t cmd = MDIO_WR | ((phy & 0x1FU) << MDIO_PMD_SHIFT) |
                   ((reg & 0x1FU) << MDIO_REG_SHIFT) | (val & 0xFFFFU) |
                   MDIO_START_BUSY;
    G32(UMAC_MDIO_CMD) = cmd;
    __asm__ volatile("dsb sy" ::: "memory");
    for (int i = 0; i < 200; i++) {
        uint32_t v = G32(UMAC_MDIO_CMD);
        if ((v & MDIO_START_BUSY) == 0U) return 1;
        genet_udelay(10);
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

// V68: UMAC MAC + leftover RX_EN + MIB snapshot. Do not write CMD_RX_EN
// (firmware rings would DMA into stale buffers). Boot-time only; no wait.
int kernel_genet2_selftest(void) {
    if (genet2_probed) return genet2_ok_val;
    genet2_probed = 1;
    genet2_ok_val = 0;
    genet2_mac_val = 0;
    genet2_rx_val = 0;
    genet2_frames_val = 0;
    genet2_bytes_val = 0;

    if (!kernel_genet_selftest()) return 0;

    uint32_t mac0 = G32(UMAC_MAC0);
    uint32_t mac1 = G32(UMAC_MAC1) & 0xFFFFU;
    unsigned long mac = ((unsigned long)mac0 << 16) | (unsigned long)mac1;
    genet2_mac_val = mac;

    uint32_t cmd = G32(UMAC_CMD);
    genet2_rx_val = (cmd & CMD_RX_EN) ? 1U : 0U;

    // Snapshot leftover MIB immediately. A 50ms CNTPCT spin here runs after
    // job execution is enabled and holds core0 off the Swift executor;
    // leftover rx.pok is already populated by firmware netboot.
    genet2_frames_val = G32(UMAC_MIB_RX_POK);
    genet2_bytes_val = G32(UMAC_MIB_RX_BYTES);

    // Firmware netboot leaves UMAC_MAC0/1 at 0 on this board (observed).
    // ok=1 is leftover RX_EN + live MIB, not a station MAC. Do not write MAC.
    if (cmd == 0xFFFFFFFFU || cmd == 0xDEADDEADU) return 0;
    if (genet2_frames_val == 0xFFFFFFFFU || genet2_bytes_val == 0xFFFFFFFFU) return 0;

    genet2_ok_val = 1;
    return 1;
}

int           kernel_genet2_ok(void)     { return genet2_ok_val;     }
unsigned long kernel_genet2_mac(void)    { return genet2_mac_val;    }
unsigned int  kernel_genet2_rx(void)     { return genet2_rx_val;     }
unsigned int  kernel_genet2_frames(void) { return genet2_frames_val; }
unsigned int  kernel_genet2_bytes(void)  { return genet2_bytes_val;  }

static int genet3_probed;
static int genet3_ok_val;
static unsigned long genet3_mac_val;
static unsigned int genet3_mbox_val;
static unsigned long genet3_umac_val;

// V69: mailbox station MAC. Do not write UMAC_MAC0/1. Do not touch CMD_RX_EN.
int kernel_genet3_selftest(void) {
    if (genet3_probed) return genet3_ok_val;
    genet3_probed = 1;
    genet3_ok_val = 0;
    genet3_mac_val = 0;
    genet3_mbox_val = 0;
    genet3_umac_val = 0;

    if (!kernel_genet2_selftest()) return 0;
    genet3_umac_val = kernel_genet2_mac();

    unsigned long mac = 0;
    if (kernel_vc_mbox_board_mac(&mac)) {
        genet3_mbox_val = 1;
        genet3_mac_val = mac;
    }

    // ok=1 is a non-zero firmware MAC. umac stays leftover (observed 0).
    // Do not program UMAC — that is a later DMA-adjacent slice.
    if (genet3_mbox_val == 0U || genet3_mac_val == 0UL) return 0;

    genet3_ok_val = 1;
    return 1;
}

int           kernel_genet3_ok(void)   { return genet3_ok_val;   }
unsigned long kernel_genet3_mac(void)  { return genet3_mac_val;  }
unsigned int  kernel_genet3_mbox(void) { return genet3_mbox_val; }
unsigned long kernel_genet3_umac(void) { return genet3_umac_val; }

static int genet4_probed;
static int genet4_ok_val;
static unsigned long genet4_serial_val;
static unsigned int genet4_mbox_val;
static unsigned long genet4_mac_val;

// V70: mailbox board serial. Do not write UMAC_MAC0/1. Do not touch CMD_RX_EN.
int kernel_genet4_selftest(void) {
    if (genet4_probed) return genet4_ok_val;
    genet4_probed = 1;
    genet4_ok_val = 0;
    genet4_serial_val = 0;
    genet4_mbox_val = 0;
    genet4_mac_val = 0;

    if (!kernel_genet3_selftest()) return 0;
    genet4_mac_val = kernel_genet3_mac();

    unsigned long serial = 0;
    if (kernel_vc_mbox_board_serial(&serial)) {
        genet4_mbox_val = 1;
        genet4_serial_val = serial;
    }

    // ok=1 is a non-zero firmware serial. Mailbox path; no MDIO.
    if (genet4_mbox_val == 0U || genet4_serial_val == 0UL) return 0;

    genet4_ok_val = 1;
    return 1;
}

int           kernel_genet4_ok(void)     { return genet4_ok_val;     }
unsigned long kernel_genet4_serial(void) { return genet4_serial_val; }
unsigned int  kernel_genet4_mbox(void)   { return genet4_mbox_val;   }
unsigned long kernel_genet4_mac(void)    { return genet4_mac_val;    }

#define CMD_TX_EN (1U << 0)
#define CMD_PROMISC (1U << 4)

#define RDMA_OFF 0x2000U
#define DESC_WORDS 3U
#define DESC_BYTES (DESC_WORDS * 4U)
#define TOTAL_DESC 256U
#define DESC_INDEX 16U
#define DMA_RING_SIZE 0x40U
#define DMA_RINGS_SIZE (DMA_RING_SIZE * (DESC_INDEX + 1U))
#define RDMA_REG_OFF (RDMA_OFF + TOTAL_DESC * DESC_BYTES)

#define RX_RING_N 4U
#define RX_BUF_LEN 2048U

#define DMA_CTRL_OFF 0x04U
#define DMA_STATUS_OFF 0x08U
#define DMA_EN (1U << 0)
#define DMA_DISABLED (1U << 0)
#define DMA_RING_BUF_EN_SHIFT 1U
#define DMA_RING_CFG_OFF 0x00U

#define RR_WRITE_PTR 0x00U
#define RR_PROD_INDEX 0x08U
#define RR_CONS_INDEX 0x0CU
#define RR_BUF_SIZE 0x10U
#define RR_START 0x14U
#define RR_END 0x1CU
#define RR_XON_XOFF 0x28U
#define RR_READ_PTR 0x2CU

#define DMA_RING16_EN (1U << (DESC_INDEX + DMA_RING_BUF_EN_SHIFT))

static int genet5_probed;
static int genet5_ok_val;
static unsigned int genet5_stop_val;
static unsigned int genet5_ring_val;
static unsigned int genet5_rx_val;
static unsigned int genet5_frames_val;

static uint32_t rdma_common(unsigned int off) {
    return G32(RDMA_REG_OFF + DMA_RINGS_SIZE + off);
}

static void rdma_common_wr(unsigned int off, uint32_t v) {
    G32(RDMA_REG_OFF + DMA_RINGS_SIZE + off) = v;
    __asm__ volatile("dsb sy" ::: "memory");
}

static uint32_t rdma_ring16(unsigned int off) {
    return G32(RDMA_REG_OFF + DMA_RING_SIZE * DESC_INDEX + off);
}

static void rdma_ring16_wr(unsigned int off, uint32_t v) {
    G32(RDMA_REG_OFF + DMA_RING_SIZE * DESC_INDEX + off) = v;
    __asm__ volatile("dsb sy" ::: "memory");
}

static int genet_wait_us(unsigned int us, int (*pred)(void)) {
    uint64_t freq, start, now, ticks;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(start));
    ticks = (uint64_t)us * freq / 1000000ULL;
    do {
        if (pred && pred()) return 1;
        genet_udelay(10);
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(now));
    } while (now - start < ticks);
    return pred ? pred() : 1;
}

static int rdma_is_disabled(void) {
    return (rdma_common(DMA_STATUS_OFF) & DMA_DISABLED) ? 1 : 0;
}

static int leftover_rx_clear(void) {
    return (G32(UMAC_CMD) & CMD_RX_EN) == 0U ? 1 : 0;
}

// V72: stop leftover RX, program our NC buffers into on-chip BDs, enable RX.
int kernel_genet5_selftest(void) {
    if (genet5_probed) return genet5_ok_val;
    genet5_probed = 1;
    genet5_ok_val = 0;
    genet5_stop_val = 0;
    genet5_ring_val = 0;
    genet5_rx_val = 0;
    genet5_frames_val = 0;

    if (!kernel_genet4_selftest()) return 0;

    unsigned long pa[2];
    void *nc[2];
    if (!kernel_dma_alloc_nc(&pa[0], &nc[0])) return 0;
    if (!kernel_dma_alloc_nc(&pa[1], &nc[1])) return 0;

    uint32_t cmd = G32(UMAC_CMD);
    if (cmd == 0xFFFFFFFFU || cmd == 0xDEADDEADU) return 0;
    G32(UMAC_CMD) = cmd & ~(CMD_RX_EN | CMD_TX_EN);
    __asm__ volatile("dsb sy" ::: "memory");
    genet_wait_us(2000, leftover_rx_clear);
    if ((G32(UMAC_CMD) & CMD_RX_EN) != 0U) return 0;
    genet5_stop_val = 1;

    uint32_t dma_ctrl = rdma_common(DMA_CTRL_OFF);
    rdma_common_wr(DMA_CTRL_OFF, dma_ctrl & ~DMA_EN);
    if (!genet_wait_us(5000, rdma_is_disabled)) return 0;

    unsigned int i;
    for (i = 0; i < RX_RING_N; i++) {
        unsigned long buf_pa = pa[i / 2U] + (unsigned long)(i % 2U) * RX_BUF_LEN;
        unsigned int bd = RDMA_OFF + i * DESC_BYTES;
        G32(bd + 4U) = (uint32_t)buf_pa;
        G32(bd + 8U) = 0;
        G32(bd + 0U) = (RX_BUF_LEN << 16);
    }
    __asm__ volatile("dsb sy" ::: "memory");

    rdma_ring16_wr(RR_PROD_INDEX, 0);
    rdma_ring16_wr(RR_CONS_INDEX, 0);
    rdma_ring16_wr(RR_BUF_SIZE, (RX_RING_N << 16) | RX_BUF_LEN);
    rdma_ring16_wr(RR_XON_XOFF, (1U << 16) | 2U);
    rdma_ring16_wr(RR_START, 0);
    rdma_ring16_wr(RR_READ_PTR, 0);
    rdma_ring16_wr(RR_WRITE_PTR, 0);
    rdma_ring16_wr(RR_END, RX_RING_N * DESC_WORDS - 1U);

    if (rdma_ring16(RR_START) != 0U) return 0;
    if (rdma_ring16(RR_END) != (RX_RING_N * DESC_WORDS - 1U)) return 0;
    if (G32(RDMA_OFF + 4U) != (uint32_t)pa[0]) return 0;
    genet5_ring_val = 1;

    rdma_common_wr(DMA_RING_CFG_OFF, (1U << DESC_INDEX));
    rdma_common_wr(DMA_CTRL_OFF, DMA_EN | DMA_RING16_EN);

    cmd = G32(UMAC_CMD);
    G32(UMAC_CMD) = (cmd | CMD_RX_EN | CMD_PROMISC) & ~CMD_TX_EN;
    __asm__ volatile("dsb sy" ::: "memory");
    if ((G32(UMAC_CMD) & CMD_RX_EN) == 0U) return 0;
    genet5_rx_val = 1;

    uint64_t freq, start, now, ticks;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(start));
    ticks = 250ULL * freq / 1000ULL;
    do {
        uint32_t prod = rdma_ring16(RR_PROD_INDEX) & 0xFFFFU;
        genet5_frames_val = prod;
        if (prod != 0U) break;
        genet_udelay(1000);
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(now));
    } while (now - start < ticks);

    genet5_ok_val = 1;
    return 1;
}

int          kernel_genet5_ok(void)     { return genet5_ok_val;     }
unsigned int kernel_genet5_stop(void)   { return genet5_stop_val;   }
unsigned int kernel_genet5_ring(void)   { return genet5_ring_val;   }
unsigned int kernel_genet5_rx(void)     { return genet5_rx_val;     }
unsigned int kernel_genet5_frames(void) { return genet5_frames_val; }

#define TDMA_OFF 0x4000U
#define TDMA_REG_OFF (TDMA_OFF + TOTAL_DESC * DESC_BYTES)
#define HFB_REG_OFF 0xFC00U
#define HFB_CTRL 0x00U
#define HFB_FLT_ENABLE 0x04U
#define RBUF_OFF 0x0300U
#define RBUF_CTRL 0x00U
#define RBUF_64B_EN (1U << 0)
#define RBUF_CHK_CTRL 0x14U
#define RBUF_RXCHK_EN (1U << 0)
#define UMAC_MAX_FRAME_LEN (UMAC_OFF + 0x014U)
#define DMA_SOP 0x2000U
#define DMA_EOP 0x4000U
#define DMA_TX_APPEND_CRC 0x0040U
#define DMA_TX_QTAG_SHIFT 7U
#define DMA_QTAG_MASK 0x3FU
#define DMA_MBUF_DONE 0x24U
#define DMA_INDEX2RING0 0x70U
#define TX_RING_N 4U
#define TX_FRAME_LEN 60U

static int genet6_probed;
static int genet6_ok_val;
static unsigned int genet6_mac_val;
static unsigned int genet6_tx_val;
static unsigned int genet6_frames_val;

static uint32_t tdma_common(unsigned int off) {
    return G32(TDMA_REG_OFF + DMA_RINGS_SIZE + off);
}

static void tdma_common_wr(unsigned int off, uint32_t v) {
    G32(TDMA_REG_OFF + DMA_RINGS_SIZE + off) = v;
    __asm__ volatile("dsb sy" ::: "memory");
}

static uint32_t tdma_ring16(unsigned int off) {
    return G32(TDMA_REG_OFF + DMA_RING_SIZE * DESC_INDEX + off);
}

static void tdma_ring16_wr(unsigned int off, uint32_t v) {
    G32(TDMA_REG_OFF + DMA_RING_SIZE * DESC_INDEX + off) = v;
    __asm__ volatile("dsb sy" ::: "memory");
}

static int tdma_is_disabled(void) {
    return (tdma_common(DMA_STATUS_OFF) & DMA_DISABLED) ? 1 : 0;
}

static void genet_write_arp(volatile uint8_t *p, unsigned long mac) {
    unsigned int i;
    for (i = 0; i < 6U; i++) p[i] = 0xFFU;
    p[6]  = (uint8_t)((mac >> 40) & 0xFFUL);
    p[7]  = (uint8_t)((mac >> 32) & 0xFFUL);
    p[8]  = (uint8_t)((mac >> 24) & 0xFFUL);
    p[9]  = (uint8_t)((mac >> 16) & 0xFFUL);
    p[10] = (uint8_t)((mac >> 8) & 0xFFUL);
    p[11] = (uint8_t)(mac & 0xFFUL);
    p[12] = 0x08U;
    p[13] = 0x06U;
    p[14] = 0x00U;
    p[15] = 0x01U;
    p[16] = 0x08U;
    p[17] = 0x00U;
    p[18] = 0x06U;
    p[19] = 0x04U;
    p[20] = 0x00U;
    p[21] = 0x01U;
    for (i = 0; i < 6U; i++) p[22U + i] = p[6U + i];
    p[28] = 10U;
    p[29] = 42U;
    p[30] = 0U;
    p[31] = 2U;
    for (i = 0; i < 6U; i++) p[32U + i] = 0U;
    p[38] = 10U;
    p[39] = 42U;
    p[40] = 0U;
    p[41] = 1U;
    for (i = 42U; i < TX_FRAME_LEN; i++) p[i] = 0U;
}

// V73: write mailbox MAC into UMAC, own TX ring, one ARP, longer RX poll.
int kernel_genet6_selftest(void) {
    if (genet6_probed) return genet6_ok_val;
    genet6_probed = 1;
    genet6_ok_val = 0;
    genet6_mac_val = 0;
    genet6_tx_val = 0;
    genet6_frames_val = 0;

    if (!kernel_genet5_selftest()) return 0;

    unsigned long mac = kernel_genet3_mac();
    if (!mac) return 0;
    uint32_t mac0 = (uint32_t)(mac >> 16);
    uint32_t mac1 = (uint32_t)(mac & 0xFFFFUL);
    G32(UMAC_MAC0) = mac0;
    G32(UMAC_MAC1) = mac1;
    __asm__ volatile("dsb sy" ::: "memory");
    if (G32(UMAC_MAC0) != mac0) return 0;
    if ((G32(UMAC_MAC1) & 0xFFFFU) != mac1) return 0;
    genet6_mac_val = 1;

    G32(HFB_REG_OFF + HFB_CTRL) = 0;
    G32(HFB_REG_OFF + HFB_FLT_ENABLE) = 0;
    G32(HFB_REG_OFF + HFB_FLT_ENABLE + 4U) = 0;
    __asm__ volatile("dsb sy" ::: "memory");
    unsigned int i;
    for (i = 0; i < 8U; i++) {
        rdma_common_wr(DMA_INDEX2RING0 + i * 4U, 0);
    }
    uint32_t rbuf = G32(RBUF_OFF + RBUF_CTRL);
    if (rbuf != 0xFFFFFFFFU && rbuf != 0xDEADDEADU && (rbuf & RBUF_64B_EN)) {
        G32(RBUF_OFF + RBUF_CTRL) = rbuf & ~RBUF_64B_EN;
        __asm__ volatile("dsb sy" ::: "memory");
    }
    G32(UMAC_MAX_FRAME_LEN) = 1536U;
    __asm__ volatile("dsb sy" ::: "memory");

    unsigned long tx_pa = 0;
    void *tx_nc = 0;
    if (!kernel_dma_alloc_nc(&tx_pa, &tx_nc)) return 0;
    genet_write_arp((volatile uint8_t *)tx_nc, mac);
    __asm__ volatile("dsb sy" ::: "memory");

    uint32_t tctrl = tdma_common(DMA_CTRL_OFF);
    tdma_common_wr(DMA_CTRL_OFF, tctrl & ~DMA_EN);
    if (!genet_wait_us(5000, tdma_is_disabled)) return 0;

    for (i = 0; i < TX_RING_N; i++) {
        unsigned int bd = TDMA_OFF + i * DESC_BYTES;
        G32(bd + 4U) = (uint32_t)tx_pa;
        G32(bd + 8U) = 0;
        G32(bd + 0U) = 0;
    }
    uint32_t len_stat = ((uint32_t)TX_FRAME_LEN << 16) |
                        (DMA_QTAG_MASK << DMA_TX_QTAG_SHIFT) |
                        DMA_TX_APPEND_CRC | DMA_SOP | DMA_EOP;
    G32(TDMA_OFF + 0U) = len_stat;
    G32(TDMA_OFF + 4U) = (uint32_t)tx_pa;
    G32(TDMA_OFF + 8U) = 0;
    __asm__ volatile("dsb sy" ::: "memory");

    tdma_ring16_wr(RR_PROD_INDEX, 0);
    tdma_ring16_wr(RR_CONS_INDEX, 0);
    tdma_ring16_wr(RR_BUF_SIZE, (TX_RING_N << 16) | RX_BUF_LEN);
    tdma_ring16_wr(DMA_MBUF_DONE, 1);
    tdma_ring16_wr(RR_XON_XOFF, 0);
    tdma_ring16_wr(RR_START, 0);
    tdma_ring16_wr(RR_READ_PTR, 0);
    tdma_ring16_wr(RR_WRITE_PTR, 0);
    tdma_ring16_wr(RR_END, TX_RING_N * DESC_WORDS - 1U);

    if (tdma_ring16(RR_START) != 0U) return 0;
    if (tdma_ring16(RR_END) != (TX_RING_N * DESC_WORDS - 1U)) return 0;
    if (G32(TDMA_OFF + 4U) != (uint32_t)tx_pa) return 0;

    tdma_common_wr(DMA_RING_CFG_OFF, (1U << DESC_INDEX));
    tdma_common_wr(DMA_CTRL_OFF, DMA_EN | DMA_RING16_EN);

    uint32_t cmd = G32(UMAC_CMD);
    G32(UMAC_CMD) = cmd | CMD_TX_EN | CMD_RX_EN | CMD_PROMISC;
    __asm__ volatile("dsb sy" ::: "memory");
    if ((G32(UMAC_CMD) & CMD_TX_EN) == 0U) return 0;

    tdma_ring16_wr(RR_PROD_INDEX, 1);
    genet6_tx_val = 1;

    uint64_t freq, start, now, ticks;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(start));
    ticks = 1500ULL * freq / 1000ULL;
    do {
        uint32_t prod = rdma_ring16(RR_PROD_INDEX) & 0xFFFFU;
        genet6_frames_val = prod;
        if (prod != 0U) break;
        genet_udelay(1000);
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(now));
    } while (now - start < ticks);

    genet6_ok_val = 1;
    return 1;
}

int          kernel_genet6_ok(void)     { return genet6_ok_val;     }
unsigned int kernel_genet6_mac(void)    { return genet6_mac_val;    }
unsigned int kernel_genet6_tx(void)     { return genet6_tx_val;     }
unsigned int kernel_genet6_frames(void) { return genet6_frames_val; }

#define UMAC_TX_FLUSH (UMAC_OFF + 0x334U)
#define UMAC_MDF_CTRL (UMAC_OFF + 0x650U)
#define UMAC_MDF_ADDR (UMAC_OFF + 0x654U)
#define DMA_SCB_BURST 0x0CU
#define DMA_MAX_BURST 0x10U

static int genet7_probed;
static int genet7_ok_val;
static unsigned int genet7_ring_val;
static unsigned int genet7_tx_val;
static unsigned int genet7_cons_val;
static unsigned int genet7_prod_val;
static unsigned int genet7_frames_val;
static unsigned long genet7_rx_pa[128];

static void genet_wr32(unsigned int off, uint32_t v) {
    G32(off) = v;
    __asm__ volatile("dsb sy" ::: "memory");
}

static void genet_pulse32(unsigned int off, uint32_t on, uint32_t offv) {
    genet_wr32(off, on);
    genet_udelay(10);
    genet_wr32(off, offv);
}

// V74: leftover RBUF/RDMA reset, Linux ring-16 BD count, fail-closed TX CONS.
int kernel_genet7_selftest(void) {
    enum {
        RX_Q16_N = 256U,
        TX_Q16_N = 128U,
        TX_Q16_START = 128U,
        RX_PAGES = 128U,
        FC_XOFF = 5U,
        FC_XON = 16U
    };
    if (genet7_probed) return genet7_ok_val;
    genet7_probed = 1;
    genet7_ok_val = 0;
    genet7_ring_val = 0;
    genet7_tx_val = 0;
    genet7_cons_val = 0;
    genet7_prod_val = 0;
    genet7_frames_val = 0;

    if (!kernel_genet6_selftest()) return 0;

    unsigned long mac = kernel_genet3_mac();
    if (!mac) return 0;

    uint32_t cmd = G32(UMAC_CMD);
    if (cmd == 0xFFFFFFFFU || cmd == 0xDEADDEADU) return 0;
    genet_wr32(UMAC_CMD, cmd & ~(CMD_RX_EN | CMD_TX_EN));
    genet_wait_us(2000, leftover_rx_clear);

    rdma_common_wr(DMA_CTRL_OFF, 0);
    tdma_common_wr(DMA_CTRL_OFF, 0);
    rdma_common_wr(DMA_RING_CFG_OFF, 0);
    tdma_common_wr(DMA_RING_CFG_OFF, 0);
    if (!genet_wait_us(5000, rdma_is_disabled)) return 0;
    if (!genet_wait_us(5000, tdma_is_disabled)) return 0;

    genet_pulse32(UMAC_TX_FLUSH, 1, 0);
    uint32_t rbuf = G32(RBUF_OFF + RBUF_CTRL);
    if (rbuf != 0xFFFFFFFFU && rbuf != 0xDEADDEADU && (rbuf & RBUF_64B_EN)) {
        genet_wr32(RBUF_OFF + RBUF_CTRL, rbuf & ~RBUF_64B_EN);
    }

    G32(HFB_REG_OFF + HFB_CTRL) = 0;
    G32(HFB_REG_OFF + HFB_FLT_ENABLE) = 0;
    G32(HFB_REG_OFF + HFB_FLT_ENABLE + 4U) = 0;
    __asm__ volatile("dsb sy" ::: "memory");
    unsigned int i;
    for (i = 0; i < 8U; i++) {
        rdma_common_wr(DMA_INDEX2RING0 + i * 4U, 0);
    }
    rdma_common_wr(DMA_SCB_BURST, DMA_MAX_BURST);
    tdma_common_wr(DMA_SCB_BURST, DMA_MAX_BURST);

    uint32_t mac0 = (uint32_t)(mac >> 16);
    uint32_t mac1 = (uint32_t)(mac & 0xFFFFUL);
    genet_wr32(UMAC_MAC0, mac0);
    genet_wr32(UMAC_MAC1, mac1);
    if (G32(UMAC_MAC0) != mac0) return 0;
    if ((G32(UMAC_MAC1) & 0xFFFFU) != mac1) return 0;
    genet_wr32(UMAC_MAX_FRAME_LEN, 1536U);

    for (i = 0; i < RX_PAGES; i++) {
        void *nc = 0;
        if (!kernel_dma_alloc_nc(&genet7_rx_pa[i], &nc)) return 0;
    }
    unsigned long tx_pa = 0;
    void *tx_nc = 0;
    if (!kernel_dma_alloc_nc(&tx_pa, &tx_nc)) return 0;
    genet_write_arp((volatile uint8_t *)tx_nc, mac);
    __asm__ volatile("dsb sy" ::: "memory");

    for (i = 0; i < RX_Q16_N; i++) {
        unsigned long buf_pa = genet7_rx_pa[i / 2U] + (unsigned long)(i % 2U) * RX_BUF_LEN;
        unsigned int bd = RDMA_OFF + i * DESC_BYTES;
        G32(bd + 4U) = (uint32_t)buf_pa;
        G32(bd + 8U) = 0;
        G32(bd + 0U) = (RX_BUF_LEN << 16);
    }
    __asm__ volatile("dsb sy" ::: "memory");

    rdma_ring16_wr(RR_PROD_INDEX, 0);
    rdma_ring16_wr(RR_CONS_INDEX, 0);
    rdma_ring16_wr(RR_BUF_SIZE, (RX_Q16_N << 16) | RX_BUF_LEN);
    rdma_ring16_wr(RR_XON_XOFF, (FC_XOFF << 16) | FC_XON);
    rdma_ring16_wr(RR_START, 0);
    rdma_ring16_wr(RR_READ_PTR, 0);
    rdma_ring16_wr(RR_WRITE_PTR, 0);
    rdma_ring16_wr(RR_END, RX_Q16_N * DESC_WORDS - 1U);

    uint32_t tx_start = TX_Q16_START * DESC_WORDS;
    uint32_t tx_end = (TX_Q16_START + TX_Q16_N) * DESC_WORDS - 1U;
    for (i = 0; i < TX_Q16_N; i++) {
        unsigned int bd = TDMA_OFF + (TX_Q16_START + i) * DESC_BYTES;
        G32(bd + 4U) = (uint32_t)tx_pa;
        G32(bd + 8U) = 0;
        G32(bd + 0U) = 0;
    }
    uint32_t len_stat = ((uint32_t)TX_FRAME_LEN << 16) |
                        (DMA_QTAG_MASK << DMA_TX_QTAG_SHIFT) |
                        DMA_TX_APPEND_CRC | DMA_SOP | DMA_EOP;
    unsigned int tx_bd = TDMA_OFF + TX_Q16_START * DESC_BYTES;
    G32(tx_bd + 0U) = len_stat;
    G32(tx_bd + 4U) = (uint32_t)tx_pa;
    G32(tx_bd + 8U) = 0;
    __asm__ volatile("dsb sy" ::: "memory");

    tdma_ring16_wr(RR_PROD_INDEX, 0);
    tdma_ring16_wr(RR_CONS_INDEX, 0);
    tdma_ring16_wr(RR_BUF_SIZE, (TX_Q16_N << 16) | RX_BUF_LEN);
    tdma_ring16_wr(DMA_MBUF_DONE, 1);
    tdma_ring16_wr(RR_XON_XOFF, 0);
    tdma_ring16_wr(RR_START, tx_start);
    tdma_ring16_wr(RR_READ_PTR, tx_start);
    tdma_ring16_wr(RR_WRITE_PTR, tx_start);
    tdma_ring16_wr(RR_END, tx_end);

    if (rdma_ring16(RR_END) != (RX_Q16_N * DESC_WORDS - 1U)) return 0;
    if (tdma_ring16(RR_START) != tx_start) return 0;
    if (tdma_ring16(RR_END) != tx_end) return 0;
    if (G32(tx_bd + 4U) != (uint32_t)tx_pa) return 0;
    genet7_ring_val = 1;

    rdma_common_wr(DMA_RING_CFG_OFF, (1U << DESC_INDEX));
    tdma_common_wr(DMA_RING_CFG_OFF, (1U << DESC_INDEX));
    rdma_common_wr(DMA_CTRL_OFF, DMA_EN | DMA_RING16_EN);
    tdma_common_wr(DMA_CTRL_OFF, DMA_EN | DMA_RING16_EN);

    cmd = G32(UMAC_CMD);
    genet_wr32(UMAC_CMD, cmd | CMD_TX_EN | CMD_RX_EN | CMD_PROMISC);
    if ((G32(UMAC_CMD) & (CMD_TX_EN | CMD_RX_EN)) != (CMD_TX_EN | CMD_RX_EN)) return 0;

    tdma_ring16_wr(RR_PROD_INDEX, 1);
    genet7_prod_val = tdma_ring16(RR_PROD_INDEX) & 0xFFFFU;

    uint64_t freq, start, now, ticks;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(start));
    ticks = 50ULL * freq / 1000ULL;
    do {
        genet7_cons_val = tdma_ring16(RR_CONS_INDEX) & 0xFFFFU;
        if (genet7_cons_val != 0U) break;
        genet_udelay(100);
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(now));
    } while (now - start < ticks);
    genet7_cons_val = tdma_ring16(RR_CONS_INDEX) & 0xFFFFU;
    genet7_prod_val = tdma_ring16(RR_PROD_INDEX) & 0xFFFFU;
    if (genet7_cons_val != 0U) genet7_tx_val = 1;

    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(start));
    ticks = 1000ULL * freq / 1000ULL;
    do {
        uint32_t rxprod = rdma_ring16(RR_PROD_INDEX) & 0xFFFFU;
        genet7_frames_val = rxprod;
        if (rxprod != 0U) break;
        genet_udelay(1000);
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(now));
    } while (now - start < ticks);

    // Park DMA after the snapshot so a 256-BD ring cannot stall UART later.
    cmd = G32(UMAC_CMD);
    genet_wr32(UMAC_CMD, cmd & ~(CMD_RX_EN | CMD_TX_EN));
    rdma_common_wr(DMA_CTRL_OFF, 0);
    tdma_common_wr(DMA_CTRL_OFF, 0);
    genet_wait_us(2000, rdma_is_disabled);
    genet_wait_us(2000, tdma_is_disabled);

    genet7_ok_val = 1;
    return 1;
}

int          kernel_genet7_ok(void)     { return genet7_ok_val;     }
unsigned int kernel_genet7_ring(void)   { return genet7_ring_val;   }
unsigned int kernel_genet7_tx(void)     { return genet7_tx_val;     }
unsigned int kernel_genet7_cons(void)   { return genet7_cons_val;   }
unsigned int kernel_genet7_prod(void)   { return genet7_prod_val;   }
unsigned int kernel_genet7_frames(void) { return genet7_frames_val; }

static int genet8_probed;
static int genet8_ok_val;
static unsigned int genet8_prod_val;
static unsigned int genet8_cons_val;
static unsigned int genet8_tx_val;
static unsigned int genet8_frames_val;

// V75: GENET v4/v5 ring map — TDMA PROD is 0x0C, CONS is 0x08.
int kernel_genet8_selftest(void) {
    enum {
        V4_TDMA_READ = 0x00U,
        V4_READ_HI = 0x04U,
        V4_TDMA_CONS = 0x08U,
        V4_TDMA_PROD = 0x0C,
        V4_START_HI = 0x18U,
        V4_END_HI = 0x20U,
        V4_TDMA_WRITE = 0x2CU,
        V4_WRITE_HI = 0x30U,
        TBUF_OFF = 0x0600U,
        TX_Q16_START = 128U
    };
    if (genet8_probed) return genet8_ok_val;
    genet8_probed = 1;
    genet8_ok_val = 0;
    genet8_prod_val = 0;
    genet8_cons_val = 0;
    genet8_tx_val = 0;
    genet8_frames_val = 0;

    if (!kernel_genet7_selftest()) return 0;
    if (!kernel_genet7_ring()) return 0;

    unsigned long mac = kernel_genet3_mac();
    if (!mac) return 0;

    // genet7 parked DMA. Stay disabled, leftover-flush, then v4 doorbell.
    rdma_common_wr(DMA_CTRL_OFF, 0);
    tdma_common_wr(DMA_CTRL_OFF, 0);
    if (!genet_wait_us(2000, rdma_is_disabled)) return 0;
    if (!genet_wait_us(2000, tdma_is_disabled)) return 0;
    genet_pulse32(UMAC_TX_FLUSH, 1, 0);

    uint32_t tbuf = G32(TBUF_OFF);
    if (tbuf != 0xFFFFFFFFU && tbuf != 0xDEADDEADU && (tbuf & RBUF_64B_EN)) {
        genet_wr32(TBUF_OFF, tbuf & ~RBUF_64B_EN);
    }

    unsigned long tx_pa = 0;
    void *tx_nc = 0;
    if (!kernel_dma_alloc_nc(&tx_pa, &tx_nc)) return 0;
    genet_write_arp((volatile uint8_t *)tx_nc, mac);
    __asm__ volatile("dsb sy" ::: "memory");

    unsigned int tx_bd = TDMA_OFF + TX_Q16_START * DESC_BYTES;
    uint32_t len_stat = ((uint32_t)TX_FRAME_LEN << 16) |
                        (DMA_QTAG_MASK << DMA_TX_QTAG_SHIFT) |
                        DMA_TX_APPEND_CRC | DMA_SOP | DMA_EOP;
    G32(tx_bd + 0U) = len_stat;
    G32(tx_bd + 4U) = (uint32_t)tx_pa;
    G32(tx_bd + 8U) = 0;
    __asm__ volatile("dsb sy" ::: "memory");

    uint32_t tx_start = TX_Q16_START * DESC_WORDS;
    rdma_ring16_wr(V4_READ_HI, 0);
    rdma_ring16_wr(V4_START_HI, 0);
    rdma_ring16_wr(V4_END_HI, 0);
    rdma_ring16_wr(V4_WRITE_HI, 0);
    tdma_ring16_wr(V4_TDMA_READ, tx_start);
    tdma_ring16_wr(V4_READ_HI, 0);
    tdma_ring16_wr(V4_START_HI, 0);
    tdma_ring16_wr(V4_END_HI, 0);
    tdma_ring16_wr(V4_TDMA_WRITE, tx_start);
    tdma_ring16_wr(V4_WRITE_HI, 0);
    tdma_ring16_wr(V4_TDMA_CONS, 0);
    tdma_ring16_wr(V4_TDMA_PROD, 0);

    rdma_common_wr(DMA_RING_CFG_OFF, (1U << DESC_INDEX));
    tdma_common_wr(DMA_RING_CFG_OFF, (1U << DESC_INDEX));
    rdma_common_wr(DMA_CTRL_OFF, DMA_EN | DMA_RING16_EN);
    tdma_common_wr(DMA_CTRL_OFF, DMA_EN | DMA_RING16_EN);

    uint32_t cmd = G32(UMAC_CMD);
    genet_wr32(UMAC_CMD, cmd | CMD_TX_EN | CMD_RX_EN | CMD_PROMISC);
    if ((G32(UMAC_CMD) & CMD_TX_EN) == 0U) return 0;

    tdma_ring16_wr(V4_TDMA_PROD, 1);
    genet8_prod_val = tdma_ring16(V4_TDMA_PROD) & 0xFFFFU;

    uint64_t freq, start, now, ticks;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(start));
    ticks = 50ULL * freq / 1000ULL;
    do {
        genet8_cons_val = tdma_ring16(V4_TDMA_CONS) & 0xFFFFU;
        genet8_prod_val = tdma_ring16(V4_TDMA_PROD) & 0xFFFFU;
        if (genet8_cons_val != 0U) break;
        genet_udelay(100);
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(now));
    } while (now - start < ticks);
    genet8_cons_val = tdma_ring16(V4_TDMA_CONS) & 0xFFFFU;
    genet8_prod_val = tdma_ring16(V4_TDMA_PROD) & 0xFFFFU;
    if (genet8_cons_val != 0U || genet8_prod_val != 0U) genet8_tx_val = 1;

    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(start));
    ticks = 1000ULL * freq / 1000ULL;
    do {
        uint32_t rxprod = rdma_ring16(V4_TDMA_CONS) & 0xFFFFU;
        genet8_frames_val = rxprod;
        if (rxprod != 0U) break;
        genet_udelay(1000);
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(now));
    } while (now - start < ticks);

    cmd = G32(UMAC_CMD);
    genet_wr32(UMAC_CMD, cmd & ~(CMD_RX_EN | CMD_TX_EN));
    rdma_common_wr(DMA_CTRL_OFF, 0);
    tdma_common_wr(DMA_CTRL_OFF, 0);
    genet_wait_us(2000, rdma_is_disabled);
    genet_wait_us(2000, tdma_is_disabled);

    genet8_ok_val = 1;
    return 1;
}

int          kernel_genet8_ok(void)     { return genet8_ok_val;     }
unsigned int kernel_genet8_prod(void)   { return genet8_prod_val;   }
unsigned int kernel_genet8_cons(void)   { return genet8_cons_val;   }
unsigned int kernel_genet8_tx(void)     { return genet8_tx_val;     }
unsigned int kernel_genet8_frames(void) { return genet8_frames_val; }

int kernel_genet9_selftest(void);

static int genet9_probed;
static int genet9_ok_val;
static unsigned int genet9_rx_val;
static unsigned int genet9_tx_val;
static unsigned int genet9_kind_val;

static uint16_t genet9_be16(const volatile uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t genet9_be32(const volatile uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void genet9_put16(volatile uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void genet9_put32(volatile uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void genet9_put_mac(volatile uint8_t *p, unsigned long mac) {
    p[0] = (uint8_t)((mac >> 40) & 0xFFUL);
    p[1] = (uint8_t)((mac >> 32) & 0xFFUL);
    p[2] = (uint8_t)((mac >> 24) & 0xFFUL);
    p[3] = (uint8_t)((mac >> 16) & 0xFFUL);
    p[4] = (uint8_t)((mac >> 8) & 0xFFUL);
    p[5] = (uint8_t)(mac & 0xFFUL);
}

static uint16_t genet9_csum(const volatile uint8_t *p, unsigned int len) {
    uint32_t s = 0;
    unsigned int i;
    for (i = 0; i + 1U < len; i += 2U) {
        s += ((uint32_t)p[i] << 8) | (uint32_t)p[i + 1U];
    }
    if (len & 1U) s += (uint32_t)p[len - 1U] << 8;
    while (s >> 16) s = (s & 0xFFFFU) + (s >> 16);
    return (uint16_t)~s;
}

// Build ARP reply or ICMP echo-reply into tx. Returns wire length, or 0.
static unsigned int genet9_build_reply(const volatile uint8_t *rx,
                                       unsigned int rx_len,
                                       volatile uint8_t *tx,
                                       unsigned long mac,
                                       unsigned int *kind_out) {
    enum { OUR_IP = 0x0a2a0002U };
    unsigned int i;
    *kind_out = 0;
    if (rx_len < 14U) return 0;
    uint16_t et = genet9_be16(rx + 12);
    if (et == 0x0806U) {
        if (rx_len < 42U) return 0;
        if (genet9_be16(rx + 20) != 1U) return 0;
        if (genet9_be32(rx + 38) != OUR_IP) return 0;
        *kind_out = 1;
        for (i = 0; i < 6U; i++) tx[i] = rx[22U + i];
        genet9_put_mac(tx + 6, mac);
        genet9_put16(tx + 12, 0x0806U);
        genet9_put16(tx + 14, 1);
        genet9_put16(tx + 16, 0x0800U);
        tx[18] = 6;
        tx[19] = 4;
        genet9_put16(tx + 20, 2);
        genet9_put_mac(tx + 22, mac);
        genet9_put32(tx + 28, OUR_IP);
        for (i = 0; i < 6U; i++) tx[32U + i] = rx[22U + i];
        for (i = 0; i < 4U; i++) tx[38U + i] = rx[28U + i];
        for (i = 42U; i < 60U; i++) tx[i] = 0;
        return 60U;
    }
    if (et == 0x0800U) {
        if (rx_len < 34U) return 0;
        if ((rx[14] >> 4) != 4U) return 0;
        unsigned int ihl = (unsigned int)(rx[14] & 0x0FU) * 4U;
        if (ihl < 20U) return 0;
        if (rx[14 + 9] != 1U) return 0;
        if (genet9_be32(rx + 14 + 16) != OUR_IP) return 0;
        unsigned int ip_len = genet9_be16(rx + 16);
        if (ip_len < ihl + 8U) return 0;
        unsigned int icmp_off = 14U + ihl;
        if (rx_len < icmp_off + 8U) return 0;
        if (rx[icmp_off] != 8U) return 0;
        unsigned int frame_len = 14U + ip_len;
        if (frame_len > 1514U) frame_len = 1514U;
        if (frame_len > rx_len) frame_len = rx_len;
        for (i = 0; i < frame_len; i++) tx[i] = rx[i];
        for (i = 0; i < 6U; i++) tx[i] = rx[6U + i];
        genet9_put_mac(tx + 6, mac);
        uint32_t src = genet9_be32(rx + 26);
        genet9_put32(tx + 26, OUR_IP);
        genet9_put32(tx + 30, src);
        tx[22] = 64;
        tx[24] = 0;
        tx[25] = 0;
        genet9_put16(tx + 24, genet9_csum(tx + 14, ihl));
        tx[icmp_off] = 0;
        tx[icmp_off + 2] = 0;
        tx[icmp_off + 3] = 0;
        unsigned int icmp_len = ip_len - ihl;
        if (icmp_off + icmp_len > frame_len) icmp_len = frame_len - icmp_off;
        genet9_put16(tx + icmp_off + 2, genet9_csum(tx + icmp_off, icmp_len));
        *kind_out = 2;
        return frame_len < 60U ? 60U : frame_len;
    }
    return 0;
}

// V76: parse one completed RX BD; reply to ARP request or ICMP echo-request.
int kernel_genet9_selftest(void) {
    enum {
        V4_TDMA_READ = 0x00U,
        V4_READ_HI = 0x04U,
        V4_TDMA_CONS = 0x08U,
        V4_RDMA_PROD = 0x08U,
        V4_TDMA_PROD = 0x0C,
        V4_START_HI = 0x18U,
        V4_END_HI = 0x20U,
        V4_TDMA_WRITE = 0x2CU,
        V4_WRITE_HI = 0x30U,
        TBUF_OFF = 0x0600U,
        TX_Q16_START = 128U
    };
    if (genet9_probed) return genet9_ok_val;
    genet9_probed = 1;
    genet9_ok_val = 0;
    genet9_rx_val = 0;
    genet9_tx_val = 0;
    genet9_kind_val = 0;

    if (!kernel_genet8_selftest()) return 0;

    unsigned long mac = kernel_genet3_mac();
    if (!mac) return 0;

    unsigned long tx_pa = 0;
    void *tx_nc = 0;
    if (!kernel_dma_alloc_nc(&tx_pa, &tx_nc)) return 0;

    rdma_common_wr(DMA_CTRL_OFF, 0);
    tdma_common_wr(DMA_CTRL_OFF, 0);
    if (!genet_wait_us(2000, rdma_is_disabled)) return 0;
    if (!genet_wait_us(2000, tdma_is_disabled)) return 0;
    genet_pulse32(UMAC_TX_FLUSH, 1, 0);

    uint32_t tbuf = G32(TBUF_OFF);
    if (tbuf != 0xFFFFFFFFU && tbuf != 0xDEADDEADU && (tbuf & RBUF_64B_EN)) {
        genet_wr32(TBUF_OFF, tbuf & ~RBUF_64B_EN);
    }

    uint32_t tx_start = TX_Q16_START * DESC_WORDS;
    tdma_ring16_wr(V4_TDMA_READ, tx_start);
    tdma_ring16_wr(V4_READ_HI, 0);
    tdma_ring16_wr(V4_START_HI, 0);
    tdma_ring16_wr(V4_END_HI, 0);
    tdma_ring16_wr(V4_TDMA_WRITE, tx_start);
    tdma_ring16_wr(V4_WRITE_HI, 0);
    tdma_ring16_wr(V4_TDMA_CONS, 0);
    tdma_ring16_wr(V4_TDMA_PROD, 0);

    rdma_common_wr(DMA_RING_CFG_OFF, (1U << DESC_INDEX));
    tdma_common_wr(DMA_RING_CFG_OFF, (1U << DESC_INDEX));
    rdma_common_wr(DMA_CTRL_OFF, DMA_EN | DMA_RING16_EN);
    tdma_common_wr(DMA_CTRL_OFF, DMA_EN | DMA_RING16_EN);

    uint32_t cmd = G32(UMAC_CMD);
    genet_wr32(UMAC_CMD, cmd | CMD_TX_EN | CMD_RX_EN | CMD_PROMISC);
    if ((G32(UMAC_CMD) & CMD_TX_EN) == 0U) return 0;

    uint64_t freq, start, now, ticks;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(start));
    ticks = 3000ULL * freq / 1000ULL;
    unsigned int cons = 0;
    unsigned int tx_len = 0;
    do {
        unsigned int prod = rdma_ring16(V4_RDMA_PROD) & 0xFFFFU;
        while (cons < prod && cons < 8U) {
            unsigned int bd = RDMA_OFF + cons * DESC_BYTES;
            unsigned int rx_len = (G32(bd) >> 16) & 0x0FFFU;
            unsigned long buf_pa = (unsigned long)G32(bd + 4U);
            void *rx_nc = 0;
            if (rx_len >= 14U && kernel_dma_nc_from_pa(buf_pa, &rx_nc) && rx_nc) {
                tx_len = genet9_build_reply((const volatile uint8_t *)rx_nc,
                                            rx_len,
                                            (volatile uint8_t *)tx_nc,
                                            mac,
                                            &genet9_kind_val);
                if (genet9_kind_val != 0U) {
                    genet9_rx_val = 1;
                    break;
                }
            }
            cons++;
        }
        if (genet9_kind_val != 0U) break;
        genet_udelay(1000);
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(now));
    } while (now - start < ticks);

    if (genet9_kind_val != 0U && tx_len != 0U) {
        __asm__ volatile("dsb sy" ::: "memory");
        unsigned int tx_bd = TDMA_OFF + TX_Q16_START * DESC_BYTES;
        uint32_t len_stat = ((uint32_t)tx_len << 16) |
                            (DMA_QTAG_MASK << DMA_TX_QTAG_SHIFT) |
                            DMA_TX_APPEND_CRC | DMA_SOP | DMA_EOP;
        G32(tx_bd + 0U) = len_stat;
        G32(tx_bd + 4U) = (uint32_t)tx_pa;
        G32(tx_bd + 8U) = 0;
        __asm__ volatile("dsb sy" ::: "memory");
        tdma_ring16_wr(V4_TDMA_PROD, 1);

        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(start));
        ticks = 50ULL * freq / 1000ULL;
        unsigned int tcons = 0;
        unsigned int tprod = 0;
        do {
            tcons = tdma_ring16(V4_TDMA_CONS) & 0xFFFFU;
            tprod = tdma_ring16(V4_TDMA_PROD) & 0xFFFFU;
            if (tcons != 0U) break;
            genet_udelay(100);
            __asm__ volatile("mrs %0, cntpct_el0" : "=r"(now));
        } while (now - start < ticks);
        tcons = tdma_ring16(V4_TDMA_CONS) & 0xFFFFU;
        tprod = tdma_ring16(V4_TDMA_PROD) & 0xFFFFU;
        if (tcons != 0U || tprod != 0U) genet9_tx_val = 1;
    }

    cmd = G32(UMAC_CMD);
    genet_wr32(UMAC_CMD, cmd & ~(CMD_RX_EN | CMD_TX_EN));
    rdma_common_wr(DMA_CTRL_OFF, 0);
    tdma_common_wr(DMA_CTRL_OFF, 0);
    genet_wait_us(2000, rdma_is_disabled);
    genet_wait_us(2000, tdma_is_disabled);

    genet9_ok_val = 1;
    return 1;
}

int          kernel_genet9_ok(void)   { return genet9_ok_val;   }
unsigned int kernel_genet9_rx(void)   { return genet9_rx_val;   }
unsigned int kernel_genet9_tx(void)   { return genet9_tx_val;   }
unsigned int kernel_genet9_kind(void) { return genet9_kind_val; }

int kernel_genet10_selftest(void);

static int genet10_inited;
static int genet10_ok_val;
static unsigned int genet10_rx_val;
static unsigned int genet10_tx_val;
static unsigned int genet10_replies_val;
static unsigned int genet10_kind_val;
static unsigned long genet10_tx_pa;
static void *genet10_tx_nc;

static int genet10_unpark(void) {
    enum {
        V4_TDMA_READ = 0x00U,
        V4_READ_HI = 0x04U,
        V4_TDMA_CONS = 0x08U,
        V4_TDMA_PROD = 0x0C,
        V4_START_HI = 0x18U,
        V4_END_HI = 0x20U,
        V4_TDMA_WRITE = 0x2CU,
        V4_WRITE_HI = 0x30U,
        TBUF_OFF = 0x0600U,
        TX_Q16_START = 128U
    };
    rdma_common_wr(DMA_CTRL_OFF, 0);
    tdma_common_wr(DMA_CTRL_OFF, 0);
    if (!genet_wait_us(2000, rdma_is_disabled)) return 0;
    if (!genet_wait_us(2000, tdma_is_disabled)) return 0;
    genet_pulse32(UMAC_TX_FLUSH, 1, 0);

    uint32_t tbuf = G32(TBUF_OFF);
    if (tbuf != 0xFFFFFFFFU && tbuf != 0xDEADDEADU && (tbuf & RBUF_64B_EN)) {
        genet_wr32(TBUF_OFF, tbuf & ~RBUF_64B_EN);
    }

    uint32_t tx_start = TX_Q16_START * DESC_WORDS;
    tdma_ring16_wr(V4_TDMA_READ, tx_start);
    tdma_ring16_wr(V4_READ_HI, 0);
    tdma_ring16_wr(V4_START_HI, 0);
    tdma_ring16_wr(V4_END_HI, 0);
    tdma_ring16_wr(V4_TDMA_WRITE, tx_start);
    tdma_ring16_wr(V4_WRITE_HI, 0);
    tdma_ring16_wr(V4_TDMA_CONS, 0);
    tdma_ring16_wr(V4_TDMA_PROD, 0);

    rdma_common_wr(DMA_RING_CFG_OFF, (1U << DESC_INDEX));
    tdma_common_wr(DMA_RING_CFG_OFF, (1U << DESC_INDEX));
    rdma_common_wr(DMA_CTRL_OFF, DMA_EN | DMA_RING16_EN);
    tdma_common_wr(DMA_CTRL_OFF, DMA_EN | DMA_RING16_EN);

    uint32_t cmd = G32(UMAC_CMD);
    genet_wr32(UMAC_CMD, cmd | CMD_TX_EN | CMD_RX_EN | CMD_PROMISC);
    return ((G32(UMAC_CMD) & CMD_TX_EN) != 0U) ? 1 : 0;
}

static void genet10_park(void) {
    uint32_t cmd = G32(UMAC_CMD);
    genet_wr32(UMAC_CMD, cmd & ~(CMD_RX_EN | CMD_TX_EN));
    rdma_common_wr(DMA_CTRL_OFF, 0);
    tdma_common_wr(DMA_CTRL_OFF, 0);
    genet_wait_us(2000, rdma_is_disabled);
    genet_wait_us(2000, tdma_is_disabled);
}

// V77: re-runnable bounded poll. Do not leave the 256-BD ring running.
int kernel_genet10_selftest(void) {
    if (genet10_inited) return genet10_ok_val;
    genet10_inited = 1;
    genet10_ok_val = 0;
    genet10_rx_val = 0;
    genet10_tx_val = 0;
    genet10_replies_val = 0;
    genet10_kind_val = 0;
    if (!kernel_genet9_selftest()) return 0;
    if (!kernel_dma_alloc_nc(&genet10_tx_pa, &genet10_tx_nc)) return 0;
    genet10_ok_val = 1;
    return 1;
}

int kernel_genet10_poll(void) {
    enum {
        V4_RDMA_PROD = 0x08U,
        V4_TDMA_CONS = 0x08U,
        V4_RDMA_CONS = 0x0C,
        V4_TDMA_PROD = 0x0C,
        TX_Q16_START = 128U,
        TX_Q16_N = 128U,
        RX_Q16_N = 256U
    };
    if (!kernel_genet10_selftest()) return 0;

    genet10_rx_val = 0;
    genet10_tx_val = 0;
    genet10_replies_val = 0;
    genet10_kind_val = 0;

    unsigned long mac = kernel_genet3_mac();
    if (!mac || !genet10_tx_nc) return 0;
    if (!genet10_unpark()) return 0;

    unsigned int cons = rdma_ring16(V4_RDMA_PROD) & 0xFFFFU;
    rdma_ring16_wr(V4_RDMA_CONS, cons);

    uint64_t freq, start, now, ticks;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(start));
    ticks = 4000ULL * freq / 1000ULL;
    unsigned int tx_prod = 0;
    unsigned int processed = 0;

    do {
        unsigned int prod = rdma_ring16(V4_RDMA_PROD) & 0xFFFFU;
        while (cons != prod && processed < 16U) {
            unsigned int idx = cons & (RX_Q16_N - 1U);
            unsigned int bd = RDMA_OFF + idx * DESC_BYTES;
            unsigned int rx_len = (G32(bd) >> 16) & 0x0FFFU;
            unsigned long buf_pa = (unsigned long)G32(bd + 4U);
            void *rx_nc = 0;
            unsigned int kind = 0;
            unsigned int tx_len = 0;
            genet10_rx_val++;
            processed++;
            if (rx_len >= 14U && kernel_dma_nc_from_pa(buf_pa, &rx_nc) && rx_nc) {
                unsigned int half = (tx_prod & 1U) * RX_BUF_LEN;
                volatile uint8_t *tx = (volatile uint8_t *)genet10_tx_nc + half;
                tx_len = genet9_build_reply((const volatile uint8_t *)rx_nc,
                                            rx_len, tx, mac, &kind);
            }
            cons = (cons + 1U) & 0xFFFFU;
            rdma_ring16_wr(V4_RDMA_CONS, cons);
            if (kind != 0U && tx_len != 0U && tx_prod < TX_Q16_N) {
                genet10_kind_val = kind;
                __asm__ volatile("dsb sy" ::: "memory");
                unsigned int tx_bd = TDMA_OFF +
                    (TX_Q16_START + (tx_prod % TX_Q16_N)) * DESC_BYTES;
                uint32_t len_stat = ((uint32_t)tx_len << 16) |
                                    (DMA_QTAG_MASK << DMA_TX_QTAG_SHIFT) |
                                    DMA_TX_APPEND_CRC | DMA_SOP | DMA_EOP;
                G32(tx_bd + 0U) = len_stat;
                G32(tx_bd + 4U) = (uint32_t)(genet10_tx_pa +
                    (unsigned long)((tx_prod & 1U) * RX_BUF_LEN));
                G32(tx_bd + 8U) = 0;
                __asm__ volatile("dsb sy" ::: "memory");
                tx_prod++;
                tdma_ring16_wr(V4_TDMA_PROD, tx_prod);

                uint64_t t0, tnow, twait;
                __asm__ volatile("mrs %0, cntpct_el0" : "=r"(t0));
                twait = 20ULL * freq / 1000ULL;
                unsigned int tcons = 0;
                do {
                    tcons = tdma_ring16(V4_TDMA_CONS) & 0xFFFFU;
                    if (tcons == tx_prod) break;
                    genet_udelay(100);
                    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(tnow));
                } while (tnow - t0 < twait);
                tcons = tdma_ring16(V4_TDMA_CONS) & 0xFFFFU;
                unsigned int tprod = tdma_ring16(V4_TDMA_PROD) & 0xFFFFU;
                if (tcons != 0U || tprod != 0U) {
                    genet10_tx_val = 1;
                    genet10_replies_val++;
                }
            }
        }
        if (genet10_replies_val >= 2U) break;
        genet_udelay(1000);
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(now));
    } while (now - start < ticks);

    genet10_park();
    genet10_ok_val = 1;
    return 1;
}

int          kernel_genet10_ok(void)      { return genet10_ok_val;      }
unsigned int kernel_genet10_rx(void)      { return genet10_rx_val;      }
unsigned int kernel_genet10_tx(void)      { return genet10_tx_val;      }
unsigned int kernel_genet10_replies(void) { return genet10_replies_val; }
unsigned int kernel_genet10_kind(void)    { return genet10_kind_val;    }

int kernel_genet11_selftest(void);

static int genet11_inited;
static int genet11_ok_val;
static unsigned int genet11_rx_val;
static unsigned int genet11_tx_val;
static unsigned int genet11_replies_val;
static unsigned int genet11_kind_val;
static unsigned long genet11_tx_pa;
static void *genet11_tx_nc;

// Build UDP echo (port 7) into tx. Returns wire length, or 0. kind 3=udp.
static unsigned int genet11_build_udp_echo(const volatile uint8_t *rx,
                                           unsigned int rx_len,
                                           volatile uint8_t *tx,
                                           unsigned long mac,
                                           unsigned int *kind_out) {
    enum { OUR_IP = 0x0a2a0002U, ECHO_PORT = 7U };
    unsigned int i;
    *kind_out = 0;
    if (rx_len < 42U) return 0;
    if (genet9_be16(rx + 12) != 0x0800U) return 0;
    if ((rx[14] >> 4) != 4U) return 0;
    unsigned int ihl = (unsigned int)(rx[14] & 0x0FU) * 4U;
    if (ihl < 20U) return 0;
    if (rx[14 + 9] != 0x11U) return 0;
    if (genet9_be32(rx + 14 + 16) != OUR_IP) return 0;
    unsigned int ip_len = genet9_be16(rx + 16);
    if (ip_len < ihl + 8U) return 0;
    unsigned int udp_off = 14U + ihl;
    if (rx_len < udp_off + 8U) return 0;
    if (genet9_be16(rx + udp_off + 2) != ECHO_PORT) return 0;
    unsigned int udp_len = genet9_be16(rx + udp_off + 4);
    if (udp_len < 8U) return 0;
    unsigned int wire = 14U + ihl + udp_len;
    if (wire > 1514U || wire > rx_len) return 0;
    for (i = 0; i < wire; i++) tx[i] = rx[i];
    for (i = 0; i < 6U; i++) tx[i] = rx[6U + i];
    genet9_put_mac(tx + 6, mac);
    uint32_t src = genet9_be32(rx + 26);
    genet9_put32(tx + 26, OUR_IP);
    genet9_put32(tx + 30, src);
    tx[22] = 64;
    tx[24] = 0;
    tx[25] = 0;
    genet9_put16(tx + 24, genet9_csum(tx + 14, ihl));
    uint16_t sport = genet9_be16(rx + udp_off);
    genet9_put16(tx + udp_off, ECHO_PORT);
    genet9_put16(tx + udp_off + 2, sport);
    tx[udp_off + 6] = 0;
    tx[udp_off + 7] = 0;
    uint32_t s = 0;
    s += (OUR_IP >> 16) & 0xFFFFU;
    s += OUR_IP & 0xFFFFU;
    s += (src >> 16) & 0xFFFFU;
    s += src & 0xFFFFU;
    s += 0x11U;
    s += udp_len;
    for (i = 0; i + 1U < udp_len; i += 2U) {
        s += genet9_be16(tx + udp_off + i);
    }
    if (udp_len & 1U) s += (uint32_t)tx[udp_off + udp_len - 1U] << 8;
    while (s >> 16) s = (s & 0xFFFFU) + (s >> 16);
    uint16_t ucsum = (uint16_t)~s;
    if (ucsum == 0U) ucsum = 0xFFFFU;
    genet9_put16(tx + udp_off + 6, ucsum);
    *kind_out = 3;
    for (i = wire; i < 60U; i++) tx[i] = 0;
    return wire < 60U ? 60U : wire;
}

// V78: re-runnable bounded UDP echo. Do not leave the 256-BD ring running.
int kernel_genet11_selftest(void) {
    if (genet11_inited) return genet11_ok_val;
    genet11_inited = 1;
    genet11_ok_val = 0;
    genet11_rx_val = 0;
    genet11_tx_val = 0;
    genet11_replies_val = 0;
    genet11_kind_val = 0;
    if (!kernel_genet10_selftest()) return 0;
    if (!kernel_dma_alloc_nc(&genet11_tx_pa, &genet11_tx_nc)) return 0;
    genet11_ok_val = 1;
    return 1;
}

int kernel_genet11_poll(void) {
    enum {
        V4_RDMA_PROD = 0x08U,
        V4_TDMA_CONS = 0x08U,
        V4_RDMA_CONS = 0x0C,
        V4_TDMA_PROD = 0x0C,
        TX_Q16_START = 128U,
        TX_Q16_N = 128U,
        RX_Q16_N = 256U
    };
    if (!kernel_genet11_selftest()) return 0;

    genet11_rx_val = 0;
    genet11_tx_val = 0;
    genet11_replies_val = 0;
    genet11_kind_val = 0;

    unsigned long mac = kernel_genet3_mac();
    if (!mac || !genet11_tx_nc) return 0;
    if (!genet10_unpark()) return 0;

    unsigned int cons = rdma_ring16(V4_RDMA_PROD) & 0xFFFFU;
    rdma_ring16_wr(V4_RDMA_CONS, cons);

    uint64_t freq, start, now, ticks;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(start));
    ticks = 4000ULL * freq / 1000ULL;
    unsigned int tx_prod = 0;
    unsigned int processed = 0;

    do {
        unsigned int prod = rdma_ring16(V4_RDMA_PROD) & 0xFFFFU;
        while (cons != prod && processed < 16U) {
            unsigned int idx = cons & (RX_Q16_N - 1U);
            unsigned int bd = RDMA_OFF + idx * DESC_BYTES;
            unsigned int rx_len = (G32(bd) >> 16) & 0x0FFFU;
            unsigned long buf_pa = (unsigned long)G32(bd + 4U);
            void *rx_nc = 0;
            unsigned int kind = 0;
            unsigned int tx_len = 0;
            genet11_rx_val++;
            processed++;
            if (rx_len >= 42U && kernel_dma_nc_from_pa(buf_pa, &rx_nc) && rx_nc) {
                unsigned int half = (tx_prod & 1U) * RX_BUF_LEN;
                volatile uint8_t *tx = (volatile uint8_t *)genet11_tx_nc + half;
                tx_len = genet11_build_udp_echo((const volatile uint8_t *)rx_nc,
                                                rx_len, tx, mac, &kind);
            }
            cons = (cons + 1U) & 0xFFFFU;
            rdma_ring16_wr(V4_RDMA_CONS, cons);
            if (kind != 0U && tx_len != 0U && tx_prod < TX_Q16_N) {
                genet11_kind_val = kind;
                __asm__ volatile("dsb sy" ::: "memory");
                unsigned int tx_bd = TDMA_OFF +
                    (TX_Q16_START + (tx_prod % TX_Q16_N)) * DESC_BYTES;
                uint32_t len_stat = ((uint32_t)tx_len << 16) |
                                    (DMA_QTAG_MASK << DMA_TX_QTAG_SHIFT) |
                                    DMA_TX_APPEND_CRC | DMA_SOP | DMA_EOP;
                G32(tx_bd + 0U) = len_stat;
                G32(tx_bd + 4U) = (uint32_t)(genet11_tx_pa +
                    (unsigned long)((tx_prod & 1U) * RX_BUF_LEN));
                G32(tx_bd + 8U) = 0;
                __asm__ volatile("dsb sy" ::: "memory");
                tx_prod++;
                tdma_ring16_wr(V4_TDMA_PROD, tx_prod);

                uint64_t t0, tnow, twait;
                __asm__ volatile("mrs %0, cntpct_el0" : "=r"(t0));
                twait = 20ULL * freq / 1000ULL;
                unsigned int tcons = 0;
                do {
                    tcons = tdma_ring16(V4_TDMA_CONS) & 0xFFFFU;
                    if (tcons == tx_prod) break;
                    genet_udelay(100);
                    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(tnow));
                } while (tnow - t0 < twait);
                tcons = tdma_ring16(V4_TDMA_CONS) & 0xFFFFU;
                unsigned int tprod = tdma_ring16(V4_TDMA_PROD) & 0xFFFFU;
                if (tcons != 0U || tprod != 0U) {
                    genet11_tx_val = 1;
                    genet11_replies_val++;
                }
            }
        }
        if (genet11_replies_val >= 2U) break;
        genet_udelay(1000);
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(now));
    } while (now - start < ticks);

    genet10_park();
    genet11_ok_val = 1;
    return 1;
}

int          kernel_genet11_ok(void)      { return genet11_ok_val;      }
unsigned int kernel_genet11_rx(void)      { return genet11_rx_val;      }
unsigned int kernel_genet11_tx(void)      { return genet11_tx_val;      }
unsigned int kernel_genet11_replies(void) { return genet11_replies_val; }
unsigned int kernel_genet11_kind(void)    { return genet11_kind_val;    }

int kernel_genet12_selftest(void);

static int genet12_inited;
static int genet12_ok_val;
static unsigned int genet12_rx_val;
static unsigned int genet12_tx_val;
static unsigned int genet12_replies_val;
static unsigned int genet12_kind_val;
static unsigned long genet12_tx_pa;
static void *genet12_tx_nc;
static int genet12_got_syn;
static int genet12_echoed;
static uint32_t genet12_peer_ip;
static uint16_t genet12_peer_port;
static uint32_t genet12_our_seq;
static uint32_t genet12_our_ack;

// One frame only: core0 stack is 4 KiB. SYN -> SYN-ACK (0x12). Payload -> echo.
static unsigned int genet12_process(const volatile uint8_t *rx,
                                    unsigned int rx_len,
                                    volatile uint8_t *tx,
                                    unsigned long mac,
                                    unsigned int *kind_out) {
    enum { OUR_IP = 0x0a2a0002U, ECHO_PORT = 7U, ISN = 0x10000000U };
    unsigned int i;
    uint8_t flags;
    uint32_t seq;
    uint32_t src;
    uint32_t s;
    uint16_t sport;
    uint16_t csum;
    unsigned int ihl;
    unsigned int ip_len;
    unsigned int tcp_off;
    unsigned int doff;
    unsigned int plen;
    unsigned int tcp_len;
    unsigned int wire;
    *kind_out = 0;
    if (rx_len < 54U) return 0;
    if (genet9_be16(rx + 12) != 0x0800U) return 0;
    if ((rx[14] >> 4) != 4U) return 0;
    ihl = (unsigned int)(rx[14] & 0x0FU) * 4U;
    if (ihl < 20U) return 0;
    if (rx[14 + 9] != 6U) return 0;
    if (genet9_be32(rx + 14 + 16) != OUR_IP) return 0;
    ip_len = genet9_be16(rx + 16);
    if (ip_len < ihl + 20U) return 0;
    tcp_off = 14U + ihl;
    if (rx_len < tcp_off + 20U) return 0;
    if (genet9_be16(rx + tcp_off + 2) != ECHO_PORT) return 0;
    doff = ((unsigned int)(rx[tcp_off + 12] >> 4) & 0x0FU) * 4U;
    if (doff < 20U || tcp_off + doff > rx_len) return 0;
    if (ip_len < ihl + doff) return 0;
    plen = ip_len - ihl - doff;
    if (tcp_off + doff + plen > rx_len) plen = rx_len - tcp_off - doff;
    if (plen > 256U) plen = 256U;
    flags = rx[tcp_off + 13];
    seq = genet9_be32(rx + tcp_off + 4);
    sport = genet9_be16(rx + tcp_off);
    src = genet9_be32(rx + 26);
    if ((flags & 0x02U) != 0U && (flags & 0x10U) == 0U) {
        genet12_peer_ip = src;
        genet12_peer_port = sport;
        genet12_our_seq = ISN + 1U;
        genet12_our_ack = seq + 1U;
        genet12_got_syn = 1;
        flags = 0x12;
        seq = ISN;
        plen = 0;
    } else if (!genet12_got_syn) {
        return 0;
    } else if (src != genet12_peer_ip || sport != genet12_peer_port) {
        return 0;
    } else if (plen > 0U) {
        genet12_our_ack = seq + plen;
        seq = genet12_our_seq;
        genet12_our_seq += plen;
        genet12_echoed = 1;
        *kind_out = 4;
        flags = 0x18;
    } else if ((flags & 0x01U) != 0U) {
        genet12_our_ack = seq + 1U;
        seq = genet12_our_seq;
        flags = 0x11;
        plen = 0;
    } else {
        return 0;
    }
    tcp_len = 20U + plen;
    wire = 34U + tcp_len;
    for (i = 0; i < 6U; i++) tx[i] = rx[6U + i];
    genet9_put_mac(tx + 6, mac);
    genet9_put16(tx + 12, 0x0800U);
    tx[14] = 0x45;
    tx[15] = 0;
    genet9_put16(tx + 16, (uint16_t)(20U + tcp_len));
    genet9_put16(tx + 18, 0);
    genet9_put16(tx + 20, 0);
    tx[22] = 64;
    tx[23] = 6U;
    tx[24] = 0;
    tx[25] = 0;
    genet9_put32(tx + 26, OUR_IP);
    genet9_put32(tx + 30, src);
    genet9_put16(tx + 24, genet9_csum(tx + 14, 20U));
    genet9_put16(tx + 34, ECHO_PORT);
    genet9_put16(tx + 36, sport);
    genet9_put32(tx + 38, seq);
    genet9_put32(tx + 42, genet12_our_ack);
    tx[46] = 0x50;
    tx[47] = flags;
    genet9_put16(tx + 48, 1024);
    tx[50] = 0;
    tx[51] = 0;
    tx[52] = 0;
    tx[53] = 0;
    for (i = 0; i < plen; i++) tx[54U + i] = rx[tcp_off + doff + i];
    s = 0;
    s += (OUR_IP >> 16) & 0xFFFFU;
    s += OUR_IP & 0xFFFFU;
    s += (src >> 16) & 0xFFFFU;
    s += src & 0xFFFFU;
    s += 6U;
    s += tcp_len;
    for (i = 0; i + 1U < tcp_len; i += 2U) s += genet9_be16(tx + 34U + i);
    if (tcp_len & 1U) s += (uint32_t)tx[34U + tcp_len - 1U] << 8;
    while (s >> 16) s = (s & 0xFFFFU) + (s >> 16);
    csum = (uint16_t)~s;
    if (csum == 0U) csum = 0xFFFFU;
    genet9_put16(tx + 50, csum);
    for (i = wire; i < 60U; i++) tx[i] = 0;
    return wire < 60U ? 60U : wire;
}

// V79: re-runnable bounded TCP echo. Do not leave the 256-BD ring running.
int kernel_genet12_selftest(void) {
    if (genet12_inited) return genet12_ok_val;
    genet12_inited = 1;
    genet12_ok_val = 0;
    genet12_rx_val = 0;
    genet12_tx_val = 0;
    genet12_replies_val = 0;
    genet12_kind_val = 0;
    if (!kernel_genet11_selftest()) return 0;
    if (!kernel_dma_alloc_nc(&genet12_tx_pa, &genet12_tx_nc)) return 0;
    genet12_ok_val = 1;
    return 1;
}

int kernel_genet12_poll(void) {
    enum {
        V4_RDMA_PROD = 0x08U,
        V4_TDMA_CONS = 0x08U,
        V4_RDMA_CONS = 0x0C,
        V4_TDMA_PROD = 0x0C,
        TX_Q16_START = 128U,
        TX_Q16_N = 128U,
        RX_Q16_N = 256U
    };
    if (!kernel_genet12_selftest()) return 0;

    genet12_rx_val = 0;
    genet12_tx_val = 0;
    genet12_replies_val = 0;
    genet12_kind_val = 0;
    genet12_got_syn = 0;
    genet12_echoed = 0;

    unsigned long mac = kernel_genet3_mac();
    if (!mac || !genet12_tx_nc) return 0;
    if (!genet10_unpark()) return 0;

    unsigned int cons = rdma_ring16(V4_RDMA_PROD) & 0xFFFFU;
    rdma_ring16_wr(V4_RDMA_CONS, cons);

    uint64_t freq, start, now, ticks;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(start));
    ticks = 4000ULL * freq / 1000ULL;
    unsigned int tx_prod = 0;
    unsigned int processed = 0;

    do {
        unsigned int prod = rdma_ring16(V4_RDMA_PROD) & 0xFFFFU;
        while (cons != prod && processed < 16U) {
            unsigned int idx = cons & (RX_Q16_N - 1U);
            unsigned int bd = RDMA_OFF + idx * DESC_BYTES;
            unsigned int rx_len = (G32(bd) >> 16) & 0x0FFFU;
            unsigned long buf_pa = (unsigned long)G32(bd + 4U);
            void *rx_nc = 0;
            unsigned int kind = 0;
            unsigned int tx_len = 0;
            genet12_rx_val++;
            processed++;
            if (rx_len >= 54U && kernel_dma_nc_from_pa(buf_pa, &rx_nc) && rx_nc) {
                unsigned int half = (tx_prod & 1U) * RX_BUF_LEN;
                volatile uint8_t *tx = (volatile uint8_t *)genet12_tx_nc + half;
                tx_len = genet12_process((const volatile uint8_t *)rx_nc,
                                         rx_len, tx, mac, &kind);
            }
            cons = (cons + 1U) & 0xFFFFU;
            rdma_ring16_wr(V4_RDMA_CONS, cons);
            if (tx_len != 0U && tx_prod < TX_Q16_N) {
                if (kind != 0U) genet12_kind_val = kind;
                __asm__ volatile("dsb sy" ::: "memory");
                unsigned int tx_bd = TDMA_OFF +
                    (TX_Q16_START + (tx_prod % TX_Q16_N)) * DESC_BYTES;
                uint32_t len_stat = ((uint32_t)tx_len << 16) |
                                    (DMA_QTAG_MASK << DMA_TX_QTAG_SHIFT) |
                                    DMA_TX_APPEND_CRC | DMA_SOP | DMA_EOP;
                G32(tx_bd + 0U) = len_stat;
                G32(tx_bd + 4U) = (uint32_t)(genet12_tx_pa +
                    (unsigned long)((tx_prod & 1U) * RX_BUF_LEN));
                G32(tx_bd + 8U) = 0;
                __asm__ volatile("dsb sy" ::: "memory");
                tx_prod++;
                tdma_ring16_wr(V4_TDMA_PROD, tx_prod);

                uint64_t t0, tnow, twait;
                __asm__ volatile("mrs %0, cntpct_el0" : "=r"(t0));
                twait = 20ULL * freq / 1000ULL;
                unsigned int tcons = 0;
                do {
                    tcons = tdma_ring16(V4_TDMA_CONS) & 0xFFFFU;
                    if (tcons == tx_prod) break;
                    genet_udelay(100);
                    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(tnow));
                } while (tnow - t0 < twait);
                tcons = tdma_ring16(V4_TDMA_CONS) & 0xFFFFU;
                unsigned int tprod = tdma_ring16(V4_TDMA_PROD) & 0xFFFFU;
                if (tcons != 0U || tprod != 0U) {
                    genet12_tx_val = 1;
                    genet12_replies_val++;
                }
            }
        }
        if (genet12_echoed) break;
        genet_udelay(1000);
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(now));
    } while (now - start < ticks);

    genet10_park();
    genet12_ok_val = 1;
    return 1;
}

int          kernel_genet12_ok(void)      { return genet12_ok_val;      }
unsigned int kernel_genet12_rx(void)      { return genet12_rx_val;      }
unsigned int kernel_genet12_tx(void)      { return genet12_tx_val;      }
unsigned int kernel_genet12_replies(void) { return genet12_replies_val; }
unsigned int kernel_genet12_kind(void)    { return genet12_kind_val;    }

// V117: originate ARP who-has 10.42.0.1, then ICMP echo. Bounded
// unpark/poll/park. No standing 256-BD ring. TX buffer is a DMA NC
// page — not the 4 KiB core0 stack. No EL0. No boot event emit.
int kernel_genet13_selftest(void);

static int genet13_probed;
static int genet13_ok_val;
static unsigned int genet13_arp_val;
static unsigned int genet13_echo_val;
static unsigned long genet13_peer_mac_val;
static unsigned long genet13_tx_pa;
static void *genet13_tx_nc;

static int genet13_tx_frame(unsigned int tx_len, unsigned int tx_prod) {
    enum {
        V4_TDMA_CONS = 0x08U,
        V4_TDMA_PROD = 0x0C,
        TX_Q16_START = 128U,
        TX_Q16_N = 128U
    };
    uint64_t freq, t0, tnow, twait;
    unsigned int tcons;
    unsigned int tx_bd;
    uint32_t len_stat;

    if (tx_len < 60U || !genet13_tx_nc) return 0;
    __asm__ volatile("dsb sy" ::: "memory");
    tx_bd = TDMA_OFF + (TX_Q16_START + (tx_prod % TX_Q16_N)) * DESC_BYTES;
    len_stat = ((uint32_t)tx_len << 16) |
               (DMA_QTAG_MASK << DMA_TX_QTAG_SHIFT) |
               DMA_TX_APPEND_CRC | DMA_SOP | DMA_EOP;
    G32(tx_bd + 0U) = len_stat;
    G32(tx_bd + 4U) = (uint32_t)genet13_tx_pa;
    G32(tx_bd + 8U) = 0;
    __asm__ volatile("dsb sy" ::: "memory");
    tdma_ring16_wr(V4_TDMA_PROD, tx_prod + 1U);

    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(t0));
    twait = 20ULL * freq / 1000ULL;
    do {
        tcons = tdma_ring16(V4_TDMA_CONS) & 0xFFFFU;
        if (tcons == (tx_prod + 1U)) return 1;
        genet_udelay(100);
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(tnow));
    } while (tnow - t0 < twait);
    tcons = tdma_ring16(V4_TDMA_CONS) & 0xFFFFU;
    return (tcons != 0U) ? 1 : 0;
}

static unsigned int genet13_build_icmp(volatile uint8_t *tx,
                                       unsigned long mac,
                                       unsigned long peer_mac) {
    enum { OUR_IP = 0x0a2a0002U, HOST_IP = 0x0a2a0001U, ICMP_ID = 0xA117U };
    unsigned int i;
    for (i = 0; i < 6U; i++) {
        tx[i] = (uint8_t)((peer_mac >> (40U - 8U * i)) & 0xFFUL);
        tx[6U + i] = (uint8_t)((mac >> (40U - 8U * i)) & 0xFFUL);
    }
    genet9_put16(tx + 12, 0x0800U);
    tx[14] = 0x45;
    tx[15] = 0;
    genet9_put16(tx + 16, 36);
    genet9_put16(tx + 18, ICMP_ID);
    genet9_put16(tx + 20, 0);
    tx[22] = 64;
    tx[23] = 1;
    tx[24] = 0;
    tx[25] = 0;
    genet9_put32(tx + 26, OUR_IP);
    genet9_put32(tx + 30, HOST_IP);
    genet9_put16(tx + 24, genet9_csum(tx + 14, 20U));
    tx[34] = 8;
    tx[35] = 0;
    tx[36] = 0;
    tx[37] = 0;
    genet9_put16(tx + 38, ICMP_ID);
    genet9_put16(tx + 40, 1);
    genet9_put32(tx + 42, 0xA1170001U);
    genet9_put32(tx + 46, 0);
    genet9_put16(tx + 36, genet9_csum(tx + 34, 16U));
    for (i = 50U; i < 60U; i++) tx[i] = 0;
    return 60U;
}

static int genet13_arp_reply(const volatile uint8_t *rx,
                             unsigned int rx_len,
                             unsigned long *peer_mac) {
    enum { HOST_IP = 0x0a2a0001U };
    unsigned int i;
    unsigned long mac = 0;
    if (rx_len < 42U) return 0;
    if (genet9_be16(rx + 12) != 0x0806U) return 0;
    if (genet9_be16(rx + 20) != 2U) return 0;
    if (genet9_be32(rx + 28) != HOST_IP) return 0;
    for (i = 0; i < 6U; i++) {
        mac = (mac << 8) | (unsigned long)rx[22U + i];
    }
    if (mac == 0UL) return 0;
    if (peer_mac) *peer_mac = mac;
    return 1;
}

static int genet13_icmp_reply(const volatile uint8_t *rx, unsigned int rx_len) {
    enum { OUR_IP = 0x0a2a0002U, HOST_IP = 0x0a2a0001U, ICMP_ID = 0xA117U };
    unsigned int ihl;
    unsigned int icmp_off;
    if (rx_len < 42U) return 0;
    if (genet9_be16(rx + 12) != 0x0800U) return 0;
    if ((rx[14] >> 4) != 4U) return 0;
    ihl = (unsigned int)(rx[14] & 0x0FU) * 4U;
    if (ihl < 20U) return 0;
    if (rx[14 + 9] != 1U) return 0;
    if (genet9_be32(rx + 26) != HOST_IP) return 0;
    if (genet9_be32(rx + 30) != OUR_IP) return 0;
    icmp_off = 14U + ihl;
    if (rx_len < icmp_off + 8U) return 0;
    if (rx[icmp_off] != 0U) return 0;
    if (genet9_be16(rx + icmp_off + 4) != ICMP_ID) return 0;
    if (genet9_be16(rx + icmp_off + 6) != 1U) return 0;
    return 1;
}

int kernel_genet13_selftest(void) {
    enum {
        V4_RDMA_PROD = 0x08U,
        V4_RDMA_CONS = 0x0C,
        RX_Q16_N = 256U
    };
    unsigned long mac;
    unsigned long peer_mac = 0;
    unsigned int cons;
    unsigned int tx_prod = 0;
    uint64_t freq, start, now, ticks;

    if (genet13_probed) return genet13_ok_val;
    genet13_probed = 1;
    genet13_ok_val = 0;
    genet13_arp_val = 0;
    genet13_echo_val = 0;
    genet13_peer_mac_val = 0;

    if (!kernel_genet12_selftest()) return 0;
    mac = kernel_genet3_mac();
    if (!mac) return 0;
    if (!kernel_dma_alloc_nc(&genet13_tx_pa, &genet13_tx_nc)) return 0;
    if (!genet13_tx_nc) return 0;
    if (!genet10_unpark()) return 0;

    cons = rdma_ring16(V4_RDMA_PROD) & 0xFFFFU;
    rdma_ring16_wr(V4_RDMA_CONS, cons);

    genet_write_arp((volatile uint8_t *)genet13_tx_nc, mac);
    if (!genet13_tx_frame(TX_FRAME_LEN, tx_prod)) {
        genet10_park();
        return 0;
    }
    tx_prod++;

    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(start));
    ticks = 3000ULL * freq / 1000ULL;
    do {
        unsigned int prod = rdma_ring16(V4_RDMA_PROD) & 0xFFFFU;
        while (cons != prod) {
            unsigned int idx = cons & (RX_Q16_N - 1U);
            unsigned int bd = RDMA_OFF + idx * DESC_BYTES;
            unsigned int rx_len = (G32(bd) >> 16) & 0x0FFFU;
            unsigned long buf_pa = (unsigned long)G32(bd + 4U);
            void *rx_nc = 0;
            if (rx_len >= 42U && kernel_dma_nc_from_pa(buf_pa, &rx_nc) && rx_nc) {
                if (genet13_arp_reply((const volatile uint8_t *)rx_nc,
                                      rx_len, &peer_mac)) {
                    genet13_arp_val = 1;
                    genet13_peer_mac_val = peer_mac;
                }
            }
            cons = (cons + 1U) & 0xFFFFU;
            rdma_ring16_wr(V4_RDMA_CONS, cons);
            if (genet13_arp_val) break;
        }
        if (genet13_arp_val) break;
        genet_udelay(1000);
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(now));
    } while (now - start < ticks);

    if (genet13_arp_val && peer_mac != 0UL) {
        unsigned int icmp_len = genet13_build_icmp(
            (volatile uint8_t *)genet13_tx_nc, mac, peer_mac);
        if (genet13_tx_frame(icmp_len, tx_prod)) {
            tx_prod++;
            __asm__ volatile("mrs %0, cntpct_el0" : "=r"(start));
            ticks = 3000ULL * freq / 1000ULL;
            do {
                unsigned int prod = rdma_ring16(V4_RDMA_PROD) & 0xFFFFU;
                while (cons != prod) {
                    unsigned int idx = cons & (RX_Q16_N - 1U);
                    unsigned int bd = RDMA_OFF + idx * DESC_BYTES;
                    unsigned int rx_len = (G32(bd) >> 16) & 0x0FFFU;
                    unsigned long buf_pa = (unsigned long)G32(bd + 4U);
                    void *rx_nc = 0;
                    if (rx_len >= 42U &&
                        kernel_dma_nc_from_pa(buf_pa, &rx_nc) && rx_nc) {
                        if (genet13_icmp_reply((const volatile uint8_t *)rx_nc,
                                               rx_len)) {
                            genet13_echo_val = 1;
                        }
                    }
                    cons = (cons + 1U) & 0xFFFFU;
                    rdma_ring16_wr(V4_RDMA_CONS, cons);
                    if (genet13_echo_val) break;
                }
                if (genet13_echo_val) break;
                genet_udelay(1000);
                __asm__ volatile("mrs %0, cntpct_el0" : "=r"(now));
            } while (now - start < ticks);
        }
    }

    genet10_park();
    if (genet13_arp_val == 1U && genet13_echo_val == 1U) {
        genet13_ok_val = 1;
        return 1;
    }
    return 0;
}

int           kernel_genet13_ok(void)       { return genet13_ok_val;       }
unsigned int  kernel_genet13_arp(void)      { return genet13_arp_val;      }
unsigned int  kernel_genet13_echo(void)     { return genet13_echo_val;     }
unsigned long kernel_genet13_peer_mac(void) { return genet13_peer_mac_val; }

// V118: originate UDP echo to 10.42.0.1:41240 (0xA118). Host listener
// echoes the payload. Bounded unpark/poll/park. TX on a DMA NC page —
// not the 4 KiB core0 stack. No EL0. No boot event emit.
int kernel_genet14_selftest(void);

static int genet14_probed;
static int genet14_ok_val;
static unsigned int genet14_udp_val;
static unsigned int genet14_echo_val;
static unsigned long genet14_tx_pa;
static void *genet14_tx_nc;

static int genet14_tx_frame(unsigned int tx_len, unsigned int tx_prod) {
    enum {
        V4_TDMA_CONS = 0x08U,
        V4_TDMA_PROD = 0x0C,
        TX_Q16_START = 128U,
        TX_Q16_N = 128U
    };
    uint64_t freq, t0, tnow, twait;
    unsigned int tcons;
    unsigned int tx_bd;
    uint32_t len_stat;

    if (tx_len < 60U || !genet14_tx_nc) return 0;
    __asm__ volatile("dsb sy" ::: "memory");
    tx_bd = TDMA_OFF + (TX_Q16_START + (tx_prod % TX_Q16_N)) * DESC_BYTES;
    len_stat = ((uint32_t)tx_len << 16) |
               (DMA_QTAG_MASK << DMA_TX_QTAG_SHIFT) |
               DMA_TX_APPEND_CRC | DMA_SOP | DMA_EOP;
    G32(tx_bd + 0U) = len_stat;
    G32(tx_bd + 4U) = (uint32_t)genet14_tx_pa;
    G32(tx_bd + 8U) = 0;
    __asm__ volatile("dsb sy" ::: "memory");
    tdma_ring16_wr(V4_TDMA_PROD, tx_prod + 1U);

    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(t0));
    twait = 20ULL * freq / 1000ULL;
    do {
        tcons = tdma_ring16(V4_TDMA_CONS) & 0xFFFFU;
        if (tcons == (tx_prod + 1U)) return 1;
        genet_udelay(100);
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(tnow));
    } while (tnow - t0 < twait);
    tcons = tdma_ring16(V4_TDMA_CONS) & 0xFFFFU;
    return (tcons != 0U) ? 1 : 0;
}

static unsigned int genet14_build_udp(volatile uint8_t *tx,
                                      unsigned long mac,
                                      unsigned long peer_mac) {
    enum {
        OUR_IP = 0x0a2a0002U,
        HOST_IP = 0x0a2a0001U,
        UDP_PORT = 0xA118U
    };
    unsigned int i;
    uint32_t s;
    uint16_t ucsum;
    for (i = 0; i < 6U; i++) {
        tx[i] = (uint8_t)((peer_mac >> (40U - 8U * i)) & 0xFFUL);
        tx[6U + i] = (uint8_t)((mac >> (40U - 8U * i)) & 0xFFUL);
    }
    genet9_put16(tx + 12, 0x0800U);
    tx[14] = 0x45;
    tx[15] = 0;
    genet9_put16(tx + 16, 36);
    genet9_put16(tx + 18, UDP_PORT);
    genet9_put16(tx + 20, 0);
    tx[22] = 64;
    tx[23] = 0x11U;
    tx[24] = 0;
    tx[25] = 0;
    genet9_put32(tx + 26, OUR_IP);
    genet9_put32(tx + 30, HOST_IP);
    genet9_put16(tx + 24, genet9_csum(tx + 14, 20U));
    genet9_put16(tx + 34, UDP_PORT);
    genet9_put16(tx + 36, UDP_PORT);
    genet9_put16(tx + 38, 16);
    tx[40] = 0;
    tx[41] = 0;
    genet9_put32(tx + 42, 0xA1180001U);
    genet9_put32(tx + 46, 0xA1180002U);
    s = 0;
    s += (OUR_IP >> 16) & 0xFFFFU;
    s += OUR_IP & 0xFFFFU;
    s += (HOST_IP >> 16) & 0xFFFFU;
    s += HOST_IP & 0xFFFFU;
    s += 0x11U;
    s += 16U;
    for (i = 0; i + 1U < 16U; i += 2U) s += genet9_be16(tx + 34U + i);
    while (s >> 16) s = (s & 0xFFFFU) + (s >> 16);
    ucsum = (uint16_t)~s;
    if (ucsum == 0U) ucsum = 0xFFFFU;
    genet9_put16(tx + 40, ucsum);
    for (i = 50U; i < 60U; i++) tx[i] = 0;
    return 60U;
}

static int genet14_udp_echo(const volatile uint8_t *rx, unsigned int rx_len) {
    enum {
        OUR_IP = 0x0a2a0002U,
        HOST_IP = 0x0a2a0001U,
        UDP_PORT = 0xA118U
    };
    unsigned int ihl;
    unsigned int udp_off;
    if (rx_len < 50U) return 0;
    if (genet9_be16(rx + 12) != 0x0800U) return 0;
    if ((rx[14] >> 4) != 4U) return 0;
    ihl = (unsigned int)(rx[14] & 0x0FU) * 4U;
    if (ihl < 20U) return 0;
    if (rx[14 + 9] != 0x11U) return 0;
    if (genet9_be32(rx + 26) != HOST_IP) return 0;
    if (genet9_be32(rx + 30) != OUR_IP) return 0;
    udp_off = 14U + ihl;
    if (rx_len < udp_off + 16U) return 0;
    if (genet9_be16(rx + udp_off) != UDP_PORT) return 0;
    if (genet9_be16(rx + udp_off + 2) != UDP_PORT) return 0;
    if (genet9_be32(rx + udp_off + 8) != 0xA1180001U) return 0;
    if (genet9_be32(rx + udp_off + 12) != 0xA1180002U) return 0;
    return 1;
}

int kernel_genet14_selftest(void) {
    enum {
        V4_RDMA_PROD = 0x08U,
        V4_RDMA_CONS = 0x0C,
        RX_Q16_N = 256U
    };
    unsigned long mac;
    unsigned long peer_mac;
    unsigned int cons;
    unsigned int tx_prod = 0;
    uint64_t freq, start, now, ticks;

    if (genet14_probed) return genet14_ok_val;
    genet14_probed = 1;
    genet14_ok_val = 0;
    genet14_udp_val = 0;
    genet14_echo_val = 0;

    if (!kernel_genet13_selftest()) return 0;
    mac = kernel_genet3_mac();
    peer_mac = kernel_genet13_peer_mac();
    if (!mac || !peer_mac) return 0;
    if (!kernel_dma_alloc_nc(&genet14_tx_pa, &genet14_tx_nc)) return 0;
    if (!genet14_tx_nc) return 0;
    if (!genet10_unpark()) return 0;

    cons = rdma_ring16(V4_RDMA_PROD) & 0xFFFFU;
    rdma_ring16_wr(V4_RDMA_CONS, cons);

    genet14_build_udp((volatile uint8_t *)genet14_tx_nc, mac, peer_mac);
    if (!genet14_tx_frame(60U, tx_prod)) {
        genet10_park();
        return 0;
    }
    genet14_udp_val = 1;
    tx_prod++;

    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(start));
    ticks = 3000ULL * freq / 1000ULL;
    do {
        unsigned int prod = rdma_ring16(V4_RDMA_PROD) & 0xFFFFU;
        while (cons != prod) {
            unsigned int idx = cons & (RX_Q16_N - 1U);
            unsigned int bd = RDMA_OFF + idx * DESC_BYTES;
            unsigned int rx_len = (G32(bd) >> 16) & 0x0FFFU;
            unsigned long buf_pa = (unsigned long)G32(bd + 4U);
            void *rx_nc = 0;
            if (rx_len >= 50U && kernel_dma_nc_from_pa(buf_pa, &rx_nc) && rx_nc) {
                if (genet14_udp_echo((const volatile uint8_t *)rx_nc, rx_len)) {
                    genet14_echo_val = 1;
                }
            }
            cons = (cons + 1U) & 0xFFFFU;
            rdma_ring16_wr(V4_RDMA_CONS, cons);
            if (genet14_echo_val) break;
        }
        if (genet14_echo_val) break;
        genet_udelay(1000);
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(now));
    } while (now - start < ticks);

    genet10_park();
    if (genet14_udp_val == 1U && genet14_echo_val == 1U) {
        genet14_ok_val = 1;
        return 1;
    }
    return 0;
}

int          kernel_genet14_ok(void)   { return genet14_ok_val;   }
unsigned int kernel_genet14_udp(void)  { return genet14_udp_val;  }
unsigned int kernel_genet14_echo(void) { return genet14_echo_val; }

// V119: originate TCP echo to 10.42.0.1:41241 (0xA119). Host listener
// completes the handshake and echoes the payload. Bounded
// unpark/poll/park. TX on a DMA NC page — not the 4 KiB core0 stack.
// No EL0. No boot event emit.
int kernel_genet15_selftest(void);

static int genet15_probed;
static int genet15_ok_val;
static unsigned int genet15_tcp_val;
static unsigned int genet15_echo_val;
static unsigned long genet15_tx_pa;
static void *genet15_tx_nc;

static int genet15_tx_frame(unsigned int tx_len, unsigned int tx_prod) {
    enum {
        V4_TDMA_CONS = 0x08U,
        V4_TDMA_PROD = 0x0C,
        TX_Q16_START = 128U,
        TX_Q16_N = 128U
    };
    uint64_t freq, t0, tnow, twait;
    unsigned int tcons;
    unsigned int tx_bd;
    uint32_t len_stat;

    if (tx_len < 60U || !genet15_tx_nc) return 0;
    __asm__ volatile("dsb sy" ::: "memory");
    tx_bd = TDMA_OFF + (TX_Q16_START + (tx_prod % TX_Q16_N)) * DESC_BYTES;
    len_stat = ((uint32_t)tx_len << 16) |
               (DMA_QTAG_MASK << DMA_TX_QTAG_SHIFT) |
               DMA_TX_APPEND_CRC | DMA_SOP | DMA_EOP;
    G32(tx_bd + 0U) = len_stat;
    G32(tx_bd + 4U) = (uint32_t)genet15_tx_pa;
    G32(tx_bd + 8U) = 0;
    __asm__ volatile("dsb sy" ::: "memory");
    tdma_ring16_wr(V4_TDMA_PROD, tx_prod + 1U);

    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(t0));
    twait = 20ULL * freq / 1000ULL;
    do {
        tcons = tdma_ring16(V4_TDMA_CONS) & 0xFFFFU;
        if (tcons == (tx_prod + 1U)) return 1;
        genet_udelay(100);
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(tnow));
    } while (tnow - t0 < twait);
    tcons = tdma_ring16(V4_TDMA_CONS) & 0xFFFFU;
    return (tcons != 0U) ? 1 : 0;
}

static unsigned int genet15_build_tcp(volatile uint8_t *tx,
                                      unsigned long mac,
                                      unsigned long peer_mac,
                                      uint32_t seq,
                                      uint32_t ack,
                                      uint8_t flags,
                                      unsigned int plen) {
    enum {
        OUR_IP = 0x0a2a0002U,
        HOST_IP = 0x0a2a0001U,
        TCP_PORT = 0xA119U
    };
    unsigned int i;
    unsigned int tcp_len = 20U + plen;
    unsigned int wire = 34U + tcp_len;
    uint32_t s;
    uint16_t csum;

    for (i = 0; i < 6U; i++) {
        tx[i] = (uint8_t)((peer_mac >> (40U - 8U * i)) & 0xFFUL);
        tx[6U + i] = (uint8_t)((mac >> (40U - 8U * i)) & 0xFFUL);
    }
    genet9_put16(tx + 12, 0x0800U);
    tx[14] = 0x45;
    tx[15] = 0;
    genet9_put16(tx + 16, (uint16_t)(20U + tcp_len));
    genet9_put16(tx + 18, TCP_PORT);
    genet9_put16(tx + 20, 0);
    tx[22] = 64;
    tx[23] = 6U;
    tx[24] = 0;
    tx[25] = 0;
    genet9_put32(tx + 26, OUR_IP);
    genet9_put32(tx + 30, HOST_IP);
    genet9_put16(tx + 24, genet9_csum(tx + 14, 20U));
    genet9_put16(tx + 34, TCP_PORT);
    genet9_put16(tx + 36, TCP_PORT);
    genet9_put32(tx + 38, seq);
    genet9_put32(tx + 42, ack);
    tx[46] = 0x50;
    tx[47] = flags;
    genet9_put16(tx + 48, 1024);
    tx[50] = 0;
    tx[51] = 0;
    tx[52] = 0;
    tx[53] = 0;
    if (plen >= 8U) {
        genet9_put32(tx + 54, 0xA1190001U);
        genet9_put32(tx + 58, 0xA1190002U);
    }
    s = 0;
    s += (OUR_IP >> 16) & 0xFFFFU;
    s += OUR_IP & 0xFFFFU;
    s += (HOST_IP >> 16) & 0xFFFFU;
    s += HOST_IP & 0xFFFFU;
    s += 6U;
    s += tcp_len;
    for (i = 0; i + 1U < tcp_len; i += 2U) s += genet9_be16(tx + 34U + i);
    if (tcp_len & 1U) s += (uint32_t)tx[34U + tcp_len - 1U] << 8;
    while (s >> 16) s = (s & 0xFFFFU) + (s >> 16);
    csum = (uint16_t)~s;
    if (csum == 0U) csum = 0xFFFFU;
    genet9_put16(tx + 50, csum);
    for (i = wire; i < 60U; i++) tx[i] = 0;
    return wire < 60U ? 60U : wire;
}

static int genet15_synack(const volatile uint8_t *rx,
                          unsigned int rx_len,
                          uint32_t *peer_seq) {
    enum {
        OUR_IP = 0x0a2a0002U,
        HOST_IP = 0x0a2a0001U,
        TCP_PORT = 0xA119U,
        ISN = 0xA1190000U
    };
    unsigned int ihl;
    unsigned int ip_len;
    unsigned int tcp_off;
    unsigned int doff;
    uint8_t flags;
    if (rx_len < 54U) return 0;
    if (genet9_be16(rx + 12) != 0x0800U) return 0;
    if ((rx[14] >> 4) != 4U) return 0;
    ihl = (unsigned int)(rx[14] & 0x0FU) * 4U;
    if (ihl < 20U) return 0;
    if (rx[14 + 9] != 6U) return 0;
    if (genet9_be32(rx + 26) != HOST_IP) return 0;
    if (genet9_be32(rx + 30) != OUR_IP) return 0;
    ip_len = genet9_be16(rx + 16);
    if (ip_len < ihl + 20U) return 0;
    tcp_off = 14U + ihl;
    if (rx_len < tcp_off + 20U) return 0;
    if (genet9_be16(rx + tcp_off) != TCP_PORT) return 0;
    if (genet9_be16(rx + tcp_off + 2) != TCP_PORT) return 0;
    doff = ((unsigned int)(rx[tcp_off + 12] >> 4) & 0x0FU) * 4U;
    if (doff < 20U || tcp_off + doff > rx_len) return 0;
    flags = rx[tcp_off + 13];
    if ((flags & 0x12U) != 0x12U) return 0;
    if ((flags & 0x04U) != 0U) return 0;
    if (genet9_be32(rx + tcp_off + 8) != (ISN + 1U)) return 0;
    if (peer_seq) *peer_seq = genet9_be32(rx + tcp_off + 4);
    return 1;
}

static int genet15_tcp_echo(const volatile uint8_t *rx, unsigned int rx_len) {
    enum {
        OUR_IP = 0x0a2a0002U,
        HOST_IP = 0x0a2a0001U,
        TCP_PORT = 0xA119U
    };
    unsigned int ihl;
    unsigned int ip_len;
    unsigned int tcp_off;
    unsigned int doff;
    unsigned int plen;
    if (rx_len < 54U) return 0;
    if (genet9_be16(rx + 12) != 0x0800U) return 0;
    if ((rx[14] >> 4) != 4U) return 0;
    ihl = (unsigned int)(rx[14] & 0x0FU) * 4U;
    if (ihl < 20U) return 0;
    if (rx[14 + 9] != 6U) return 0;
    if (genet9_be32(rx + 26) != HOST_IP) return 0;
    if (genet9_be32(rx + 30) != OUR_IP) return 0;
    ip_len = genet9_be16(rx + 16);
    if (ip_len < ihl + 28U) return 0;
    tcp_off = 14U + ihl;
    if (rx_len < tcp_off + 28U) return 0;
    if (genet9_be16(rx + tcp_off) != TCP_PORT) return 0;
    if (genet9_be16(rx + tcp_off + 2) != TCP_PORT) return 0;
    doff = ((unsigned int)(rx[tcp_off + 12] >> 4) & 0x0FU) * 4U;
    if (doff < 20U || tcp_off + doff + 8U > rx_len) return 0;
    if (ip_len < ihl + doff + 8U) return 0;
    plen = ip_len - ihl - doff;
    if (plen < 8U) return 0;
    if (genet9_be32(rx + tcp_off + doff) != 0xA1190001U) return 0;
    if (genet9_be32(rx + tcp_off + doff + 4) != 0xA1190002U) return 0;
    return 1;
}

int kernel_genet15_selftest(void) {
    enum {
        V4_RDMA_PROD = 0x08U,
        V4_RDMA_CONS = 0x0C,
        RX_Q16_N = 256U,
        ISN = 0xA1190000U
    };
    unsigned long mac;
    unsigned long peer_mac;
    unsigned int cons;
    unsigned int tx_prod = 0;
    unsigned int syn_len;
    unsigned int data_len;
    uint32_t peer_seq = 0;
    uint64_t freq, start, now, ticks;

    if (genet15_probed) return genet15_ok_val;
    genet15_probed = 1;
    genet15_ok_val = 0;
    genet15_tcp_val = 0;
    genet15_echo_val = 0;

    if (!kernel_genet14_selftest()) return 0;
    mac = kernel_genet3_mac();
    peer_mac = kernel_genet13_peer_mac();
    if (!mac || !peer_mac) return 0;
    if (!kernel_dma_alloc_nc(&genet15_tx_pa, &genet15_tx_nc)) return 0;
    if (!genet15_tx_nc) return 0;
    if (!genet10_unpark()) return 0;

    cons = rdma_ring16(V4_RDMA_PROD) & 0xFFFFU;
    rdma_ring16_wr(V4_RDMA_CONS, cons);

    syn_len = genet15_build_tcp((volatile uint8_t *)genet15_tx_nc,
                                mac, peer_mac, ISN, 0, 0x02U, 0);
    if (!genet15_tx_frame(syn_len, tx_prod)) {
        genet10_park();
        return 0;
    }
    tx_prod++;

    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(start));
    ticks = 3000ULL * freq / 1000ULL;
    do {
        unsigned int prod = rdma_ring16(V4_RDMA_PROD) & 0xFFFFU;
        while (cons != prod) {
            unsigned int idx = cons & (RX_Q16_N - 1U);
            unsigned int bd = RDMA_OFF + idx * DESC_BYTES;
            unsigned int rx_len = (G32(bd) >> 16) & 0x0FFFU;
            unsigned long buf_pa = (unsigned long)G32(bd + 4U);
            void *rx_nc = 0;
            if (rx_len >= 54U && kernel_dma_nc_from_pa(buf_pa, &rx_nc) && rx_nc) {
                if (genet15_synack((const volatile uint8_t *)rx_nc, rx_len,
                                   &peer_seq)) {
                    genet15_tcp_val = 1;
                }
            }
            cons = (cons + 1U) & 0xFFFFU;
            rdma_ring16_wr(V4_RDMA_CONS, cons);
            if (genet15_tcp_val) break;
        }
        if (genet15_tcp_val) break;
        genet_udelay(1000);
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(now));
    } while (now - start < ticks);

    if (genet15_tcp_val) {
        data_len = genet15_build_tcp((volatile uint8_t *)genet15_tx_nc,
                                     mac, peer_mac, ISN + 1U, peer_seq + 1U,
                                     0x18U, 8U);
        if (genet15_tx_frame(data_len, tx_prod)) {
            tx_prod++;
            __asm__ volatile("mrs %0, cntpct_el0" : "=r"(start));
            ticks = 3000ULL * freq / 1000ULL;
            do {
                unsigned int prod = rdma_ring16(V4_RDMA_PROD) & 0xFFFFU;
                while (cons != prod) {
                    unsigned int idx = cons & (RX_Q16_N - 1U);
                    unsigned int bd = RDMA_OFF + idx * DESC_BYTES;
                    unsigned int rx_len = (G32(bd) >> 16) & 0x0FFFU;
                    unsigned long buf_pa = (unsigned long)G32(bd + 4U);
                    void *rx_nc = 0;
                    if (rx_len >= 54U &&
                        kernel_dma_nc_from_pa(buf_pa, &rx_nc) && rx_nc) {
                        if (genet15_tcp_echo((const volatile uint8_t *)rx_nc,
                                             rx_len)) {
                            genet15_echo_val = 1;
                        }
                    }
                    cons = (cons + 1U) & 0xFFFFU;
                    rdma_ring16_wr(V4_RDMA_CONS, cons);
                    if (genet15_echo_val) break;
                }
                if (genet15_echo_val) break;
                genet_udelay(1000);
                __asm__ volatile("mrs %0, cntpct_el0" : "=r"(now));
            } while (now - start < ticks);
        }
    }

    genet10_park();
    if (genet15_tcp_val == 1U && genet15_echo_val == 1U) {
        genet15_ok_val = 1;
        return 1;
    }
    return 0;
}

int          kernel_genet15_ok(void)   { return genet15_ok_val;   }
unsigned int kernel_genet15_tcp(void)  { return genet15_tcp_val;  }
unsigned int kernel_genet15_echo(void) { return genet15_echo_val; }

// V120: originate TFTP RRQ to 10.42.0.1:69 for aether/v120.bin.
// Host TFTP (already serving netboot) returns DATA block 1. Fail-closed
// payload match. Bounded unpark/poll/park. TX on a DMA NC page —
// not the 4 KiB core0 stack. No EL0. No boot event emit. New file only.
int kernel_genet16_selftest(void);

static int genet16_probed;
static int genet16_ok_val;
static unsigned int genet16_tftp_val;
static unsigned int genet16_match_val;
static unsigned long genet16_tx_pa;
static void *genet16_tx_nc;

static int genet16_tx_frame(unsigned int tx_len, unsigned int tx_prod) {
    enum {
        V4_TDMA_CONS = 0x08U,
        V4_TDMA_PROD = 0x0C,
        TX_Q16_START = 128U,
        TX_Q16_N = 128U
    };
    uint64_t freq, t0, tnow, twait;
    unsigned int tcons;
    unsigned int tx_bd;
    uint32_t len_stat;

    if (tx_len < 60U || !genet16_tx_nc) return 0;
    __asm__ volatile("dsb sy" ::: "memory");
    tx_bd = TDMA_OFF + (TX_Q16_START + (tx_prod % TX_Q16_N)) * DESC_BYTES;
    len_stat = ((uint32_t)tx_len << 16) |
               (DMA_QTAG_MASK << DMA_TX_QTAG_SHIFT) |
               DMA_TX_APPEND_CRC | DMA_SOP | DMA_EOP;
    G32(tx_bd + 0U) = len_stat;
    G32(tx_bd + 4U) = (uint32_t)genet16_tx_pa;
    G32(tx_bd + 8U) = 0;
    __asm__ volatile("dsb sy" ::: "memory");
    tdma_ring16_wr(V4_TDMA_PROD, tx_prod + 1U);

    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(t0));
    twait = 20ULL * freq / 1000ULL;
    do {
        tcons = tdma_ring16(V4_TDMA_CONS) & 0xFFFFU;
        if (tcons == (tx_prod + 1U)) return 1;
        genet_udelay(100);
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(tnow));
    } while (tnow - t0 < twait);
    tcons = tdma_ring16(V4_TDMA_CONS) & 0xFFFFU;
    return (tcons != 0U) ? 1 : 0;
}

static unsigned int genet16_put_cstr(volatile uint8_t *p, const char *s) {
    unsigned int n = 0;
    while (s[n]) {
        p[n] = (uint8_t)s[n];
        n++;
    }
    p[n] = 0;
    return n + 1U;
}

static unsigned int genet16_build_udp(volatile uint8_t *tx,
                                      unsigned long mac,
                                      unsigned long peer_mac,
                                      unsigned int tftp_len) {
    enum {
        OUR_IP = 0x0a2a0002U,
        HOST_IP = 0x0a2a0001U,
        UDP_SPORT = 0xA120U,
        TFTP_PORT = 69U
    };
    unsigned int i;
    unsigned int udp_len = 8U + tftp_len;
    unsigned int ip_len = 20U + udp_len;
    unsigned int wire = 14U + ip_len;
    uint32_t s;
    uint16_t ucsum;

    for (i = 0; i < 6U; i++) {
        tx[i] = (uint8_t)((peer_mac >> (40U - 8U * i)) & 0xFFUL);
        tx[6U + i] = (uint8_t)((mac >> (40U - 8U * i)) & 0xFFUL);
    }
    genet9_put16(tx + 12, 0x0800U);
    tx[14] = 0x45;
    tx[15] = 0;
    genet9_put16(tx + 16, (uint16_t)ip_len);
    genet9_put16(tx + 18, UDP_SPORT);
    genet9_put16(tx + 20, 0);
    tx[22] = 64;
    tx[23] = 0x11U;
    tx[24] = 0;
    tx[25] = 0;
    genet9_put32(tx + 26, OUR_IP);
    genet9_put32(tx + 30, HOST_IP);
    genet9_put16(tx + 24, genet9_csum(tx + 14, 20U));
    genet9_put16(tx + 34, UDP_SPORT);
    genet9_put16(tx + 36, (uint16_t)TFTP_PORT);
    genet9_put16(tx + 38, (uint16_t)udp_len);
    tx[40] = 0;
    tx[41] = 0;
    s = 0;
    s += (OUR_IP >> 16) & 0xFFFFU;
    s += OUR_IP & 0xFFFFU;
    s += (HOST_IP >> 16) & 0xFFFFU;
    s += HOST_IP & 0xFFFFU;
    s += 0x11U;
    s += udp_len;
    for (i = 0; i + 1U < udp_len; i += 2U) s += genet9_be16(tx + 34U + i);
    if (udp_len & 1U) s += (uint32_t)tx[34U + udp_len - 1U] << 8;
    while (s >> 16) s = (s & 0xFFFFU) + (s >> 16);
    ucsum = (uint16_t)~s;
    if (ucsum == 0U) ucsum = 0xFFFFU;
    genet9_put16(tx + 40, ucsum);
    for (i = wire; i < 60U; i++) tx[i] = 0;
    return wire < 60U ? 60U : wire;
}

static unsigned int genet16_build_rrq(volatile uint8_t *tx,
                                      unsigned long mac,
                                      unsigned long peer_mac) {
    unsigned int off = 42U;
    genet9_put16(tx + off, 1);
    off += 2U;
    off += genet16_put_cstr(tx + off, "aether/v120.bin");
    off += genet16_put_cstr(tx + off, "octet");
    return genet16_build_udp(tx, mac, peer_mac, off - 42U);
}

static unsigned int genet16_build_ack(volatile uint8_t *tx,
                                      unsigned long mac,
                                      unsigned long peer_mac) {
    genet9_put16(tx + 42, 4);
    genet9_put16(tx + 44, 1);
    return genet16_build_udp(tx, mac, peer_mac, 4U);
}

static int genet16_tftp_data(const volatile uint8_t *rx, unsigned int rx_len) {
    enum {
        OUR_IP = 0x0a2a0002U,
        HOST_IP = 0x0a2a0001U,
        UDP_SPORT = 0xA120U,
        TFTP_PORT = 69U
    };
    unsigned int ihl;
    unsigned int udp_off;
    unsigned int tftp_off;
    if (rx_len < 62U) return 0;
    if (genet9_be16(rx + 12) != 0x0800U) return 0;
    if ((rx[14] >> 4) != 4U) return 0;
    ihl = (unsigned int)(rx[14] & 0x0FU) * 4U;
    if (ihl < 20U) return 0;
    if (rx[14 + 9] != 0x11U) return 0;
    if (genet9_be32(rx + 26) != HOST_IP) return 0;
    if (genet9_be32(rx + 30) != OUR_IP) return 0;
    udp_off = 14U + ihl;
    if (rx_len < udp_off + 28U) return 0;
    if (genet9_be16(rx + udp_off) != (uint16_t)TFTP_PORT) return 0;
    if (genet9_be16(rx + udp_off + 2) != UDP_SPORT) return 0;
    tftp_off = udp_off + 8U;
    if (genet9_be16(rx + tftp_off) != 3U) return 0;
    if (genet9_be16(rx + tftp_off + 2) != 1U) return 0;
    if (genet9_be32(rx + tftp_off + 4) != 0xA1200001U) return 0;
    if (genet9_be32(rx + tftp_off + 8) != 0xA1200002U) return 0;
    if (genet9_be32(rx + tftp_off + 12) != 0xA1200003U) return 0;
    if (genet9_be32(rx + tftp_off + 16) != 0xA1200004U) return 0;
    return 1;
}

int kernel_genet16_selftest(void) {
    enum {
        V4_RDMA_PROD = 0x08U,
        V4_RDMA_CONS = 0x0C,
        RX_Q16_N = 256U
    };
    unsigned long mac;
    unsigned long peer_mac;
    unsigned int cons;
    unsigned int tx_prod = 0;
    unsigned int rrq_len;
    unsigned int ack_len;
    uint64_t freq, start, now, ticks;

    if (genet16_probed) return genet16_ok_val;
    genet16_probed = 1;
    genet16_ok_val = 0;
    genet16_tftp_val = 0;
    genet16_match_val = 0;

    if (!kernel_genet13_selftest()) return 0;
    mac = kernel_genet3_mac();
    peer_mac = kernel_genet13_peer_mac();
    if (!mac || !peer_mac) return 0;
    if (!kernel_dma_alloc_nc(&genet16_tx_pa, &genet16_tx_nc)) return 0;
    if (!genet16_tx_nc) return 0;
    if (!genet10_unpark()) return 0;

    cons = rdma_ring16(V4_RDMA_PROD) & 0xFFFFU;
    rdma_ring16_wr(V4_RDMA_CONS, cons);

    rrq_len = genet16_build_rrq((volatile uint8_t *)genet16_tx_nc, mac, peer_mac);
    if (!genet16_tx_frame(rrq_len, tx_prod)) {
        genet10_park();
        return 0;
    }
    genet16_tftp_val = 1;
    tx_prod++;

    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(start));
    ticks = 3000ULL * freq / 1000ULL;
    do {
        unsigned int prod = rdma_ring16(V4_RDMA_PROD) & 0xFFFFU;
        while (cons != prod) {
            unsigned int idx = cons & (RX_Q16_N - 1U);
            unsigned int bd = RDMA_OFF + idx * DESC_BYTES;
            unsigned int rx_len = (G32(bd) >> 16) & 0x0FFFU;
            unsigned long buf_pa = (unsigned long)G32(bd + 4U);
            void *rx_nc = 0;
            if (rx_len >= 62U && kernel_dma_nc_from_pa(buf_pa, &rx_nc) && rx_nc) {
                if (genet16_tftp_data((const volatile uint8_t *)rx_nc, rx_len)) {
                    genet16_match_val = 1;
                }
            }
            cons = (cons + 1U) & 0xFFFFU;
            rdma_ring16_wr(V4_RDMA_CONS, cons);
            if (genet16_match_val) break;
        }
        if (genet16_match_val) break;
        genet_udelay(1000);
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(now));
    } while (now - start < ticks);

    if (genet16_match_val) {
        ack_len = genet16_build_ack((volatile uint8_t *)genet16_tx_nc, mac, peer_mac);
        (void)genet16_tx_frame(ack_len, tx_prod);
    }

    genet10_park();
    if (genet16_tftp_val == 1U && genet16_match_val == 1U) {
        genet16_ok_val = 1;
        return 1;
    }
    return 0;
}

int          kernel_genet16_ok(void)    { return genet16_ok_val;    }
unsigned int kernel_genet16_tftp(void)  { return genet16_tftp_val;  }
unsigned int kernel_genet16_match(void) { return genet16_match_val; }

// V121: originate mDNS A query for aether-v121.local to 0xe00000fb:5353.
// Host helper answers with A 10.42.0.1. Bounded unpark/poll/park.
// TX on a DMA NC page — not the 4 KiB core0 stack. No EL0. No boot event emit.
int kernel_genet17_selftest(void);

static int genet17_probed;
static int genet17_ok_val;
static unsigned int genet17_mdns_val;
static unsigned int genet17_ans_val;
static unsigned long genet17_tx_pa;
static void *genet17_tx_nc;

static int genet17_tx_frame(unsigned int tx_len, unsigned int tx_prod) {
    enum {
        V4_TDMA_CONS = 0x08U,
        V4_TDMA_PROD = 0x0C,
        TX_Q16_START = 128U,
        TX_Q16_N = 128U
    };
    uint64_t freq, t0, tnow, twait;
    unsigned int tcons;
    unsigned int tx_bd;
    uint32_t len_stat;

    if (tx_len < 60U || !genet17_tx_nc) return 0;
    __asm__ volatile("dsb sy" ::: "memory");
    tx_bd = TDMA_OFF + (TX_Q16_START + (tx_prod % TX_Q16_N)) * DESC_BYTES;
    len_stat = ((uint32_t)tx_len << 16) |
               (DMA_QTAG_MASK << DMA_TX_QTAG_SHIFT) |
               DMA_TX_APPEND_CRC | DMA_SOP | DMA_EOP;
    G32(tx_bd + 0U) = len_stat;
    G32(tx_bd + 4U) = (uint32_t)genet17_tx_pa;
    G32(tx_bd + 8U) = 0;
    __asm__ volatile("dsb sy" ::: "memory");
    tdma_ring16_wr(V4_TDMA_PROD, tx_prod + 1U);

    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(t0));
    twait = 20ULL * freq / 1000ULL;
    do {
        tcons = tdma_ring16(V4_TDMA_CONS) & 0xFFFFU;
        if (tcons == (tx_prod + 1U)) return 1;
        genet_udelay(100);
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(tnow));
    } while (tnow - t0 < twait);
    tcons = tdma_ring16(V4_TDMA_CONS) & 0xFFFFU;
    return (tcons != 0U) ? 1 : 0;
}

static unsigned int genet17_put_label(volatile uint8_t *p, const char *s) {
    unsigned int n = 0;
    while (s[n]) n++;
    p[0] = (uint8_t)n;
    for (unsigned int i = 0; i < n; i++) p[1U + i] = (uint8_t)s[i];
    return n + 1U;
}

static unsigned int genet17_skip_name(const volatile uint8_t *p,
                                      unsigned int off,
                                      unsigned int lim) {
    unsigned int hops = 0;
    while (off < lim && hops < 16U) {
        uint8_t lab = p[off];
        if (lab == 0U) return off + 1U;
        if ((lab & 0xC0U) == 0xC0U) {
            if (off + 2U > lim) return 0;
            return off + 2U;
        }
        if (off + 1U + (unsigned int)lab > lim) return 0;
        off += 1U + (unsigned int)lab;
        hops++;
    }
    return 0;
}

static unsigned int genet17_build_query(volatile uint8_t *tx, unsigned long mac) {
    enum {
        OUR_IP = 0x0a2a0002U,
        MCAST_IP = 0xe00000fbU,
        UDP_SPORT = 0xA121U,
        MDNS_PORT = 5353U
    };
    unsigned int i;
    unsigned int dns_off = 42U;
    unsigned int off;
    unsigned int dns_len;
    unsigned int udp_len;
    unsigned int ip_len;
    unsigned int wire;

    tx[0] = 0x01;
    tx[1] = 0x00;
    tx[2] = 0x5e;
    tx[3] = 0x00;
    tx[4] = 0x00;
    tx[5] = 0xfb;
    for (i = 0; i < 6U; i++) {
        tx[6U + i] = (uint8_t)((mac >> (40U - 8U * i)) & 0xFFUL);
    }
    genet9_put16(tx + 12, 0x0800U);
    tx[14] = 0x45;
    tx[15] = 0;
    genet9_put16(tx + 18, UDP_SPORT);
    genet9_put16(tx + 20, 0);
    tx[22] = 255;
    tx[23] = 0x11U;
    tx[24] = 0;
    tx[25] = 0;
    genet9_put32(tx + 26, OUR_IP);
    genet9_put32(tx + 30, MCAST_IP);

    genet9_put16(tx + dns_off, UDP_SPORT);
    genet9_put16(tx + dns_off + 2, 0);
    genet9_put16(tx + dns_off + 4, 1);
    genet9_put16(tx + dns_off + 6, 0);
    genet9_put16(tx + dns_off + 8, 0);
    genet9_put16(tx + dns_off + 10, 0);
    off = dns_off + 12U;
    off += genet17_put_label(tx + off, "aether-v121");
    off += genet17_put_label(tx + off, "local");
    tx[off] = 0;
    off += 1U;
    genet9_put16(tx + off, 1);
    off += 2U;
    genet9_put16(tx + off, 1);
    off += 2U;
    dns_len = off - dns_off;
    udp_len = 8U + dns_len;
    ip_len = 20U + udp_len;
    wire = 14U + ip_len;
    genet9_put16(tx + 16, (uint16_t)ip_len);
    genet9_put16(tx + 24, genet9_csum(tx + 14, 20U));
    genet9_put16(tx + 34, UDP_SPORT);
    genet9_put16(tx + 36, (uint16_t)MDNS_PORT);
    genet9_put16(tx + 38, (uint16_t)udp_len);
    /* IPv4 UDP checksum 0 = none. Host dropped Pi queries with a bad csum. */
    tx[40] = 0;
    tx[41] = 0;
    for (i = wire; i < 60U; i++) tx[i] = 0;
    return wire < 60U ? 60U : wire;
}

static int genet17_mdns_ans(const volatile uint8_t *rx, unsigned int rx_len) {
    enum {
        OUR_IP = 0x0a2a0002U,
        HOST_IP = 0x0a2a0001U,
        MCAST_IP = 0xe00000fbU,
        UDP_SPORT = 0xA121U,
        MDNS_PORT = 5353U
    };
    unsigned int ihl;
    unsigned int udp_off;
    unsigned int dns_off;
    unsigned int off;
    unsigned int lim;
    unsigned int qd;
    unsigned int an;
    unsigned int i;
    uint32_t dst_ip;
    uint16_t dport;
    if (rx_len < 54U) return 0;
    if (genet9_be16(rx + 12) != 0x0800U) return 0;
    if ((rx[14] >> 4) != 4U) return 0;
    ihl = (unsigned int)(rx[14] & 0x0FU) * 4U;
    if (ihl < 20U) return 0;
    if (rx[14 + 9] != 0x11U) return 0;
    if (genet9_be32(rx + 26) != HOST_IP) return 0;
    dst_ip = genet9_be32(rx + 30);
    if (dst_ip != OUR_IP && dst_ip != MCAST_IP) return 0;
    udp_off = 14U + ihl;
    if (rx_len < udp_off + 20U) return 0;
    if (genet9_be16(rx + udp_off) != (uint16_t)MDNS_PORT) return 0;
    dport = genet9_be16(rx + udp_off + 2);
    if (dport != UDP_SPORT && dport != (uint16_t)MDNS_PORT) return 0;
    dns_off = udp_off + 8U;
    if (rx_len < dns_off + 12U) return 0;
    if ((genet9_be16(rx + dns_off + 2) & 0x8000U) == 0U) return 0;
    qd = genet9_be16(rx + dns_off + 4);
    an = genet9_be16(rx + dns_off + 6);
    if (an == 0U) return 0;
    off = dns_off + 12U;
    lim = rx_len;
    for (i = 0; i < qd && i < 8U; i++) {
        off = genet17_skip_name(rx, off, lim);
        if (off == 0U || off + 4U > lim) return 0;
        off += 4U;
    }
    for (i = 0; i < an && i < 8U; i++) {
        unsigned int type;
        unsigned int rdlen;
        off = genet17_skip_name(rx, off, lim);
        if (off == 0U || off + 10U > lim) return 0;
        type = genet9_be16(rx + off);
        rdlen = genet9_be16(rx + off + 8);
        off += 10U;
        if (off + rdlen > lim) return 0;
        if (type == 1U && rdlen == 4U && genet9_be32(rx + off) == HOST_IP) {
            return 1;
        }
        off += rdlen;
    }
    return 0;
}

int kernel_genet17_selftest(void) {
    enum {
        V4_RDMA_PROD = 0x08U,
        V4_RDMA_CONS = 0x0C,
        RX_Q16_N = 256U
    };
    unsigned long mac;
    unsigned int cons;
    unsigned int tx_prod = 0;
    unsigned int qlen;
    uint64_t freq, start, now, ticks;

    if (genet17_probed) return genet17_ok_val;
    genet17_probed = 1;
    genet17_ok_val = 0;
    genet17_mdns_val = 0;
    genet17_ans_val = 0;

    if (!kernel_genet13_selftest()) return 0;
    mac = kernel_genet3_mac();
    if (!mac) return 0;
    if (!kernel_dma_alloc_nc(&genet17_tx_pa, &genet17_tx_nc)) return 0;
    if (!genet17_tx_nc) return 0;
    if (!genet10_unpark()) return 0;

    cons = rdma_ring16(V4_RDMA_PROD) & 0xFFFFU;
    rdma_ring16_wr(V4_RDMA_CONS, cons);

    qlen = genet17_build_query((volatile uint8_t *)genet17_tx_nc, mac);
    if (!genet17_tx_frame(qlen, tx_prod)) {
        genet10_park();
        return 0;
    }
    genet17_mdns_val = 1;
    tx_prod++;

    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(start));
    ticks = 5000ULL * freq / 1000ULL;
    do {
        unsigned int prod = rdma_ring16(V4_RDMA_PROD) & 0xFFFFU;
        while (cons != prod) {
            unsigned int idx = cons & (RX_Q16_N - 1U);
            unsigned int bd = RDMA_OFF + idx * DESC_BYTES;
            unsigned int rx_len = (G32(bd) >> 16) & 0x0FFFU;
            unsigned long buf_pa = (unsigned long)G32(bd + 4U);
            void *rx_nc = 0;
            if (rx_len >= 54U && kernel_dma_nc_from_pa(buf_pa, &rx_nc) && rx_nc) {
                if (genet17_mdns_ans((const volatile uint8_t *)rx_nc, rx_len)) {
                    genet17_ans_val = 1;
                }
            }
            cons = (cons + 1U) & 0xFFFFU;
            rdma_ring16_wr(V4_RDMA_CONS, cons);
            if (genet17_ans_val) break;
        }
        if (genet17_ans_val) break;
        genet_udelay(1000);
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(now));
    } while (now - start < ticks);

    genet10_park();
    if (genet17_mdns_val == 1U && genet17_ans_val == 1U) {
        genet17_ok_val = 1;
        return 1;
    }
    return 0;
}

int          kernel_genet17_ok(void)   { return genet17_ok_val;   }
unsigned int kernel_genet17_mdns(void) { return genet17_mdns_val; }
unsigned int kernel_genet17_ans(void)  { return genet17_ans_val;  }

// V122: originate HTTP/1.0 GET /aether/v122.txt to 10.42.0.1:41250 (0xA122).
// Host helper returns 200 + 16-byte body. Bounded unpark/poll/park.
// TX on a DMA NC page — not the 4 KiB core0 stack. No EL0. No boot event emit.
int kernel_genet18_selftest(void);

static int genet18_probed;
static int genet18_ok_val;
static unsigned int genet18_http_val;
static unsigned int genet18_body_val;
static unsigned long genet18_tx_pa;
static void *genet18_tx_nc;

static int genet18_tx_frame(unsigned int tx_len, unsigned int tx_prod) {
    enum {
        V4_TDMA_CONS = 0x08U,
        V4_TDMA_PROD = 0x0C,
        TX_Q16_START = 128U,
        TX_Q16_N = 128U
    };
    uint64_t freq, t0, tnow, twait;
    unsigned int tcons;
    unsigned int tx_bd;
    uint32_t len_stat;

    if (tx_len < 60U || !genet18_tx_nc) return 0;
    __asm__ volatile("dsb sy" ::: "memory");
    tx_bd = TDMA_OFF + (TX_Q16_START + (tx_prod % TX_Q16_N)) * DESC_BYTES;
    len_stat = ((uint32_t)tx_len << 16) |
               (DMA_QTAG_MASK << DMA_TX_QTAG_SHIFT) |
               DMA_TX_APPEND_CRC | DMA_SOP | DMA_EOP;
    G32(tx_bd + 0U) = len_stat;
    G32(tx_bd + 4U) = (uint32_t)genet18_tx_pa;
    G32(tx_bd + 8U) = 0;
    __asm__ volatile("dsb sy" ::: "memory");
    tdma_ring16_wr(V4_TDMA_PROD, tx_prod + 1U);

    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(t0));
    twait = 20ULL * freq / 1000ULL;
    do {
        tcons = tdma_ring16(V4_TDMA_CONS) & 0xFFFFU;
        if (tcons == (tx_prod + 1U)) return 1;
        genet_udelay(100);
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(tnow));
    } while (tnow - t0 < twait);
    tcons = tdma_ring16(V4_TDMA_CONS) & 0xFFFFU;
    return (tcons != 0U) ? 1 : 0;
}

static unsigned int genet18_http_req_len(void) {
    static const char req[] =
        "GET /aether/v122.txt HTTP/1.0\r\nHost: 10.42.0.1\r\n\r\n";
    unsigned int n = 0;
    while (req[n]) n++;
    return n;
}

static void genet18_put_http_req(volatile uint8_t *p) {
    static const char req[] =
        "GET /aether/v122.txt HTTP/1.0\r\nHost: 10.42.0.1\r\n\r\n";
    unsigned int i = 0;
    while (req[i]) {
        p[i] = (uint8_t)req[i];
        i++;
    }
}

static unsigned int genet18_build_tcp(volatile uint8_t *tx,
                                      unsigned long mac,
                                      unsigned long peer_mac,
                                      uint32_t seq,
                                      uint32_t ack,
                                      uint8_t flags,
                                      unsigned int plen) {
    enum {
        OUR_IP = 0x0a2a0002U,
        HOST_IP = 0x0a2a0001U,
        TCP_PORT = 0xA122U
    };
    unsigned int i;
    unsigned int tcp_len = 20U + plen;
    unsigned int wire = 34U + tcp_len;
    uint32_t s;
    uint16_t csum;

    for (i = 0; i < 6U; i++) {
        tx[i] = (uint8_t)((peer_mac >> (40U - 8U * i)) & 0xFFUL);
        tx[6U + i] = (uint8_t)((mac >> (40U - 8U * i)) & 0xFFUL);
    }
    genet9_put16(tx + 12, 0x0800U);
    tx[14] = 0x45;
    tx[15] = 0;
    genet9_put16(tx + 16, (uint16_t)(20U + tcp_len));
    genet9_put16(tx + 18, TCP_PORT);
    genet9_put16(tx + 20, 0);
    tx[22] = 64;
    tx[23] = 6U;
    tx[24] = 0;
    tx[25] = 0;
    genet9_put32(tx + 26, OUR_IP);
    genet9_put32(tx + 30, HOST_IP);
    genet9_put16(tx + 24, genet9_csum(tx + 14, 20U));
    genet9_put16(tx + 34, TCP_PORT);
    genet9_put16(tx + 36, TCP_PORT);
    genet9_put32(tx + 38, seq);
    genet9_put32(tx + 42, ack);
    tx[46] = 0x50;
    tx[47] = flags;
    genet9_put16(tx + 48, 1024);
    tx[50] = 0;
    tx[51] = 0;
    tx[52] = 0;
    tx[53] = 0;
    if (plen) genet18_put_http_req(tx + 54);
    s = 0;
    s += (OUR_IP >> 16) & 0xFFFFU;
    s += OUR_IP & 0xFFFFU;
    s += (HOST_IP >> 16) & 0xFFFFU;
    s += HOST_IP & 0xFFFFU;
    s += 6U;
    s += tcp_len;
    for (i = 0; i + 1U < tcp_len; i += 2U) s += genet9_be16(tx + 34U + i);
    if (tcp_len & 1U) s += (uint32_t)tx[34U + tcp_len - 1U] << 8;
    while (s >> 16) s = (s & 0xFFFFU) + (s >> 16);
    csum = (uint16_t)~s;
    if (csum == 0U) csum = 0xFFFFU;
    genet9_put16(tx + 50, csum);
    for (i = wire; i < 60U; i++) tx[i] = 0;
    return wire < 60U ? 60U : wire;
}

static int genet18_synack(const volatile uint8_t *rx,
                          unsigned int rx_len,
                          uint32_t *peer_seq) {
    enum {
        OUR_IP = 0x0a2a0002U,
        HOST_IP = 0x0a2a0001U,
        TCP_PORT = 0xA122U,
        ISN = 0xA1220000U
    };
    unsigned int ihl;
    unsigned int ip_len;
    unsigned int tcp_off;
    unsigned int doff;
    uint8_t flags;
    if (rx_len < 54U) return 0;
    if (genet9_be16(rx + 12) != 0x0800U) return 0;
    if ((rx[14] >> 4) != 4U) return 0;
    ihl = (unsigned int)(rx[14] & 0x0FU) * 4U;
    if (ihl < 20U) return 0;
    if (rx[14 + 9] != 6U) return 0;
    if (genet9_be32(rx + 26) != HOST_IP) return 0;
    if (genet9_be32(rx + 30) != OUR_IP) return 0;
    ip_len = genet9_be16(rx + 16);
    if (ip_len < ihl + 20U) return 0;
    tcp_off = 14U + ihl;
    if (rx_len < tcp_off + 20U) return 0;
    if (genet9_be16(rx + tcp_off) != TCP_PORT) return 0;
    if (genet9_be16(rx + tcp_off + 2) != TCP_PORT) return 0;
    doff = ((unsigned int)(rx[tcp_off + 12] >> 4) & 0x0FU) * 4U;
    if (doff < 20U || tcp_off + doff > rx_len) return 0;
    flags = rx[tcp_off + 13];
    if ((flags & 0x12U) != 0x12U) return 0;
    if ((flags & 0x04U) != 0U) return 0;
    if (genet9_be32(rx + tcp_off + 8) != (ISN + 1U)) return 0;
    if (peer_seq) *peer_seq = genet9_be32(rx + tcp_off + 4);
    return 1;
}

static int genet18_http_body(const volatile uint8_t *rx, unsigned int rx_len) {
    enum {
        OUR_IP = 0x0a2a0002U,
        HOST_IP = 0x0a2a0001U,
        TCP_PORT = 0xA122U
    };
    unsigned int ihl;
    unsigned int ip_len;
    unsigned int tcp_off;
    unsigned int doff;
    unsigned int plen;
    unsigned int i;
    const volatile uint8_t *p;
    if (rx_len < 54U) return 0;
    if (genet9_be16(rx + 12) != 0x0800U) return 0;
    if ((rx[14] >> 4) != 4U) return 0;
    ihl = (unsigned int)(rx[14] & 0x0FU) * 4U;
    if (ihl < 20U) return 0;
    if (rx[14 + 9] != 6U) return 0;
    if (genet9_be32(rx + 26) != HOST_IP) return 0;
    if (genet9_be32(rx + 30) != OUR_IP) return 0;
    ip_len = genet9_be16(rx + 16);
    if (ip_len < ihl + 20U) return 0;
    tcp_off = 14U + ihl;
    if (rx_len < tcp_off + 20U) return 0;
    if (genet9_be16(rx + tcp_off) != TCP_PORT) return 0;
    if (genet9_be16(rx + tcp_off + 2) != TCP_PORT) return 0;
    doff = ((unsigned int)(rx[tcp_off + 12] >> 4) & 0x0FU) * 4U;
    if (doff < 20U || tcp_off + doff > rx_len) return 0;
    if (ip_len < ihl + doff) return 0;
    plen = ip_len - ihl - doff;
    if (tcp_off + doff + plen > rx_len) plen = rx_len - tcp_off - doff;
    if (plen < 20U) return 0;
    p = rx + tcp_off + doff;
    if (p[0] != (uint8_t)'H' || p[1] != (uint8_t)'T' ||
        p[2] != (uint8_t)'T' || p[3] != (uint8_t)'P') {
        return 0;
    }
    for (i = 0; i + 20U <= plen; i++) {
        if (p[i] == 0x0DU && p[i + 1U] == 0x0AU &&
            p[i + 2U] == 0x0DU && p[i + 3U] == 0x0AU) {
            if (genet9_be32(p + i + 4U) != 0xA1220001U) return 0;
            if (genet9_be32(p + i + 8U) != 0xA1220002U) return 0;
            if (genet9_be32(p + i + 12U) != 0xA1220003U) return 0;
            if (genet9_be32(p + i + 16U) != 0xA1220004U) return 0;
            return 1;
        }
    }
    return 0;
}

int kernel_genet18_selftest(void) {
    enum {
        V4_RDMA_PROD = 0x08U,
        V4_RDMA_CONS = 0x0C,
        RX_Q16_N = 256U,
        ISN = 0xA1220000U
    };
    unsigned long mac;
    unsigned long peer_mac;
    unsigned int cons;
    unsigned int tx_prod = 0;
    unsigned int syn_len;
    unsigned int req_len;
    unsigned int plen;
    uint32_t peer_seq = 0;
    uint64_t freq, start, now, ticks;

    if (genet18_probed) return genet18_ok_val;
    genet18_probed = 1;
    genet18_ok_val = 0;
    genet18_http_val = 0;
    genet18_body_val = 0;

    if (!kernel_genet13_selftest()) return 0;
    mac = kernel_genet3_mac();
    peer_mac = kernel_genet13_peer_mac();
    if (!mac || !peer_mac) return 0;
    if (!kernel_dma_alloc_nc(&genet18_tx_pa, &genet18_tx_nc)) return 0;
    if (!genet18_tx_nc) return 0;
    if (!genet10_unpark()) return 0;

    cons = rdma_ring16(V4_RDMA_PROD) & 0xFFFFU;
    rdma_ring16_wr(V4_RDMA_CONS, cons);

    syn_len = genet18_build_tcp((volatile uint8_t *)genet18_tx_nc,
                                mac, peer_mac, ISN, 0, 0x02U, 0);
    if (!genet18_tx_frame(syn_len, tx_prod)) {
        genet10_park();
        return 0;
    }
    tx_prod++;

    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(start));
    ticks = 3000ULL * freq / 1000ULL;
    do {
        unsigned int prod = rdma_ring16(V4_RDMA_PROD) & 0xFFFFU;
        while (cons != prod) {
            unsigned int idx = cons & (RX_Q16_N - 1U);
            unsigned int bd = RDMA_OFF + idx * DESC_BYTES;
            unsigned int rx_len = (G32(bd) >> 16) & 0x0FFFU;
            unsigned long buf_pa = (unsigned long)G32(bd + 4U);
            void *rx_nc = 0;
            if (rx_len >= 54U && kernel_dma_nc_from_pa(buf_pa, &rx_nc) && rx_nc) {
                if (genet18_synack((const volatile uint8_t *)rx_nc, rx_len,
                                   &peer_seq)) {
                    genet18_http_val = 1;
                }
            }
            cons = (cons + 1U) & 0xFFFFU;
            rdma_ring16_wr(V4_RDMA_CONS, cons);
            if (genet18_http_val) break;
        }
        if (genet18_http_val) break;
        genet_udelay(1000);
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(now));
    } while (now - start < ticks);

    if (genet18_http_val) {
        plen = genet18_http_req_len();
        req_len = genet18_build_tcp((volatile uint8_t *)genet18_tx_nc,
                                    mac, peer_mac, ISN + 1U, peer_seq + 1U,
                                    0x18U, plen);
        if (genet18_tx_frame(req_len, tx_prod)) {
            tx_prod++;
            __asm__ volatile("mrs %0, cntpct_el0" : "=r"(start));
            ticks = 3000ULL * freq / 1000ULL;
            do {
                unsigned int prod = rdma_ring16(V4_RDMA_PROD) & 0xFFFFU;
                while (cons != prod) {
                    unsigned int idx = cons & (RX_Q16_N - 1U);
                    unsigned int bd = RDMA_OFF + idx * DESC_BYTES;
                    unsigned int rx_len = (G32(bd) >> 16) & 0x0FFFU;
                    unsigned long buf_pa = (unsigned long)G32(bd + 4U);
                    void *rx_nc = 0;
                    if (rx_len >= 54U &&
                        kernel_dma_nc_from_pa(buf_pa, &rx_nc) && rx_nc) {
                        if (genet18_http_body((const volatile uint8_t *)rx_nc,
                                              rx_len)) {
                            genet18_body_val = 1;
                        }
                    }
                    cons = (cons + 1U) & 0xFFFFU;
                    rdma_ring16_wr(V4_RDMA_CONS, cons);
                    if (genet18_body_val) break;
                }
                if (genet18_body_val) break;
                genet_udelay(1000);
                __asm__ volatile("mrs %0, cntpct_el0" : "=r"(now));
            } while (now - start < ticks);
        }
    }

    genet10_park();
    if (genet18_http_val == 1U && genet18_body_val == 1U) {
        genet18_ok_val = 1;
        return 1;
    }
    return 0;
}

int          kernel_genet18_ok(void)   { return genet18_ok_val;   }
unsigned int kernel_genet18_http(void) { return genet18_http_val; }
unsigned int kernel_genet18_body(void) { return genet18_body_val; }

// V123: originate SNTP client (LI=0 VN=4 Mode=3) to 10.42.0.1:41251 (0xA123).
// Host helper replies Mode=4 with originate=our TX and a magic transmit stamp.
// Bounded unpark/poll/park. TX on a DMA NC page — not the 4 KiB core0 stack.
// No EL0. No boot event emit.
int kernel_genet19_selftest(void);

static int genet19_probed;
static int genet19_ok_val;
static unsigned int genet19_sntp_val;
static unsigned int genet19_sync_val;
static unsigned long genet19_tx_pa;
static void *genet19_tx_nc;

static int genet19_tx_frame(unsigned int tx_len, unsigned int tx_prod) {
    enum {
        V4_TDMA_CONS = 0x08U,
        V4_TDMA_PROD = 0x0C,
        TX_Q16_START = 128U,
        TX_Q16_N = 128U
    };
    uint64_t freq, t0, tnow, twait;
    unsigned int tcons;
    unsigned int tx_bd;
    uint32_t len_stat;

    if (tx_len < 60U || !genet19_tx_nc) return 0;
    __asm__ volatile("dsb sy" ::: "memory");
    tx_bd = TDMA_OFF + (TX_Q16_START + (tx_prod % TX_Q16_N)) * DESC_BYTES;
    len_stat = ((uint32_t)tx_len << 16) |
               (DMA_QTAG_MASK << DMA_TX_QTAG_SHIFT) |
               DMA_TX_APPEND_CRC | DMA_SOP | DMA_EOP;
    G32(tx_bd + 0U) = len_stat;
    G32(tx_bd + 4U) = (uint32_t)genet19_tx_pa;
    G32(tx_bd + 8U) = 0;
    __asm__ volatile("dsb sy" ::: "memory");
    tdma_ring16_wr(V4_TDMA_PROD, tx_prod + 1U);

    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(t0));
    twait = 20ULL * freq / 1000ULL;
    do {
        tcons = tdma_ring16(V4_TDMA_CONS) & 0xFFFFU;
        if (tcons == (tx_prod + 1U)) return 1;
        genet_udelay(100);
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(tnow));
    } while (tnow - t0 < twait);
    tcons = tdma_ring16(V4_TDMA_CONS) & 0xFFFFU;
    return (tcons != 0U) ? 1 : 0;
}

static unsigned int genet19_build_sntp(volatile uint8_t *tx,
                                       unsigned long mac,
                                       unsigned long peer_mac) {
    enum {
        OUR_IP = 0x0a2a0002U,
        HOST_IP = 0x0a2a0001U,
        UDP_PORT = 0xA123U,
        NTP_LEN = 48U,
        UDP_LEN = 56U,
        IP_LEN = 76U,
        WIRE = 90U
    };
    unsigned int i;
    uint32_t s;
    uint16_t ucsum;

    for (i = 0; i < 6U; i++) {
        tx[i] = (uint8_t)((peer_mac >> (40U - 8U * i)) & 0xFFUL);
        tx[6U + i] = (uint8_t)((mac >> (40U - 8U * i)) & 0xFFUL);
    }
    genet9_put16(tx + 12, 0x0800U);
    tx[14] = 0x45;
    tx[15] = 0;
    genet9_put16(tx + 16, IP_LEN);
    genet9_put16(tx + 18, UDP_PORT);
    genet9_put16(tx + 20, 0);
    tx[22] = 64;
    tx[23] = 0x11U;
    tx[24] = 0;
    tx[25] = 0;
    genet9_put32(tx + 26, OUR_IP);
    genet9_put32(tx + 30, HOST_IP);
    genet9_put16(tx + 24, genet9_csum(tx + 14, 20U));
    genet9_put16(tx + 34, UDP_PORT);
    genet9_put16(tx + 36, UDP_PORT);
    genet9_put16(tx + 38, UDP_LEN);
    tx[40] = 0;
    tx[41] = 0;
    for (i = 0; i < NTP_LEN; i++) tx[42U + i] = 0;
    tx[42] = 0x23;
    genet9_put32(tx + 82, 0xA1230001U);
    genet9_put32(tx + 86, 0xA1230002U);
    s = 0;
    s += (OUR_IP >> 16) & 0xFFFFU;
    s += OUR_IP & 0xFFFFU;
    s += (HOST_IP >> 16) & 0xFFFFU;
    s += HOST_IP & 0xFFFFU;
    s += 0x11U;
    s += UDP_LEN;
    for (i = 0; i + 1U < UDP_LEN; i += 2U) s += genet9_be16(tx + 34U + i);
    while (s >> 16) s = (s & 0xFFFFU) + (s >> 16);
    ucsum = (uint16_t)~s;
    if (ucsum == 0U) ucsum = 0xFFFFU;
    genet9_put16(tx + 40, ucsum);
    return WIRE;
}

static int genet19_sntp_reply(const volatile uint8_t *rx, unsigned int rx_len) {
    enum {
        OUR_IP = 0x0a2a0002U,
        HOST_IP = 0x0a2a0001U,
        UDP_PORT = 0xA123U
    };
    unsigned int ihl;
    unsigned int udp_off;
    unsigned int ntp_off;
    unsigned int udp_len;
    if (rx_len < 90U) return 0;
    if (genet9_be16(rx + 12) != 0x0800U) return 0;
    if ((rx[14] >> 4) != 4U) return 0;
    ihl = (unsigned int)(rx[14] & 0x0FU) * 4U;
    if (ihl < 20U) return 0;
    if (rx[14 + 9] != 0x11U) return 0;
    if (genet9_be32(rx + 26) != HOST_IP) return 0;
    if (genet9_be32(rx + 30) != OUR_IP) return 0;
    udp_off = 14U + ihl;
    if (rx_len < udp_off + 56U) return 0;
    if (genet9_be16(rx + udp_off) != UDP_PORT) return 0;
    if (genet9_be16(rx + udp_off + 2) != UDP_PORT) return 0;
    udp_len = genet9_be16(rx + udp_off + 4);
    if (udp_len < 56U) return 0;
    ntp_off = udp_off + 8U;
    if ((rx[ntp_off] & 0x07U) != 4U) return 0;
    if (genet9_be32(rx + ntp_off + 24U) != 0xA1230001U) return 0;
    if (genet9_be32(rx + ntp_off + 28U) != 0xA1230002U) return 0;
    if (genet9_be32(rx + ntp_off + 40U) != 0xA1230003U) return 0;
    if (genet9_be32(rx + ntp_off + 44U) != 0xA1230004U) return 0;
    return 1;
}

int kernel_genet19_selftest(void) {
    enum {
        V4_RDMA_PROD = 0x08U,
        V4_RDMA_CONS = 0x0C,
        RX_Q16_N = 256U
    };
    unsigned long mac;
    unsigned long peer_mac;
    unsigned int cons;
    unsigned int tx_prod = 0;
    unsigned int tx_len;
    uint64_t freq, start, now, ticks;

    if (genet19_probed) return genet19_ok_val;
    genet19_probed = 1;
    genet19_ok_val = 0;
    genet19_sntp_val = 0;
    genet19_sync_val = 0;

    if (!kernel_genet13_selftest()) return 0;
    mac = kernel_genet3_mac();
    peer_mac = kernel_genet13_peer_mac();
    if (!mac || !peer_mac) return 0;
    if (!kernel_dma_alloc_nc(&genet19_tx_pa, &genet19_tx_nc)) return 0;
    if (!genet19_tx_nc) return 0;
    if (!genet10_unpark()) return 0;

    cons = rdma_ring16(V4_RDMA_PROD) & 0xFFFFU;
    rdma_ring16_wr(V4_RDMA_CONS, cons);

    tx_len = genet19_build_sntp((volatile uint8_t *)genet19_tx_nc, mac, peer_mac);
    if (!genet19_tx_frame(tx_len, tx_prod)) {
        genet10_park();
        return 0;
    }
    genet19_sntp_val = 1;
    tx_prod++;

    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(start));
    ticks = 3000ULL * freq / 1000ULL;
    do {
        unsigned int prod = rdma_ring16(V4_RDMA_PROD) & 0xFFFFU;
        while (cons != prod) {
            unsigned int idx = cons & (RX_Q16_N - 1U);
            unsigned int bd = RDMA_OFF + idx * DESC_BYTES;
            unsigned int rx_len = (G32(bd) >> 16) & 0x0FFFU;
            unsigned long buf_pa = (unsigned long)G32(bd + 4U);
            void *rx_nc = 0;
            if (rx_len >= 90U && kernel_dma_nc_from_pa(buf_pa, &rx_nc) && rx_nc) {
                if (genet19_sntp_reply((const volatile uint8_t *)rx_nc, rx_len)) {
                    genet19_sync_val = 1;
                }
            }
            cons = (cons + 1U) & 0xFFFFU;
            rdma_ring16_wr(V4_RDMA_CONS, cons);
            if (genet19_sync_val) break;
        }
        if (genet19_sync_val) break;
        genet_udelay(1000);
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(now));
    } while (now - start < ticks);

    genet10_park();
    if (genet19_sntp_val == 1U && genet19_sync_val == 1U) {
        genet19_ok_val = 1;
        return 1;
    }
    return 0;
}

int          kernel_genet19_ok(void)   { return genet19_ok_val;   }
unsigned int kernel_genet19_sntp(void) { return genet19_sntp_val; }
unsigned int kernel_genet19_sync(void) { return genet19_sync_val; }

// V124: originate SSDP M-SEARCH to 10.42.0.1:41252 (0xA124).
// Host helper replies HTTP/1.1 200 + ST + 16-byte body. Bounded unpark/poll/park.
// TX on a DMA NC page — not the 4 KiB core0 stack. No EL0. No boot event emit.
int kernel_genet20_selftest(void);

static int genet20_probed;
static int genet20_ok_val;
static unsigned int genet20_ssdp_val;
static unsigned int genet20_reply_val;
static unsigned long genet20_tx_pa;
static void *genet20_tx_nc;

static int genet20_tx_frame(unsigned int tx_len, unsigned int tx_prod) {
    enum {
        V4_TDMA_CONS = 0x08U,
        V4_TDMA_PROD = 0x0C,
        TX_Q16_START = 128U,
        TX_Q16_N = 128U
    };
    uint64_t freq, t0, tnow, twait;
    unsigned int tcons;
    unsigned int tx_bd;
    uint32_t len_stat;

    if (tx_len < 60U || !genet20_tx_nc) return 0;
    __asm__ volatile("dsb sy" ::: "memory");
    tx_bd = TDMA_OFF + (TX_Q16_START + (tx_prod % TX_Q16_N)) * DESC_BYTES;
    len_stat = ((uint32_t)tx_len << 16) |
               (DMA_QTAG_MASK << DMA_TX_QTAG_SHIFT) |
               DMA_TX_APPEND_CRC | DMA_SOP | DMA_EOP;
    G32(tx_bd + 0U) = len_stat;
    G32(tx_bd + 4U) = (uint32_t)genet20_tx_pa;
    G32(tx_bd + 8U) = 0;
    __asm__ volatile("dsb sy" ::: "memory");
    tdma_ring16_wr(V4_TDMA_PROD, tx_prod + 1U);

    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(t0));
    twait = 20ULL * freq / 1000ULL;
    do {
        tcons = tdma_ring16(V4_TDMA_CONS) & 0xFFFFU;
        if (tcons == (tx_prod + 1U)) return 1;
        genet_udelay(100);
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(tnow));
    } while (tnow - t0 < twait);
    tcons = tdma_ring16(V4_TDMA_CONS) & 0xFFFFU;
    return (tcons != 0U) ? 1 : 0;
}

static unsigned int genet20_msearch_len(void) {
    static const char req[] =
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 10.42.0.1:41252\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "ST: urn:aether:device:v124\r\n"
        "MX: 1\r\n"
        "\r\n";
    unsigned int n = 0;
    while (req[n]) n++;
    return n;
}

static void genet20_put_msearch(volatile uint8_t *p) {
    static const char req[] =
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 10.42.0.1:41252\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "ST: urn:aether:device:v124\r\n"
        "MX: 1\r\n"
        "\r\n";
    unsigned int i = 0;
    while (req[i]) {
        p[i] = (uint8_t)req[i];
        i++;
    }
}

static unsigned int genet20_build_ssdp(volatile uint8_t *tx,
                                       unsigned long mac,
                                       unsigned long peer_mac) {
    enum {
        OUR_IP = 0x0a2a0002U,
        HOST_IP = 0x0a2a0001U,
        UDP_PORT = 0xA124U
    };
    unsigned int i;
    unsigned int plen = genet20_msearch_len();
    unsigned int udp_len = 8U + plen;
    unsigned int ip_len = 20U + udp_len;
    unsigned int wire = 14U + ip_len;
    uint32_t s;
    uint16_t ucsum;

    for (i = 0; i < 6U; i++) {
        tx[i] = (uint8_t)((peer_mac >> (40U - 8U * i)) & 0xFFUL);
        tx[6U + i] = (uint8_t)((mac >> (40U - 8U * i)) & 0xFFUL);
    }
    genet9_put16(tx + 12, 0x0800U);
    tx[14] = 0x45;
    tx[15] = 0;
    genet9_put16(tx + 16, (uint16_t)ip_len);
    genet9_put16(tx + 18, UDP_PORT);
    genet9_put16(tx + 20, 0);
    tx[22] = 64;
    tx[23] = 0x11U;
    tx[24] = 0;
    tx[25] = 0;
    genet9_put32(tx + 26, OUR_IP);
    genet9_put32(tx + 30, HOST_IP);
    genet9_put16(tx + 24, genet9_csum(tx + 14, 20U));
    genet9_put16(tx + 34, UDP_PORT);
    genet9_put16(tx + 36, UDP_PORT);
    genet9_put16(tx + 38, (uint16_t)udp_len);
    tx[40] = 0;
    tx[41] = 0;
    genet20_put_msearch(tx + 42);
    s = 0;
    s += (OUR_IP >> 16) & 0xFFFFU;
    s += OUR_IP & 0xFFFFU;
    s += (HOST_IP >> 16) & 0xFFFFU;
    s += HOST_IP & 0xFFFFU;
    s += 0x11U;
    s += udp_len;
    for (i = 0; i + 1U < udp_len; i += 2U) s += genet9_be16(tx + 34U + i);
    if (udp_len & 1U) s += (uint32_t)tx[34U + udp_len - 1U] << 8;
    while (s >> 16) s = (s & 0xFFFFU) + (s >> 16);
    ucsum = (uint16_t)~s;
    if (ucsum == 0U) ucsum = 0xFFFFU;
    genet9_put16(tx + 40, ucsum);
    if (wire < 60U) {
        for (i = wire; i < 60U; i++) tx[i] = 0;
        return 60U;
    }
    return wire;
}

static int genet20_ssdp_reply(const volatile uint8_t *rx, unsigned int rx_len) {
    enum {
        OUR_IP = 0x0a2a0002U,
        HOST_IP = 0x0a2a0001U,
        UDP_PORT = 0xA124U
    };
    unsigned int ihl;
    unsigned int udp_off;
    unsigned int pay_off;
    unsigned int udp_len;
    unsigned int plen;
    unsigned int i;
    const volatile uint8_t *p;
    static const char st[] = "urn:aether:device:v124";
    unsigned int st_n = 0;
    int have_st = 0;

    if (rx_len < 54U) return 0;
    if (genet9_be16(rx + 12) != 0x0800U) return 0;
    if ((rx[14] >> 4) != 4U) return 0;
    ihl = (unsigned int)(rx[14] & 0x0FU) * 4U;
    if (ihl < 20U) return 0;
    if (rx[14 + 9] != 0x11U) return 0;
    if (genet9_be32(rx + 26) != HOST_IP) return 0;
    if (genet9_be32(rx + 30) != OUR_IP) return 0;
    udp_off = 14U + ihl;
    if (rx_len < udp_off + 8U) return 0;
    if (genet9_be16(rx + udp_off) != UDP_PORT) return 0;
    if (genet9_be16(rx + udp_off + 2) != UDP_PORT) return 0;
    udp_len = genet9_be16(rx + udp_off + 4);
    if (udp_len < 28U) return 0;
    pay_off = udp_off + 8U;
    plen = udp_len - 8U;
    if (pay_off + plen > rx_len) plen = rx_len - pay_off;
    if (plen < 20U) return 0;
    p = rx + pay_off;
    if (p[0] != (uint8_t)'H' || p[1] != (uint8_t)'T' ||
        p[2] != (uint8_t)'T' || p[3] != (uint8_t)'P') {
        return 0;
    }
    while (st[st_n]) st_n++;
    for (i = 0; i + st_n <= plen; i++) {
        unsigned int j = 0;
        while (j < st_n && p[i + j] == (uint8_t)st[j]) j++;
        if (j == st_n) {
            have_st = 1;
            break;
        }
    }
    if (!have_st) return 0;
    for (i = 0; i + 20U <= plen; i++) {
        if (p[i] == 0x0DU && p[i + 1U] == 0x0AU &&
            p[i + 2U] == 0x0DU && p[i + 3U] == 0x0AU) {
            if (genet9_be32(p + i + 4U) != 0xA1240001U) return 0;
            if (genet9_be32(p + i + 8U) != 0xA1240002U) return 0;
            if (genet9_be32(p + i + 12U) != 0xA1240003U) return 0;
            if (genet9_be32(p + i + 16U) != 0xA1240004U) return 0;
            return 1;
        }
    }
    return 0;
}

int kernel_genet20_selftest(void) {
    enum {
        V4_RDMA_PROD = 0x08U,
        V4_RDMA_CONS = 0x0C,
        RX_Q16_N = 256U
    };
    unsigned long mac;
    unsigned long peer_mac;
    unsigned int cons;
    unsigned int tx_prod = 0;
    unsigned int tx_len;
    uint64_t freq, start, now, ticks;

    if (genet20_probed) return genet20_ok_val;
    genet20_probed = 1;
    genet20_ok_val = 0;
    genet20_ssdp_val = 0;
    genet20_reply_val = 0;

    if (!kernel_genet13_selftest()) return 0;
    mac = kernel_genet3_mac();
    peer_mac = kernel_genet13_peer_mac();
    if (!mac || !peer_mac) return 0;
    if (!kernel_dma_alloc_nc(&genet20_tx_pa, &genet20_tx_nc)) return 0;
    if (!genet20_tx_nc) return 0;
    if (!genet10_unpark()) return 0;

    cons = rdma_ring16(V4_RDMA_PROD) & 0xFFFFU;
    rdma_ring16_wr(V4_RDMA_CONS, cons);

    tx_len = genet20_build_ssdp((volatile uint8_t *)genet20_tx_nc, mac, peer_mac);
    if (!genet20_tx_frame(tx_len, tx_prod)) {
        genet10_park();
        return 0;
    }
    genet20_ssdp_val = 1;
    tx_prod++;

    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(start));
    ticks = 3000ULL * freq / 1000ULL;
    do {
        unsigned int prod = rdma_ring16(V4_RDMA_PROD) & 0xFFFFU;
        while (cons != prod) {
            unsigned int idx = cons & (RX_Q16_N - 1U);
            unsigned int bd = RDMA_OFF + idx * DESC_BYTES;
            unsigned int rx_len = (G32(bd) >> 16) & 0x0FFFU;
            unsigned long buf_pa = (unsigned long)G32(bd + 4U);
            void *rx_nc = 0;
            if (rx_len >= 54U && kernel_dma_nc_from_pa(buf_pa, &rx_nc) && rx_nc) {
                if (genet20_ssdp_reply((const volatile uint8_t *)rx_nc, rx_len)) {
                    genet20_reply_val = 1;
                }
            }
            cons = (cons + 1U) & 0xFFFFU;
            rdma_ring16_wr(V4_RDMA_CONS, cons);
            if (genet20_reply_val) break;
        }
        if (genet20_reply_val) break;
        genet_udelay(1000);
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(now));
    } while (now - start < ticks);

    genet10_park();
    if (genet20_ssdp_val == 1U && genet20_reply_val == 1U) {
        genet20_ok_val = 1;
        return 1;
    }
    return 0;
}

int          kernel_genet20_ok(void)    { return genet20_ok_val;    }
unsigned int kernel_genet20_ssdp(void)  { return genet20_ssdp_val;  }
unsigned int kernel_genet20_reply(void) { return genet20_reply_val; }

// V125: UMAC TX MIB (tx.pok at 0x4EC, tx.bytes at 0x4E8) after one 60-byte
// local-exp frame. Not an application protocol. Bounded unpark/poll/park.
// TX on a DMA NC page — not the 4 KiB core0 stack. No EL0. No boot event emit.
int kernel_genet21_selftest(void);

#define UMAC_MIB_TX_BYTES (UMAC_OFF + 0x4E8U)
#define UMAC_MIB_TX_POK (UMAC_OFF + 0x4ECU)

static int genet21_probed;
static int genet21_ok_val;
static unsigned int genet21_mib_val;
static unsigned int genet21_delta_val;
static unsigned long genet21_tx_pa;
static void *genet21_tx_nc;

static int genet21_tx_frame(unsigned int tx_len, unsigned int tx_prod) {
    enum {
        V4_TDMA_CONS = 0x08U,
        V4_TDMA_PROD = 0x0C,
        TX_Q16_START = 128U,
        TX_Q16_N = 128U
    };
    uint64_t freq, t0, tnow, twait;
    unsigned int tcons;
    unsigned int tx_bd;
    uint32_t len_stat;

    if (tx_len < 60U || !genet21_tx_nc) return 0;
    __asm__ volatile("dsb sy" ::: "memory");
    tx_bd = TDMA_OFF + (TX_Q16_START + (tx_prod % TX_Q16_N)) * DESC_BYTES;
    len_stat = ((uint32_t)tx_len << 16) |
               (DMA_QTAG_MASK << DMA_TX_QTAG_SHIFT) |
               DMA_TX_APPEND_CRC | DMA_SOP | DMA_EOP;
    G32(tx_bd + 0U) = len_stat;
    G32(tx_bd + 4U) = (uint32_t)genet21_tx_pa;
    G32(tx_bd + 8U) = 0;
    __asm__ volatile("dsb sy" ::: "memory");
    tdma_ring16_wr(V4_TDMA_PROD, tx_prod + 1U);

    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(t0));
    twait = 20ULL * freq / 1000ULL;
    do {
        tcons = tdma_ring16(V4_TDMA_CONS) & 0xFFFFU;
        if (tcons == (tx_prod + 1U)) return 1;
        genet_udelay(100);
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(tnow));
    } while (tnow - t0 < twait);
    tcons = tdma_ring16(V4_TDMA_CONS) & 0xFFFFU;
    return (tcons != 0U) ? 1 : 0;
}

static unsigned int genet21_build_exp(volatile uint8_t *tx,
                                      unsigned long mac,
                                      unsigned long peer_mac) {
    unsigned int i;
    for (i = 0; i < 6U; i++) {
        tx[i] = (uint8_t)((peer_mac >> (40U - 8U * i)) & 0xFFUL);
        tx[6U + i] = (uint8_t)((mac >> (40U - 8U * i)) & 0xFFUL);
    }
    genet9_put16(tx + 12, 0x88B5U);
    genet9_put32(tx + 14, 0xA1250001U);
    genet9_put32(tx + 18, 0xA1250002U);
    for (i = 22U; i < 60U; i++) tx[i] = 0;
    return 60U;
}

int kernel_genet21_selftest(void) {
    enum {
        V4_RDMA_PROD = 0x08U,
        V4_RDMA_CONS = 0x0C
    };
    unsigned long mac;
    unsigned long peer_mac;
    unsigned int cons;
    unsigned int tx_prod = 0;
    uint32_t pok0, pok1, bytes0, bytes1;

    if (genet21_probed) return genet21_ok_val;
    genet21_probed = 1;
    genet21_ok_val = 0;
    genet21_mib_val = 0;
    genet21_delta_val = 0;

    if (!kernel_genet20_selftest()) return 0;
    mac = kernel_genet3_mac();
    peer_mac = kernel_genet13_peer_mac();
    if (!mac || !peer_mac) return 0;
    if (!kernel_dma_alloc_nc(&genet21_tx_pa, &genet21_tx_nc)) return 0;
    if (!genet21_tx_nc) return 0;
    if (!genet10_unpark()) return 0;

    cons = rdma_ring16(V4_RDMA_PROD) & 0xFFFFU;
    rdma_ring16_wr(V4_RDMA_CONS, cons);

    pok0 = G32(UMAC_MIB_TX_POK);
    bytes0 = G32(UMAC_MIB_TX_BYTES);
    if (pok0 == 0xFFFFFFFFU || bytes0 == 0xFFFFFFFFU) {
        genet10_park();
        return 0;
    }
    genet21_mib_val = 1;

    genet21_build_exp((volatile uint8_t *)genet21_tx_nc, mac, peer_mac);
    if (!genet21_tx_frame(60U, tx_prod)) {
        genet10_park();
        return 0;
    }

    pok1 = G32(UMAC_MIB_TX_POK);
    bytes1 = G32(UMAC_MIB_TX_BYTES);
    genet10_park();

    if (pok1 == 0xFFFFFFFFU || bytes1 == 0xFFFFFFFFU) return 0;
    if (pok1 == (pok0 + 1U) && bytes1 >= (bytes0 + 60U)) {
        genet21_delta_val = 1;
        genet21_ok_val = 1;
        return 1;
    }
    return 0;
}

int          kernel_genet21_ok(void)    { return genet21_ok_val;    }
unsigned int kernel_genet21_mib(void)   { return genet21_mib_val;   }
unsigned int kernel_genet21_delta(void) { return genet21_delta_val; }

// V126: TCP helper liveness. Second originate to the existing V119
// listener on 10.42.0.1:41241 after a host stall/timeout. Requires
// genet13 (peer MAC) only — not genet15 — so later tokens stay honest
// when a hung recv would have zeroed the protocol chain. Bounded
// unpark/poll/park. TX on a DMA NC page — not the 4 KiB core0 stack.
// No EL0. No boot event emit.
int kernel_genet22_selftest(void);

static int genet22_probed;
static int genet22_ok_val;
static unsigned int genet22_live_val;
static unsigned int genet22_indep_val;
static unsigned long genet22_tx_pa;
static void *genet22_tx_nc;

static int genet22_tx_frame(unsigned int tx_len, unsigned int tx_prod) {
    enum {
        V4_TDMA_CONS = 0x08U,
        V4_TDMA_PROD = 0x0C,
        TX_Q16_START = 128U,
        TX_Q16_N = 128U
    };
    uint64_t freq, t0, tnow, twait;
    unsigned int tcons;
    unsigned int tx_bd;
    uint32_t len_stat;

    if (tx_len < 60U || !genet22_tx_nc) return 0;
    __asm__ volatile("dsb sy" ::: "memory");
    tx_bd = TDMA_OFF + (TX_Q16_START + (tx_prod % TX_Q16_N)) * DESC_BYTES;
    len_stat = ((uint32_t)tx_len << 16) |
               (DMA_QTAG_MASK << DMA_TX_QTAG_SHIFT) |
               DMA_TX_APPEND_CRC | DMA_SOP | DMA_EOP;
    G32(tx_bd + 0U) = len_stat;
    G32(tx_bd + 4U) = (uint32_t)genet22_tx_pa;
    G32(tx_bd + 8U) = 0;
    __asm__ volatile("dsb sy" ::: "memory");
    tdma_ring16_wr(V4_TDMA_PROD, tx_prod + 1U);

    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(t0));
    twait = 20ULL * freq / 1000ULL;
    do {
        tcons = tdma_ring16(V4_TDMA_CONS) & 0xFFFFU;
        if (tcons == (tx_prod + 1U)) return 1;
        genet_udelay(100);
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(tnow));
    } while (tnow - t0 < twait);
    tcons = tdma_ring16(V4_TDMA_CONS) & 0xFFFFU;
    return (tcons != 0U) ? 1 : 0;
}

static unsigned int genet22_build_tcp(volatile uint8_t *tx,
                                      unsigned long mac,
                                      unsigned long peer_mac,
                                      uint32_t seq,
                                      uint32_t ack,
                                      uint8_t flags,
                                      unsigned int plen) {
    enum {
        OUR_IP = 0x0a2a0002U,
        HOST_IP = 0x0a2a0001U,
        DST_PORT = 0xA119U,
        SRC_PORT = 0xA126U
    };
    unsigned int i;
    unsigned int tcp_len = 20U + plen;
    unsigned int wire = 34U + tcp_len;
    uint32_t s;
    uint16_t csum;

    for (i = 0; i < 6U; i++) {
        tx[i] = (uint8_t)((peer_mac >> (40U - 8U * i)) & 0xFFUL);
        tx[6U + i] = (uint8_t)((mac >> (40U - 8U * i)) & 0xFFUL);
    }
    genet9_put16(tx + 12, 0x0800U);
    tx[14] = 0x45;
    tx[15] = 0;
    genet9_put16(tx + 16, (uint16_t)(20U + tcp_len));
    genet9_put16(tx + 18, SRC_PORT);
    genet9_put16(tx + 20, 0);
    tx[22] = 64;
    tx[23] = 6U;
    tx[24] = 0;
    tx[25] = 0;
    genet9_put32(tx + 26, OUR_IP);
    genet9_put32(tx + 30, HOST_IP);
    genet9_put16(tx + 24, genet9_csum(tx + 14, 20U));
    genet9_put16(tx + 34, SRC_PORT);
    genet9_put16(tx + 36, DST_PORT);
    genet9_put32(tx + 38, seq);
    genet9_put32(tx + 42, ack);
    tx[46] = 0x50;
    tx[47] = flags;
    genet9_put16(tx + 48, 1024);
    tx[50] = 0;
    tx[51] = 0;
    tx[52] = 0;
    tx[53] = 0;
    if (plen >= 8U) {
        genet9_put32(tx + 54, 0xA1260001U);
        genet9_put32(tx + 58, 0xA1260002U);
    }
    s = 0;
    s += (OUR_IP >> 16) & 0xFFFFU;
    s += OUR_IP & 0xFFFFU;
    s += (HOST_IP >> 16) & 0xFFFFU;
    s += HOST_IP & 0xFFFFU;
    s += 6U;
    s += tcp_len;
    for (i = 0; i + 1U < tcp_len; i += 2U) s += genet9_be16(tx + 34U + i);
    if (tcp_len & 1U) s += (uint32_t)tx[34U + tcp_len - 1U] << 8;
    while (s >> 16) s = (s & 0xFFFFU) + (s >> 16);
    csum = (uint16_t)~s;
    if (csum == 0U) csum = 0xFFFFU;
    genet9_put16(tx + 50, csum);
    for (i = wire; i < 60U; i++) tx[i] = 0;
    return wire < 60U ? 60U : wire;
}

static int genet22_synack(const volatile uint8_t *rx,
                          unsigned int rx_len,
                          uint32_t *peer_seq) {
    enum {
        OUR_IP = 0x0a2a0002U,
        HOST_IP = 0x0a2a0001U,
        DST_PORT = 0xA119U,
        SRC_PORT = 0xA126U,
        ISN = 0xA1260000U
    };
    unsigned int ihl;
    unsigned int ip_len;
    unsigned int tcp_off;
    unsigned int doff;
    uint8_t flags;
    if (rx_len < 54U) return 0;
    if (genet9_be16(rx + 12) != 0x0800U) return 0;
    if ((rx[14] >> 4) != 4U) return 0;
    ihl = (unsigned int)(rx[14] & 0x0FU) * 4U;
    if (ihl < 20U) return 0;
    if (rx[14 + 9] != 6U) return 0;
    if (genet9_be32(rx + 26) != HOST_IP) return 0;
    if (genet9_be32(rx + 30) != OUR_IP) return 0;
    ip_len = genet9_be16(rx + 16);
    if (ip_len < ihl + 20U) return 0;
    tcp_off = 14U + ihl;
    if (rx_len < tcp_off + 20U) return 0;
    if (genet9_be16(rx + tcp_off) != DST_PORT) return 0;
    if (genet9_be16(rx + tcp_off + 2) != SRC_PORT) return 0;
    doff = ((unsigned int)(rx[tcp_off + 12] >> 4) & 0x0FU) * 4U;
    if (doff < 20U || tcp_off + doff > rx_len) return 0;
    flags = rx[tcp_off + 13];
    if ((flags & 0x12U) != 0x12U) return 0;
    if ((flags & 0x04U) != 0U) return 0;
    if (genet9_be32(rx + tcp_off + 8) != (ISN + 1U)) return 0;
    if (peer_seq) *peer_seq = genet9_be32(rx + tcp_off + 4);
    return 1;
}

static int genet22_tcp_echo(const volatile uint8_t *rx, unsigned int rx_len) {
    enum {
        OUR_IP = 0x0a2a0002U,
        HOST_IP = 0x0a2a0001U,
        DST_PORT = 0xA119U,
        SRC_PORT = 0xA126U
    };
    unsigned int ihl;
    unsigned int ip_len;
    unsigned int tcp_off;
    unsigned int doff;
    unsigned int plen;
    if (rx_len < 54U) return 0;
    if (genet9_be16(rx + 12) != 0x0800U) return 0;
    if ((rx[14] >> 4) != 4U) return 0;
    ihl = (unsigned int)(rx[14] & 0x0FU) * 4U;
    if (ihl < 20U) return 0;
    if (rx[14 + 9] != 6U) return 0;
    if (genet9_be32(rx + 26) != HOST_IP) return 0;
    if (genet9_be32(rx + 30) != OUR_IP) return 0;
    ip_len = genet9_be16(rx + 16);
    if (ip_len < ihl + 28U) return 0;
    tcp_off = 14U + ihl;
    if (rx_len < tcp_off + 28U) return 0;
    if (genet9_be16(rx + tcp_off) != DST_PORT) return 0;
    if (genet9_be16(rx + tcp_off + 2) != SRC_PORT) return 0;
    doff = ((unsigned int)(rx[tcp_off + 12] >> 4) & 0x0FU) * 4U;
    if (doff < 20U || tcp_off + doff + 8U > rx_len) return 0;
    if (ip_len < ihl + doff + 8U) return 0;
    plen = ip_len - ihl - doff;
    if (plen < 8U) return 0;
    if (genet9_be32(rx + tcp_off + doff) != 0xA1260001U) return 0;
    if (genet9_be32(rx + tcp_off + doff + 4) != 0xA1260002U) return 0;
    return 1;
}

int kernel_genet22_selftest(void) {
    enum {
        V4_RDMA_PROD = 0x08U,
        V4_RDMA_CONS = 0x0C,
        RX_Q16_N = 256U,
        ISN = 0xA1260000U
    };
    unsigned long mac;
    unsigned long peer_mac;
    unsigned int cons;
    unsigned int tx_prod = 0;
    unsigned int syn_len;
    unsigned int data_len;
    uint32_t peer_seq = 0;
    uint64_t freq, start, now, ticks;

    if (genet22_probed) return genet22_ok_val;
    genet22_probed = 1;
    genet22_ok_val = 0;
    genet22_live_val = 0;
    genet22_indep_val = 0;

    if (!kernel_genet13_selftest()) return 0;
    genet22_indep_val = 1;
    mac = kernel_genet3_mac();
    peer_mac = kernel_genet13_peer_mac();
    if (!mac || !peer_mac) return 0;
    if (!kernel_dma_alloc_nc(&genet22_tx_pa, &genet22_tx_nc)) return 0;
    if (!genet22_tx_nc) return 0;
    if (!genet10_unpark()) return 0;

    cons = rdma_ring16(V4_RDMA_PROD) & 0xFFFFU;
    rdma_ring16_wr(V4_RDMA_CONS, cons);

    syn_len = genet22_build_tcp((volatile uint8_t *)genet22_tx_nc,
                                mac, peer_mac, ISN, 0, 0x02U, 0);
    if (!genet22_tx_frame(syn_len, tx_prod)) {
        genet10_park();
        return 0;
    }
    tx_prod++;

    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(start));
    ticks = 3000ULL * freq / 1000ULL;
    do {
        unsigned int prod = rdma_ring16(V4_RDMA_PROD) & 0xFFFFU;
        while (cons != prod) {
            unsigned int idx = cons & (RX_Q16_N - 1U);
            unsigned int bd = RDMA_OFF + idx * DESC_BYTES;
            unsigned int rx_len = (G32(bd) >> 16) & 0x0FFFU;
            unsigned long buf_pa = (unsigned long)G32(bd + 4U);
            void *rx_nc = 0;
            if (rx_len >= 54U && kernel_dma_nc_from_pa(buf_pa, &rx_nc) && rx_nc) {
                if (genet22_synack((const volatile uint8_t *)rx_nc, rx_len,
                                   &peer_seq)) {
                    genet22_live_val = 1;
                }
            }
            cons = (cons + 1U) & 0xFFFFU;
            rdma_ring16_wr(V4_RDMA_CONS, cons);
            if (genet22_live_val) break;
        }
        if (genet22_live_val) break;
        genet_udelay(1000);
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(now));
    } while (now - start < ticks);

    if (genet22_live_val) {
        unsigned int echo = 0;
        data_len = genet22_build_tcp((volatile uint8_t *)genet22_tx_nc,
                                     mac, peer_mac, ISN + 1U, peer_seq + 1U,
                                     0x18U, 8U);
        if (genet22_tx_frame(data_len, tx_prod)) {
            tx_prod++;
            __asm__ volatile("mrs %0, cntpct_el0" : "=r"(start));
            ticks = 3000ULL * freq / 1000ULL;
            do {
                unsigned int prod = rdma_ring16(V4_RDMA_PROD) & 0xFFFFU;
                while (cons != prod) {
                    unsigned int idx = cons & (RX_Q16_N - 1U);
                    unsigned int bd = RDMA_OFF + idx * DESC_BYTES;
                    unsigned int rx_len = (G32(bd) >> 16) & 0x0FFFU;
                    unsigned long buf_pa = (unsigned long)G32(bd + 4U);
                    void *rx_nc = 0;
                    if (rx_len >= 54U &&
                        kernel_dma_nc_from_pa(buf_pa, &rx_nc) && rx_nc) {
                        if (genet22_tcp_echo((const volatile uint8_t *)rx_nc,
                                             rx_len)) {
                            echo = 1;
                        }
                    }
                    cons = (cons + 1U) & 0xFFFFU;
                    rdma_ring16_wr(V4_RDMA_CONS, cons);
                    if (echo) break;
                }
                if (echo) break;
                genet_udelay(1000);
                __asm__ volatile("mrs %0, cntpct_el0" : "=r"(now));
            } while (now - start < ticks);
        }
        if (!echo) genet22_live_val = 0;
    }

    genet10_park();
    if (genet22_live_val == 1U && genet22_indep_val == 1U) {
        genet22_ok_val = 1;
        return 1;
    }
    return 0;
}

int          kernel_genet22_ok(void)    { return genet22_ok_val;    }
unsigned int kernel_genet22_live(void)  { return genet22_live_val;  }
unsigned int kernel_genet22_indep(void) { return genet22_indep_val; }

// V127: INTRL2_0 TXDMA_DONE (bit 16) after one 60-byte local-exp frame.
// Poll the L2 status register only — do not enable a GIC line.
// Requires genet13 (peer MAC) only. Bounded unpark/poll/park.
// TX on a DMA NC page — not the 4 KiB core0 stack. No EL0. No boot event.
int kernel_genet23_selftest(void);

#define INTRL2_0_OFF 0x0200U
#define INTRL2_CPU_STAT (INTRL2_0_OFF + 0x00U)
#define INTRL2_CPU_CLEAR (INTRL2_0_OFF + 0x08U)
#define INTRL2_CPU_MASK_SET (INTRL2_0_OFF + 0x10U)
#define INTRL2_CPU_MASK_CLEAR (INTRL2_0_OFF + 0x14U)
#define UMAC_IRQ_TXDMA_DONE (1U << 16)

static int genet23_probed;
static int genet23_ok_val;
static unsigned int genet23_irq_val;
static unsigned int genet23_done_val;
static unsigned long genet23_tx_pa;
static void *genet23_tx_nc;

static int genet23_tx_frame(unsigned int tx_len, unsigned int tx_prod) {
    enum {
        V4_TDMA_CONS = 0x08U,
        V4_TDMA_PROD = 0x0C,
        TX_Q16_START = 128U,
        TX_Q16_N = 128U
    };
    uint64_t freq, t0, tnow, twait;
    unsigned int tcons;
    unsigned int tx_bd;
    uint32_t len_stat;

    if (tx_len < 60U || !genet23_tx_nc) return 0;
    __asm__ volatile("dsb sy" ::: "memory");
    tx_bd = TDMA_OFF + (TX_Q16_START + (tx_prod % TX_Q16_N)) * DESC_BYTES;
    len_stat = ((uint32_t)tx_len << 16) |
               (DMA_QTAG_MASK << DMA_TX_QTAG_SHIFT) |
               DMA_TX_APPEND_CRC | DMA_SOP | DMA_EOP;
    G32(tx_bd + 0U) = len_stat;
    G32(tx_bd + 4U) = (uint32_t)genet23_tx_pa;
    G32(tx_bd + 8U) = 0;
    __asm__ volatile("dsb sy" ::: "memory");
    tdma_ring16_wr(V4_TDMA_PROD, tx_prod + 1U);

    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(t0));
    twait = 20ULL * freq / 1000ULL;
    do {
        tcons = tdma_ring16(V4_TDMA_CONS) & 0xFFFFU;
        if (tcons == (tx_prod + 1U)) return 1;
        genet_udelay(100);
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(tnow));
    } while (tnow - t0 < twait);
    tcons = tdma_ring16(V4_TDMA_CONS) & 0xFFFFU;
    return (tcons != 0U) ? 1 : 0;
}

static unsigned int genet23_build_exp(volatile uint8_t *tx,
                                      unsigned long mac,
                                      unsigned long peer_mac) {
    unsigned int i;
    for (i = 0; i < 6U; i++) {
        tx[i] = (uint8_t)((peer_mac >> (40U - 8U * i)) & 0xFFUL);
        tx[6U + i] = (uint8_t)((mac >> (40U - 8U * i)) & 0xFFUL);
    }
    genet9_put16(tx + 12, 0x88B5U);
    genet9_put32(tx + 14, 0xA1270001U);
    genet9_put32(tx + 18, 0xA1270002U);
    for (i = 22U; i < 60U; i++) tx[i] = 0;
    return 60U;
}

int kernel_genet23_selftest(void) {
    enum {
        V4_RDMA_PROD = 0x08U,
        V4_RDMA_CONS = 0x0C
    };
    unsigned long mac;
    unsigned long peer_mac;
    unsigned int cons;
    unsigned int tx_prod = 0;
    uint32_t stat0, stat1;

    if (genet23_probed) return genet23_ok_val;
    genet23_probed = 1;
    genet23_ok_val = 0;
    genet23_irq_val = 0;
    genet23_done_val = 0;

    if (!kernel_genet13_selftest()) return 0;
    mac = kernel_genet3_mac();
    peer_mac = kernel_genet13_peer_mac();
    if (!mac || !peer_mac) return 0;
    if (!kernel_dma_alloc_nc(&genet23_tx_pa, &genet23_tx_nc)) return 0;
    if (!genet23_tx_nc) return 0;
    if (!genet10_unpark()) return 0;

    cons = rdma_ring16(V4_RDMA_PROD) & 0xFFFFU;
    rdma_ring16_wr(V4_RDMA_CONS, cons);
    tdma_ring16_wr(DMA_MBUF_DONE, 1);

    stat0 = G32(INTRL2_CPU_STAT);
    if (stat0 == 0xFFFFFFFFU) {
        genet10_park();
        return 0;
    }
    genet23_irq_val = 1;
    G32(INTRL2_CPU_CLEAR) = UMAC_IRQ_TXDMA_DONE;
    G32(INTRL2_CPU_MASK_CLEAR) = UMAC_IRQ_TXDMA_DONE;
    G32(INTRL2_CPU_CLEAR) = UMAC_IRQ_TXDMA_DONE;
    stat0 = G32(INTRL2_CPU_STAT);
    if (stat0 == 0xFFFFFFFFU || (stat0 & UMAC_IRQ_TXDMA_DONE) != 0U) {
        G32(INTRL2_CPU_MASK_SET) = UMAC_IRQ_TXDMA_DONE;
        genet10_park();
        return 0;
    }

    genet23_build_exp((volatile uint8_t *)genet23_tx_nc, mac, peer_mac);
    if (!genet23_tx_frame(60U, tx_prod)) {
        G32(INTRL2_CPU_MASK_SET) = UMAC_IRQ_TXDMA_DONE;
        genet10_park();
        return 0;
    }

    stat1 = G32(INTRL2_CPU_STAT);
    G32(INTRL2_CPU_CLEAR) = UMAC_IRQ_TXDMA_DONE;
    G32(INTRL2_CPU_MASK_SET) = UMAC_IRQ_TXDMA_DONE;
    genet10_park();

    if (stat1 == 0xFFFFFFFFU) return 0;
    if ((stat1 & UMAC_IRQ_TXDMA_DONE) != 0U) {
        genet23_done_val = 1;
        genet23_ok_val = 1;
        return 1;
    }
    return 0;
}

int          kernel_genet23_ok(void)    { return genet23_ok_val;    }
unsigned int kernel_genet23_irq(void)   { return genet23_irq_val;   }
unsigned int kernel_genet23_done(void)  { return genet23_done_val;  }

// V128: UMAC station filter with PROMISC off. Re-program UMAC_MAC0/1,
// clear CMD_PROMISC, originate ARP who-has, require a unicast reply
// whose dest MAC is ours. ARP is the stimulus, not a new protocol.
// Requires genet13 (peer MAC) only. Bounded unpark/poll/park.
// TX on a DMA NC page — not the 4 KiB core0 stack. No EL0. No boot event.
int kernel_genet24_selftest(void);

static int genet24_probed;
static int genet24_ok_val;
static unsigned int genet24_filter_val;
static unsigned int genet24_arp_val;
static unsigned long genet24_tx_pa;
static void *genet24_tx_nc;

static int genet24_tx_frame(unsigned int tx_len, unsigned int tx_prod) {
    enum {
        V4_TDMA_CONS = 0x08U,
        V4_TDMA_PROD = 0x0C,
        TX_Q16_START = 128U,
        TX_Q16_N = 128U
    };
    uint64_t freq, t0, tnow, twait;
    unsigned int tcons;
    unsigned int tx_bd;
    uint32_t len_stat;

    if (tx_len < 60U || !genet24_tx_nc) return 0;
    __asm__ volatile("dsb sy" ::: "memory");
    tx_bd = TDMA_OFF + (TX_Q16_START + (tx_prod % TX_Q16_N)) * DESC_BYTES;
    len_stat = ((uint32_t)tx_len << 16) |
               (DMA_QTAG_MASK << DMA_TX_QTAG_SHIFT) |
               DMA_TX_APPEND_CRC | DMA_SOP | DMA_EOP;
    G32(tx_bd + 0U) = len_stat;
    G32(tx_bd + 4U) = (uint32_t)genet24_tx_pa;
    G32(tx_bd + 8U) = 0;
    __asm__ volatile("dsb sy" ::: "memory");
    tdma_ring16_wr(V4_TDMA_PROD, tx_prod + 1U);

    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(t0));
    twait = 20ULL * freq / 1000ULL;
    do {
        tcons = tdma_ring16(V4_TDMA_CONS) & 0xFFFFU;
        if (tcons == (tx_prod + 1U)) return 1;
        genet_udelay(100);
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(tnow));
    } while (tnow - t0 < twait);
    tcons = tdma_ring16(V4_TDMA_CONS) & 0xFFFFU;
    return (tcons != 0U) ? 1 : 0;
}

static int genet24_unicast_arp(const volatile uint8_t *rx,
                               unsigned int rx_len,
                               unsigned long mac) {
    unsigned int i;
    if (!genet13_arp_reply(rx, rx_len, 0)) return 0;
    for (i = 0; i < 6U; i++) {
        uint8_t want = (uint8_t)((mac >> (40U - 8U * i)) & 0xFFUL);
        if (rx[i] != want) return 0;
    }
    return 1;
}

int kernel_genet24_selftest(void) {
    enum {
        V4_RDMA_PROD = 0x08U,
        V4_RDMA_CONS = 0x0C,
        RX_Q16_N = 256U
    };
    unsigned long mac;
    unsigned int cons;
    unsigned int tx_prod = 0;
    uint32_t mac0, mac1, cmd;
    uint64_t freq, start, now, ticks;

    if (genet24_probed) return genet24_ok_val;
    genet24_probed = 1;
    genet24_ok_val = 0;
    genet24_filter_val = 0;
    genet24_arp_val = 0;

    if (!kernel_genet13_selftest()) return 0;
    mac = kernel_genet3_mac();
    if (!mac) return 0;
    mac0 = (uint32_t)(mac >> 16);
    mac1 = (uint32_t)(mac & 0xFFFFUL);
    genet_wr32(UMAC_MAC0, mac0);
    genet_wr32(UMAC_MAC1, mac1);
    if (G32(UMAC_MAC0) != mac0) return 0;
    if ((G32(UMAC_MAC1) & 0xFFFFU) != mac1) return 0;
    if (!kernel_dma_alloc_nc(&genet24_tx_pa, &genet24_tx_nc)) return 0;
    if (!genet24_tx_nc) return 0;
    if (!genet10_unpark()) return 0;

    cmd = G32(UMAC_CMD);
    genet_wr32(UMAC_CMD, cmd & ~CMD_PROMISC);
    if ((G32(UMAC_CMD) & CMD_PROMISC) != 0U) {
        genet10_park();
        return 0;
    }
    genet24_filter_val = 1;

    cons = rdma_ring16(V4_RDMA_PROD) & 0xFFFFU;
    rdma_ring16_wr(V4_RDMA_CONS, cons);

    genet_write_arp((volatile uint8_t *)genet24_tx_nc, mac);
    if (!genet24_tx_frame(TX_FRAME_LEN, tx_prod)) {
        genet10_park();
        return 0;
    }

    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(start));
    ticks = 3000ULL * freq / 1000ULL;
    do {
        unsigned int prod = rdma_ring16(V4_RDMA_PROD) & 0xFFFFU;
        while (cons != prod) {
            unsigned int idx = cons & (RX_Q16_N - 1U);
            unsigned int bd = RDMA_OFF + idx * DESC_BYTES;
            unsigned int rx_len = (G32(bd) >> 16) & 0x0FFFU;
            unsigned long buf_pa = (unsigned long)G32(bd + 4U);
            void *rx_nc = 0;
            if (rx_len >= 42U && kernel_dma_nc_from_pa(buf_pa, &rx_nc) && rx_nc) {
                if (genet24_unicast_arp((const volatile uint8_t *)rx_nc,
                                        rx_len, mac)) {
                    genet24_arp_val = 1;
                }
            }
            cons = (cons + 1U) & 0xFFFFU;
            rdma_ring16_wr(V4_RDMA_CONS, cons);
            if (genet24_arp_val) break;
        }
        if (genet24_arp_val) break;
        genet_udelay(1000);
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(now));
    } while (now - start < ticks);

    genet10_park();
    if (genet24_filter_val == 1U && genet24_arp_val == 1U) {
        genet24_ok_val = 1;
        return 1;
    }
    return 0;
}

int          kernel_genet24_ok(void)     { return genet24_ok_val;     }
unsigned int kernel_genet24_filter(void) { return genet24_filter_val; }
unsigned int kernel_genet24_arp(void)    { return genet24_arp_val;    }

// V129: MDIO PHY identifier. IEEE PHYSID1 (reg 2) + PHYSID2 (reg 3).
// Fail-closed if either read fails or returns 0 / 0xFFFF (empty bus).
// Does not guess a Broadcom model number. No DMA. No unpark. No EL0.
// Requires V67 MDIO path only. UART token only. No boot event.
int kernel_genet25_selftest(void);

static int genet25_probed;
static int genet25_ok_val;
static unsigned int genet25_phy_val;
static unsigned int genet25_id_val;

int kernel_genet25_selftest(void) {
    unsigned int id1 = 0;
    unsigned int id2 = 0;

    if (genet25_probed) return genet25_ok_val;
    genet25_probed = 1;
    genet25_ok_val = 0;
    genet25_phy_val = 0;
    genet25_id_val = 0;

    if (!kernel_genet_selftest()) return 0;
    if (!genet_mdio_read(PHY_ADDR, MII_PHYSID1, &id1)) return 0;
    if (!genet_mdio_read(PHY_ADDR, MII_PHYSID2, &id2)) return 0;
    if (id1 == 0U || id1 == 0xFFFFU) return 0;
    if (id2 == 0U || id2 == 0xFFFFU) return 0;
    genet25_phy_val = 1;
    genet25_id_val = (id1 << 16) | id2;
    genet25_ok_val = 1;
    return 1;
}

int          kernel_genet25_ok(void)  { return genet25_ok_val;  }
unsigned int kernel_genet25_phy(void) { return genet25_phy_val; }
unsigned int kernel_genet25_id(void)  { return genet25_id_val;  }

// V130: UMAC_MAX_FRAME_LEN write+readback. Write 1518, require
// readback, restore 1536, require readback. Earlier bring-up writes
// 1536 without checking. No DMA. No MDIO. No EL0. UART token only.
int kernel_genet26_selftest(void);

static int genet26_probed;
static int genet26_ok_val;
static unsigned int genet26_len_val;
static unsigned int genet26_restore_val;

int kernel_genet26_selftest(void) {
    uint32_t got;

    if (genet26_probed) return genet26_ok_val;
    genet26_probed = 1;
    genet26_ok_val = 0;
    genet26_len_val = 0;
    genet26_restore_val = 0;

    if (!kernel_genet_selftest()) return 0;
    genet_wr32(UMAC_MAX_FRAME_LEN, 1518U);
    got = G32(UMAC_MAX_FRAME_LEN) & 0xFFFFU;
    if (got != 1518U) return 0;
    genet26_len_val = 1;
    genet_wr32(UMAC_MAX_FRAME_LEN, 1536U);
    got = G32(UMAC_MAX_FRAME_LEN) & 0xFFFFU;
    if (got != 1536U) return 0;
    genet26_restore_val = 1;
    genet26_ok_val = 1;
    return 1;
}

int          kernel_genet26_ok(void)      { return genet26_ok_val;      }
unsigned int kernel_genet26_len(void)     { return genet26_len_val;     }
unsigned int kernel_genet26_restore(void) { return genet26_restore_val; }

// V131: MDIO write+readback of BMCR. Write the same value we read so
// the link is not changed. Fail-closed if BMCR is 0/0xFFFF, the write
// times out, or the second read mismatches. Not a BMSR/link clone.
// No DMA. No EL0. UART token only.
int kernel_genet27_selftest(void);

static int genet27_probed;
static int genet27_ok_val;
static unsigned int genet27_wr_val;
static unsigned int genet27_match_val;

int kernel_genet27_selftest(void) {
    unsigned int bmcr = 0;
    unsigned int back = 0;

    if (genet27_probed) return genet27_ok_val;
    genet27_probed = 1;
    genet27_ok_val = 0;
    genet27_wr_val = 0;
    genet27_match_val = 0;

    if (!kernel_genet_selftest()) return 0;
    if (!genet_mdio_read(PHY_ADDR, MII_BMCR, &bmcr)) return 0;
    if (bmcr == 0U || bmcr == 0xFFFFU) return 0;
    if (!genet_mdio_write(PHY_ADDR, MII_BMCR, bmcr)) return 0;
    genet27_wr_val = 1;
    if (!genet_mdio_read(PHY_ADDR, MII_BMCR, &back)) return 0;
    if (back != bmcr) return 0;
    genet27_match_val = 1;
    genet27_ok_val = 1;
    return 1;
}

int          kernel_genet27_ok(void)    { return genet27_ok_val;    }
unsigned int kernel_genet27_wr(void)    { return genet27_wr_val;    }
unsigned int kernel_genet27_match(void) { return genet27_match_val; }

// V132: UMAC MDF perfect-match filter. Program slot 0 with the
// mailbox station MAC (Linux 2+4 byte layout), enable bit 16, then
// restore the leftover CTRL/ADDR words. Unused hardware — not the
// UMAC_MAC0/1 station filter and not PROMISC. No DMA. No EL0.
int kernel_genet28_selftest(void);

static int genet28_probed;
static int genet28_ok_val;
static unsigned int genet28_mdf_val;
static unsigned int genet28_restore_val;

int kernel_genet28_selftest(void) {
    unsigned long mac;
    uint32_t want0;
    uint32_t want1;
    uint32_t saved_ctrl;
    uint32_t saved0;
    uint32_t saved1;
    uint32_t enable;

    if (genet28_probed) return genet28_ok_val;
    genet28_probed = 1;
    genet28_ok_val = 0;
    genet28_mdf_val = 0;
    genet28_restore_val = 0;

    if (!kernel_genet_selftest()) return 0;
    if (!kernel_genet3_selftest()) return 0;
    mac = kernel_genet3_mac();
    if (mac == 0UL) return 0;

    saved_ctrl = G32(UMAC_MDF_CTRL);
    saved0 = G32(UMAC_MDF_ADDR);
    saved1 = G32(UMAC_MDF_ADDR + 4U);
    if (saved_ctrl == 0xDEADDEADU || saved0 == 0xDEADDEADU ||
        saved1 == 0xDEADDEADU) {
        return 0;
    }

    // Linux bcmgenet_set_mdf_addr: word0 = mac[0:1], word1 = mac[2:5].
    want0 = (uint32_t)((mac >> 32) & 0xFFFFUL);
    want1 = (uint32_t)(mac & 0xFFFFFFFFUL);
    enable = 1U << 16;
    genet_wr32(UMAC_MDF_ADDR, want0);
    genet_wr32(UMAC_MDF_ADDR + 4U, want1);
    genet_wr32(UMAC_MDF_CTRL, enable);
    if ((G32(UMAC_MDF_ADDR) & 0xFFFFU) != want0) return 0;
    if (G32(UMAC_MDF_ADDR + 4U) != want1) return 0;
    if ((G32(UMAC_MDF_CTRL) & enable) == 0U) return 0;
    genet28_mdf_val = 1;

    genet_wr32(UMAC_MDF_ADDR, saved0);
    genet_wr32(UMAC_MDF_ADDR + 4U, saved1);
    genet_wr32(UMAC_MDF_CTRL, saved_ctrl);
    if (G32(UMAC_MDF_ADDR) != saved0) return 0;
    if (G32(UMAC_MDF_ADDR + 4U) != saved1) return 0;
    if (G32(UMAC_MDF_CTRL) != saved_ctrl) return 0;
    genet28_restore_val = 1;
    genet28_ok_val = 1;
    return 1;
}

int          kernel_genet28_ok(void)      { return genet28_ok_val;      }
unsigned int kernel_genet28_mdf(void)     { return genet28_mdf_val;     }
unsigned int kernel_genet28_restore(void) { return genet28_restore_val; }

// V133: RBUF RXCHK enable. Program RBUF_CHK_CTRL bit 0 (Linux
// RBUF_RXCHK_EN), require clear then set, then restore leftover.
// Unused hardware — not a UMAC poke and not TX csum / 64B status
// blocks. No DMA. No EL0. UART token only.
int kernel_genet29_selftest(void);

static int genet29_probed;
static int genet29_ok_val;
static unsigned int genet29_rxchk_val;
static unsigned int genet29_restore_val;

int kernel_genet29_selftest(void) {
    uint32_t saved;
    uint32_t got;

    if (genet29_probed) return genet29_ok_val;
    genet29_probed = 1;
    genet29_ok_val = 0;
    genet29_rxchk_val = 0;
    genet29_restore_val = 0;

    if (!kernel_genet_selftest()) return 0;

    saved = G32(RBUF_OFF + RBUF_CHK_CTRL);
    if (saved == 0xDEADDEADU || saved == 0xFFFFFFFFU) return 0;

    genet_wr32(RBUF_OFF + RBUF_CHK_CTRL, saved & ~RBUF_RXCHK_EN);
    got = G32(RBUF_OFF + RBUF_CHK_CTRL);
    if ((got & RBUF_RXCHK_EN) != 0U) return 0;

    genet_wr32(RBUF_OFF + RBUF_CHK_CTRL, saved | RBUF_RXCHK_EN);
    got = G32(RBUF_OFF + RBUF_CHK_CTRL);
    if ((got & RBUF_RXCHK_EN) == 0U) return 0;
    genet29_rxchk_val = 1;

    genet_wr32(RBUF_OFF + RBUF_CHK_CTRL, saved);
    if (G32(RBUF_OFF + RBUF_CHK_CTRL) != saved) return 0;
    genet29_restore_val = 1;
    genet29_ok_val = 1;
    return 1;
}

int          kernel_genet29_ok(void)      { return genet29_ok_val;      }
unsigned int kernel_genet29_rxchk(void)   { return genet29_rxchk_val;   }
unsigned int kernel_genet29_restore(void) { return genet29_restore_val; }

// V134: TBUF EEE enable. Program TBUF_ENERGY_CTRL bit 0 (Linux
// TBUF_EEE_EN), require clear then set, then restore leftover.
// Unused hardware — not RBUF RXCHK, not a UMAC poke, and not
// TX csum / 64B status blocks. No DMA. No EL0. UART token only.
#define GENET_TBUF_OFF 0x0600U
#define TBUF_ENERGY_CTRL 0x14U
#define TBUF_EEE_EN (1U << 0)

int kernel_genet30_selftest(void);

static int genet30_probed;
static int genet30_ok_val;
static unsigned int genet30_eee_val;
static unsigned int genet30_restore_val;

int kernel_genet30_selftest(void) {
    uint32_t saved;
    uint32_t got;

    if (genet30_probed) return genet30_ok_val;
    genet30_probed = 1;
    genet30_ok_val = 0;
    genet30_eee_val = 0;
    genet30_restore_val = 0;

    if (!kernel_genet_selftest()) return 0;

    saved = G32(GENET_TBUF_OFF + TBUF_ENERGY_CTRL);
    if (saved == 0xDEADDEADU || saved == 0xFFFFFFFFU) return 0;

    genet_wr32(GENET_TBUF_OFF + TBUF_ENERGY_CTRL, saved & ~TBUF_EEE_EN);
    got = G32(GENET_TBUF_OFF + TBUF_ENERGY_CTRL);
    if ((got & TBUF_EEE_EN) != 0U) return 0;

    genet_wr32(GENET_TBUF_OFF + TBUF_ENERGY_CTRL, saved | TBUF_EEE_EN);
    got = G32(GENET_TBUF_OFF + TBUF_ENERGY_CTRL);
    if ((got & TBUF_EEE_EN) == 0U) return 0;
    genet30_eee_val = 1;

    genet_wr32(GENET_TBUF_OFF + TBUF_ENERGY_CTRL, saved);
    if (G32(GENET_TBUF_OFF + TBUF_ENERGY_CTRL) != saved) return 0;
    genet30_restore_val = 1;
    genet30_ok_val = 1;
    return 1;
}

int          kernel_genet30_ok(void)      { return genet30_ok_val;      }
unsigned int kernel_genet30_eee(void)     { return genet30_eee_val;     }
unsigned int kernel_genet30_restore(void) { return genet30_restore_val; }

// V135: HFB filter-0 enable bitmap. Program HFB_FLT_ENABLE bit 0
// at hfb_reg_offset 0xFC00+0x04, require clear then set, then
// restore leftover. Does not set HFB_CTRL RBUF_HFB_EN (engine
// stays off). Unused hardware filter block — not TBUF EEE, not
// RBUF RXCHK, not a UMAC poke, and not TX csum / 64B status
// blocks. No DMA. No EL0. UART token only.
#define HFB_FLT0_EN (1U << 0)

int kernel_genet31_selftest(void);

static int genet31_probed;
static int genet31_ok_val;
static unsigned int genet31_hfb_val;
static unsigned int genet31_restore_val;

int kernel_genet31_selftest(void) {
    uint32_t saved;
    uint32_t got;

    if (genet31_probed) return genet31_ok_val;
    genet31_probed = 1;
    genet31_ok_val = 0;
    genet31_hfb_val = 0;
    genet31_restore_val = 0;

    if (!kernel_genet_selftest()) return 0;

    saved = G32(HFB_REG_OFF + HFB_FLT_ENABLE);
    if (saved == 0xDEADDEADU || saved == 0xFFFFFFFFU) return 0;

    genet_wr32(HFB_REG_OFF + HFB_FLT_ENABLE, saved & ~HFB_FLT0_EN);
    got = G32(HFB_REG_OFF + HFB_FLT_ENABLE);
    if ((got & HFB_FLT0_EN) != 0U) return 0;

    genet_wr32(HFB_REG_OFF + HFB_FLT_ENABLE, saved | HFB_FLT0_EN);
    got = G32(HFB_REG_OFF + HFB_FLT_ENABLE);
    if ((got & HFB_FLT0_EN) == 0U) return 0;
    genet31_hfb_val = 1;

    genet_wr32(HFB_REG_OFF + HFB_FLT_ENABLE, saved);
    if (G32(HFB_REG_OFF + HFB_FLT_ENABLE) != saved) return 0;
    genet31_restore_val = 1;
    genet31_ok_val = 1;
    return 1;
}

int          kernel_genet31_ok(void)      { return genet31_ok_val;      }
unsigned int kernel_genet31_hfb(void)     { return genet31_hfb_val;     }
unsigned int kernel_genet31_restore(void) { return genet31_restore_val; }

// V136: UMAC TX MIB reset. Pulse UMAC_MIB_CTRL UMAC_MIB_RESET_TX
// (Linux bcmgenet_mib_init), require leftover tx.pok/tx.bytes
// non-zero (non-vacuous), then counters == 0 after reset+clear.
// Unused MIB control — not an RBUF/TBUF/HFB enable writeback,
// not TX csum / 64B status blocks. No DMA. No EL0. UART token only.
#define UMAC_MIB_CTRL (UMAC_OFF + 0x580U)
#define UMAC_MIB_RESET_TX 0x4U

int kernel_genet32_selftest(void);

static int genet32_probed;
static int genet32_ok_val;
static unsigned int genet32_rst_val;
static unsigned int genet32_zero_val;

int kernel_genet32_selftest(void) {
    uint32_t pok0;
    uint32_t bytes0;
    uint32_t pok1;
    uint32_t bytes1;
    uint32_t saved;

    if (genet32_probed) return genet32_ok_val;
    genet32_probed = 1;
    genet32_ok_val = 0;
    genet32_rst_val = 0;
    genet32_zero_val = 0;

    if (!kernel_genet_selftest()) return 0;

    pok0 = G32(UMAC_MIB_TX_POK);
    bytes0 = G32(UMAC_MIB_TX_BYTES);
    if (pok0 == 0xDEADDEADU || pok0 == 0xFFFFFFFFU) return 0;
    if (bytes0 == 0xDEADDEADU || bytes0 == 0xFFFFFFFFU) return 0;
    // Non-vacuity: leftover TX MIB must show prior work (V125+).
    if (pok0 == 0U && bytes0 == 0U) return 0;

    saved = G32(UMAC_MIB_CTRL);
    if (saved == 0xDEADDEADU || saved == 0xFFFFFFFFU) return 0;

    genet_wr32(UMAC_MIB_CTRL, saved | UMAC_MIB_RESET_TX);
    genet_wr32(UMAC_MIB_CTRL, saved & ~UMAC_MIB_RESET_TX);
    if (G32(UMAC_MIB_CTRL) != (saved & ~UMAC_MIB_RESET_TX)) return 0;
    genet32_rst_val = 1;

    pok1 = G32(UMAC_MIB_TX_POK);
    bytes1 = G32(UMAC_MIB_TX_BYTES);
    if (pok1 != 0U || bytes1 != 0U) return 0;
    genet32_zero_val = 1;
    genet32_ok_val = 1;
    return 1;
}

int          kernel_genet32_ok(void)   { return genet32_ok_val;   }
unsigned int kernel_genet32_rst(void)  { return genet32_rst_val;  }
unsigned int kernel_genet32_zero(void) { return genet32_zero_val; }
