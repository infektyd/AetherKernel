// Runtime V61: BCM2711 PCIe RC bring-up (BRCMSTB PCIe controller).
// Runtime V62: VL805 USB 3.0 controller discovery (config space + BAR0 assignment).
// Sources/Support/kernel_pcie.c
//
// BCM2711 PCIe outbound window: ARM phys 0x600000000 → PCIe bus 0xF8000000, 64MB.
// VL805 BAR0 is assigned at PCIe 0xF8000000; CPU accesses xHCI MMIO at ARM 0x600000000.
// Inbound DMA window: PCIe bus 0x400000000 → ARM phys 0x0 (device DMA addr = phys + 0x400000000).

#include "include/Support.h"
#include <stdint.h>

// ── BCM2711 PCIe RC MMIO (ARM phys) ───────────────────────────────────────
#define PCIE_BASE 0xFD500000UL
#define PCIE32(off) (*(volatile uint32_t *)(PCIE_BASE + (unsigned long)(off)))

// RGR1 reset block
#define OFF_RGR1_SW_INIT_1   0x9210U
#define RGR1_PERST           (1U << 0)
#define RGR1_BRIDGE_SW_INIT  (1U << 1)

// MISC control
#define OFF_MISC_MISC_CTRL   0x4008U
#define OFF_MISC_WIN0_LO     0x400CU   // PCIe bus addr lo for outbound win 0
#define OFF_MISC_WIN0_HI     0x4010U   // PCIe bus addr hi for outbound win 0
#define OFF_MISC_RC_BAR1_LO  0x402CU
#define OFF_MISC_RC_BAR2_LO  0x4034U
#define OFF_MISC_RC_BAR2_HI  0x4038U
#define OFF_MISC_RC_BAR3_LO  0x403CU
#define OFF_MISC_PCIE_STATUS 0x4068U
#define OFF_MISC_WIN0_BL     0x4070U   // CPU phys [base_mb,limit_mb] for win 0
#define OFF_MISC_WIN0_BHI    0x4080U   // CPU phys base mb high byte
#define OFF_MISC_WIN0_LHI    0x4084U   // CPU phys limit mb high byte
#define OFF_MISC_HARD_DEBUG  0x4204U
#define OFF_MISC_REVISION    0x406CU

#define STATUS_PHYLINKUP     (1U << 4)
#define STATUS_DL_ACTIVE     (1U << 5)
#define STATUS_PORT_RC       (1U << 7)
#define HARD_DEBUG_SERDES_IDDQ   (1U << 27)
#define HARD_DEBUG_CLKREQ_DBG_EN (1U << 0)

// MSI interrupt mask
#define OFF_MSI_CLR          0x4508U
#define OFF_MSI_MASK_SET     0x4510U

// Config space indirect access
#define OFF_EXT_CFG_DATA     0x8000U
#define OFF_EXT_CFG_INDEX    0x9000U

// Link status in PCIe standard capability (at cap base 0xAC)
#define OFF_LNKCTL_STA       0x00BCU   // [31:16]=LNKSTA, [15:0]=LNKCTL

// VL805 identifiers
#define VL805_VID   0x1106U
#define VL805_DID   0x3483U

// PCIe outbound window: CPU phys 0x600000000 → PCIe bus 0xF8000000, 64 MB
#define CPU_WIN_BASE  0x600000000ULL
#define PCIE_WIN_BASE 0xF8000000ULL
#define WIN_SIZE_MB   64U

// ARM phys address of VL805 xHCI MMIO (via outbound window)
#define VL805_MMIO_ARM_PHYS 0x600000000UL

// ── Module state ──────────────────────────────────────────────────────────
static int pcie_link_ok;
static unsigned int pcie_speed_val;  // LNKSTA[3:0]: 1=Gen1, 2=Gen2
static unsigned int pcie_width_val;  // LNKSTA[9:4]: negotiated width

