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