static int vl805_ok_val;
static uint32_t vl805_raw_viddid_val;
static uint32_t vl805_hw_rev_val;
static uint32_t vl805_pcie_status_val; // PCIE_STATUS captured during EXT_CFG attempts
static uint32_t vl805_rgr1_val;        // RGR1_SW_INIT_1 at time of EXT_CFG probe
static uint32_t vl805_busnr_val;       // DBI bridge bus numbers (SecBus byte)

// ── Timing (generic timer at 54 MHz on Pi4) ────────────────────────────────
static void pcie_udelay(unsigned int us) {
    uint64_t freq, start, now;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(start));
    uint64_t ticks = (uint64_t)us * freq / 1000000ULL;
    do {
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(now));
    } while (now - start < ticks);
}

// ── Inbound BAR size encoding: ilog2(size) - 15 for 64KB–64GB ─────────────
static uint32_t encode_ibar_size(uint64_t sz) {
    int s = 0;
    uint64_t v = sz;
    if (v == 0) return 0U;
    while (v > 1) { v >>= 1; s++; }
    if (s < 16 || s > 36) return 0U;
    return (uint32_t)(s - 15);
}

// ── Outbound window (win 0) ────────────────────────────────────────────────
static void pcie_set_outbound_win0(uint64_t cpu_phys, uint64_t pcie_addr, uint32_t size_mb) {
    // PCIe target address
    PCIE32(OFF_MISC_WIN0_LO) = (uint32_t)(pcie_addr & 0xFFFFFFFFU) | 1U; // WIN_SIZE_UNIT=BIT(0) enables window
    PCIE32(OFF_MISC_WIN0_HI) = (uint32_t)(pcie_addr >> 32);

    // CPU physical range in MB units
    uint32_t base_mb  = (uint32_t)(cpu_phys / (1024ULL * 1024ULL));
    uint32_t limit_mb = base_mb + size_mb - 1U;

    uint32_t bl = PCIE32(OFF_MISC_WIN0_BL);
    bl = (bl & ~0xFFF0U)     | ((base_mb  & 0xFFFU) << 4);
    bl = (bl & ~0xFFF00000U) | ((limit_mb & 0xFFFU) << 20);
    PCIE32(OFF_MISC_WIN0_BL) = bl;

    PCIE32(OFF_MISC_WIN0_BHI) = (PCIE32(OFF_MISC_WIN0_BHI)  & ~0xFFU) | ((base_mb  >> 12) & 0xFFU);
    PCIE32(OFF_MISC_WIN0_LHI) = (PCIE32(OFF_MISC_WIN0_LHI)  & ~0xFFU) | ((limit_mb >> 12) & 0xFFU);
}

// ── Config space 32-bit read ───────────────────────────────────────────────
// bus=0,dev=0 → RC direct registers; bus>=1 → EXT_CFG_INDEX/DATA
static uint32_t pcie_cfg_rd(unsigned int bus, unsigned int dev, unsigned int fn, unsigned int off) {
    if (bus == 0 && dev == 0) {
        return PCIE32(off);
    }
    if ((PCIE32(OFF_MISC_PCIE_STATUS) & (STATUS_PHYLINKUP | STATUS_DL_ACTIVE)) !=
            (STATUS_PHYLINKUP | STATUS_DL_ACTIVE)) {
        return 0xFFFFFFFFU;
    }
    PCIE32(OFF_EXT_CFG_INDEX) = (bus << 20) | (dev << 15) | (fn << 12);
    __asm__ volatile("dsb sy" ::: "memory");
    return PCIE32(OFF_EXT_CFG_DATA + off);
}

static void pcie_cfg_wr(unsigned int bus, unsigned int dev, unsigned int fn, unsigned int off, uint32_t val) {
    if (bus == 0 && dev == 0) {
        PCIE32(off) = val;
        return;
    }
    if ((PCIE32(OFF_MISC_PCIE_STATUS) & (STATUS_PHYLINKUP | STATUS_DL_ACTIVE)) !=
            (STATUS_PHYLINKUP | STATUS_DL_ACTIVE)) {
        return;
    }
    PCIE32(OFF_EXT_CFG_INDEX) = (bus << 20) | (dev << 15) | (fn << 12);
    __asm__ volatile("dsb sy" ::: "memory");
    PCIE32(OFF_EXT_CFG_DATA + off) = val;
}

// ── V61: PCIe RC bring-up ──────────────────────────────────────────────────
static int pcie_probed;

int kernel_pcie_selftest(void) {
    if (pcie_probed) return pcie_link_ok;
    pcie_probed = 1;
    pcie_link_ok = 0;

    // If Pi firmware already left the PCIe link up, preserve it — the VL805
    // needs its EEPROM firmware (loaded autonomously after PERST#) to respond
    // to config-space reads, which takes 500ms+.  A PERST# cycle from our side
    // would reset the VL805 and stall VL805 config access for that duration.
    // We still claim the outbound MMIO window and record the link stats.
    uint32_t st0 = PCIE32(OFF_MISC_PCIE_STATUS);
    int already_up = ((st0 & (STATUS_PHYLINKUP | STATUS_DL_ACTIVE)) ==
                      (STATUS_PHYLINKUP | STATUS_DL_ACTIVE));

    if (!already_up) {
        // Full PERST# cycle + bring-up (e.g. no Pi firmware, or cold start).

        // 1. Assert PERST# + bridge SW reset
        PCIE32(OFF_RGR1_SW_INIT_1) |= (RGR1_PERST | RGR1_BRIDGE_SW_INIT);
        pcie_udelay(100);

        // 2. Deassert bridge SW reset (keep PERST# asserted)
        PCIE32(OFF_RGR1_SW_INIT_1) &= ~RGR1_BRIDGE_SW_INIT;
        pcie_udelay(100);

        // 3. Power up SerDes (clear IDDQ)
        PCIE32(OFF_MISC_HARD_DEBUG) &= ~HARD_DEBUG_SERDES_IDDQ;
        pcie_udelay(100);

        // 6. Mask all MSI interrupts
        PCIE32(OFF_MSI_MASK_SET) = 0xFFFFFFFFU;
        PCIE32(OFF_MSI_CLR)      = 0xFFFFFFFFU;

        // 7. Deassert PERST# — device starts link training
        PCIE32(OFF_RGR1_SW_INIT_1) &= ~RGR1_PERST;
        pcie_udelay(120000);

        // 8. Poll for link-up, up to 100ms
        int linked = 0;
        for (int i = 0; i < 1000 && !linked; i++) {
            uint32_t st = PCIE32(OFF_MISC_PCIE_STATUS);
            if ((st & (STATUS_PHYLINKUP | STATUS_DL_ACTIVE)) ==
                    (STATUS_PHYLINKUP | STATUS_DL_ACTIVE)) {
                linked = 1;
            } else {
                pcie_udelay(100);
            }
        }
        if (!linked) return 0;
    }

    // 4. Configure MISC_CTRL: SCB_ACCESS_EN, CFG_READ_UR_MODE, 128B burst, SCB0=4GB.
    // Must run unconditionally — Pi firmware does not set SCB_ACCESS_EN (it uses its
    // own privileged path), so EXT_CFG_INDEX/DATA config reads return 0xFFFFFFFF until
    // we set this bit ourselves.
    {
        uint32_t mc = PCIE32(OFF_MISC_MISC_CTRL);
        mc |=  (1U << 12);
        mc |=  (1U << 13);
        mc &= ~(3U << 20);
        mc  = (mc & ~(0x1FU << 27)) | (0x11U << 27);
        PCIE32(OFF_MISC_MISC_CTRL) = mc;
    }

    // 5. Inbound DMA window (RC BAR2): PCIe 0x400000000 → ARM phys 0x0, 4GB.
    // Also always applied — sets up DMA address translation for xHCI ring buffers.
    {
        uint64_t bar2_off  = 0x400000000ULL;
        uint64_t bar2_size = 0x100000000ULL;
        uint32_t enc = encode_ibar_size(bar2_size);
        PCIE32(OFF_MISC_RC_BAR2_LO) = (uint32_t)((bar2_off & ~0x1FULL) & 0xFFFFFFFFU) | enc;
        PCIE32(OFF_MISC_RC_BAR2_HI) = (uint32_t)(bar2_off >> 32);
        PCIE32(OFF_MISC_RC_BAR1_LO) &= ~0x1FU;
        PCIE32(OFF_MISC_RC_BAR3_LO) &= ~0x1FU;
    }

    // 6. HARD_DEBUG: re-enable PCIe clock-request and ensure SerDes powered.
    // Pi firmware sets CLKREQ_DBG_EN (bit 0) when stopping USB; this gates the
    // endpoint ref-clock so config reads return 0xFFFFFFFF until the bit is cleared.
    // Also clear SERDES_IDDQ unconditionally (no-op when already clear).
    {
        uint32_t hd = PCIE32(OFF_MISC_HARD_DEBUG);
        hd &= ~HARD_DEBUG_CLKREQ_DBG_EN;
        hd &= ~HARD_DEBUG_SERDES_IDDQ;
        PCIE32(OFF_MISC_HARD_DEBUG) = hd;
    }

    // 9. Configure outbound MMIO window 0: CPU phys 0x600000000 → PCIe 0xF8000000, 64MB
    pcie_set_outbound_win0(CPU_WIN_BASE, PCIE_WIN_BASE, WIN_SIZE_MB);

    // 9b. Program bridge bus numbers.  Linux's brcm_pcie driver maps bus=0
    //     config accesses as (base + where), so PCI standard bridge config
    //     offset 0x18 (Primary/Secondary/Subordinate bus numbers) is at
    //     PCIE_BASE + 0x18 — NOT at PCIE_BASE + 0x043c + 0x18.
    //     Without SecBus=1, EXT_CFG generates TYPE-1 (forwarding) TLPs for
    //     bus=1; VL805 (an endpoint) returns UR → 0xFFFFFFFF.
    //     Bits: [7:0]=PriBus, [15:8]=SecBus, [23:16]=SubBus, [31:24]=SecLT.
    PCIE32(0x0018U) = 0x00010100U;   // PriBus=0, SecBus=1, SubBus=1

    // 10. Read link speed + width from LNKSTA
    {
        uint32_t lnkctl_sta = PCIE32(OFF_LNKCTL_STA);
        pcie_speed_val = (lnkctl_sta >> 16) & 0xFU;
        pcie_width_val = (lnkctl_sta >> 20) & 0x3FU;
    }

    pcie_link_ok = 1;
    return 1;
}

int          kernel_pcie_ok(void)    { return pcie_link_ok;    }
unsigned int kernel_pcie_speed(void) { return pcie_speed_val;  }
unsigned int kernel_pcie_width(void) { return pcie_width_val;  }

// ── V62: VL805 config-space probe + BAR0 assignment ───────────────────────
static int vl805_probed;

int kernel_vl805_selftest(void) {
    if (vl805_probed) return vl805_ok_val;
    vl805_probed = 1;
    vl805_ok_val = 0;

    if (!kernel_pcie_selftest()) return 0;

    // Read HW revision for diagnostics.
    vl805_hw_rev_val = PCIE32(OFF_MISC_REVISION);

    // Snapshot key RC registers for diagnostics:
    //   RGR1_SW_INIT_1: bit0=PERST#, bit1=BRIDGE_SW_INIT; both should be 0 for link-up
    //   PCIE_BASE+0x18: PCI bridge bus-number DWORD (config offset 0x18 for bus=0 RC).
    //     SecBus (byte 1) must be 1 so EXT_CFG generates TYPE-0 TLPs for bus=1.
    //     If SecBus=0, EXT_CFG generates TYPE-1 (forwarding) TLPs → VL805 returns UR.
    vl805_rgr1_val  = PCIE32(OFF_RGR1_SW_INIT_1);
    vl805_busnr_val = PCIE32(0x0018U);  // PCI bridge bus numbers (standard config offset 0x18)

    // Pi firmware already initialized VL805 (PERST# + firmware load) before
    // handing off. Do NOT assert PERST# here — that would clear VL805's firmware
    // and on boards with an empty EEPROM the chip would not recover. Instead,
    // poll the link status and read config space directly.
    //
    // Wait 500ms: Pi firmware sets HARD_DEBUG.CLKREQ_DBG_EN (bit0) when stopping
    // USB, gating the PCIe endpoint reference clock.  We cleared it in pcie_selftest
    // but VL805 needs time to re-lock its PLL before config TLPs will succeed.
    pcie_udelay(500000);
    //
    // Capture PCIE_STATUS in vl805_pcie_status_val so we can diagnose whether
    // 0xFFFFFFFF comes from a downed link or from the EXT_CFG mechanism itself.
    uint32_t viddid = 0xFFFFFFFFU;
    for (int attempt = 0; attempt < 20; attempt++) {
        uint32_t st = PCIE32(OFF_MISC_PCIE_STATUS);
        vl805_pcie_status_val = st;
        if ((st & (STATUS_PHYLINKUP | STATUS_DL_ACTIVE)) ==
                (STATUS_PHYLINKUP | STATUS_DL_ACTIVE)) {
            // Link is up: fire EXT_CFG read directly (bypass pcie_cfg_rd's
            // redundant link check to avoid hiding the real DATA result).
            PCIE32(OFF_EXT_CFG_INDEX) = (1U << 20) | (0U << 15) | (0U << 12);
            __asm__ volatile("dsb sy" ::: "memory");
            viddid = PCIE32(OFF_EXT_CFG_DATA);
            vl805_raw_viddid_val = viddid;
            if ((viddid & 0xFFFFU) == VL805_VID && ((viddid >> 16) & 0xFFFFU) == VL805_DID)
                break;
        } else {
            // Link down: store a distinct sentinel so the diagnostic line shows
            // the actual link status rather than a PCIe UR all-Fs.
            viddid = 0xFFFFFFFEU;
            vl805_raw_viddid_val = 0xFFFFFFFEU;
        }
        pcie_udelay(100000);
    }
    if ((viddid & 0xFFFFU) != VL805_VID || ((viddid >> 16) & 0xFFFFU) != VL805_DID) {
        return 0;
    }

    // BAR0 at config offset 0x10/0x14 (64-bit BAR).
    // Write ~0 to probe size (result discarded; we assign a fixed address).
    pcie_cfg_wr(1, 0, 0, 0x10, 0xFFFFFFFFU);
    pcie_cfg_wr(1, 0, 0, 0x14, 0xFFFFFFFFU);

    // Assign BAR0 = PCIe 0xF8000000 (maps to ARM phys 0x600000000 via outbound window)
    pcie_cfg_wr(1, 0, 0, 0x10, (uint32_t)(PCIE_WIN_BASE & 0xFFFFFFFFU));
    pcie_cfg_wr(1, 0, 0, 0x14, (uint32_t)(PCIE_WIN_BASE >> 32));

    // Enable Memory Space + Bus Master
    uint32_t cmd = pcie_cfg_rd(1, 0, 0, 0x04);
    pcie_cfg_wr(1, 0, 0, 0x04, cmd | 0x6U);

    vl805_ok_val = 1;
    return 1;
}

int kernel_vl805_ok(void) { return vl805_ok_val; }
unsigned int kernel_vl805_raw_viddid(void) { return vl805_raw_viddid_val; }
unsigned int kernel_vl805_hw_rev(void) { return (unsigned int)vl805_hw_rev_val; }
unsigned int kernel_vl805_pcie_status(void) { return (unsigned int)vl805_pcie_status_val; }
unsigned int kernel_vl805_rgr1(void) { return (unsigned int)vl805_rgr1_val; }
unsigned int kernel_vl805_busnr(void) { return (unsigned int)vl805_busnr_val; }

// Exposed for shell / certificate
unsigned int kernel_vl805_vendor(void) { return VL805_VID; }
unsigned int kernel_vl805_device(void) { return VL805_DID; }
