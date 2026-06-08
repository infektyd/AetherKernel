// Runtime V61: BCM2711 PCIe RC bring-up (BRCMSTB PCIe controller).
// Runtime V62: VL805 USB 3.0 controller discovery (config space + BAR0 assignment).
// Sources/Support/kernel_pcie.c
//
// BCM2711 PCIe outbound window: ARM phys 0x600000000 → PCIe bus 0xf8000000, 64MB (non-identity).
// Matches Pi4 DT ranges: <0x02000000 0x0 0xf8000000  0x6 0x00000000  0x0 0x04000000>
// VL805 BAR0 lo=0xf8000000 hi=0 (32-bit PCIe addr; 64-bit BAR with hi=0 decodes 3DW TLPs fine).
// CPU accesses xHCI MMIO at ARM 0x600000000 (Device nGnRnE in MMU).
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

// PCIe standard capability (base 0xAC in RC config space)
#define OFF_LNKCTL_STA       0x00BCU   // [31:16]=LNKSTA, [15:0]=LNKCTL
#define OFF_DEV_CTL_STA      0x00B4U   // [31:16]=DevSts, [15:0]=DevCtl
// DevSts bits (within the upper 16 bits of the dword at OFF_DEV_CTL_STA):
// bit19=URD (Unsupported Request Detected), bit18=FED, bit17=NFED, bit16=CED

// VL805 identifiers
#define VL805_VID   0x1106U
#define VL805_DID   0x3483U

// PCIe outbound window: CPU phys 0x600000000 → PCIe bus 0xf8000000, 64 MB (non-identity).
// VL805 BAR0 lo=0xf8000000, hi=0.  A 64-bit BAR with hi=0 is a 32-bit PCIe address;
// BRCMSTB RC issues ordinary 3DW TLPs which the VL805 decodes normally.
#define CPU_WIN_BASE  0x600000000ULL
#define PCIE_WIN_BASE 0xf8000000ULL    // PCIe bus addr (non-identity; DT ranges 0xf8000000)
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
static uint32_t vl805_bar0_lo_pi_val;  // BAR0 lo before our probe (Pi firmware state)
static uint32_t vl805_pm_state_val;    // PM power state at selftest time (0=D0, 3=D3hot)
static int vl805_vc_xhci_reset_val = -1;    // result of RPI_FIRMWARE_NOTIFY_XHCI_RESET mailbox call
static uint32_t vl805_vc_xhci_payload_val;  // vc_buf[5] after call (VC response: 0=success)
static uint32_t vl805_fw_ver_pre_val;        // VL805 config 0x50 BEFORE NOTIFY_XHCI_RESET (0=ROM)
static uint32_t vl805_rom_status_val;        // VL805 config 0x50 AFTER NOTIFY_XHCI_RESET
static uint32_t vl805_mmio_poll_ms_val;      // ms waited before mmio_raw0 became non-0xDEADDEAD
static uint32_t vl805_mmio_early_val;        // MMIO[0] after early probe (before NOTIFY)
static uint32_t vl805_hard_debug_pre_val;    // HARD_DEBUG before NOTIFY
static uint32_t vl805_hard_debug_post_val;   // HARD_DEBUG after NOTIFY + settle
static uint32_t vl805_dev_sts_val;           // PCIe DevSts after first dead MMIO read (URD bit=0x80000 → UR, else timeout)
static uint32_t vl805_rc_psts_val;           // RC Primary PCI Status (PCIE32(0x04)>>16): bit13=Received Master Abort (UR/CTO)
static uint32_t vl805_ext_cap0_val;          // Extended cap header at 0x100: bits[15:0]=cap ID (0x0001=AER); 0=none
static uint32_t vl805_rc_2sts_val;           // RC Bridge Secondary Status (PCIE32(0x1C)>>16): bit13=RMA from secondary
static uint32_t vl805_aer_sts_val;           // AER Uncorrectable Error Status full 32 bits: bit14=CTO, bit20=UR received
static uint32_t vl805_aer_msk_val;           // AER Uncorrectable Error Mask full 32 bits: bit14=CTO masked, bit20=UR masked
static uint32_t vl805_mmio_early_ticks_val;  // 54MHz ticks for Phase 1 MMIO read: <10=AXI, ~30-100=UR, ~2.7M=CTO(50ms)

// Pre-write snapshots of WIN0 registers (Pi firmware state)
static uint32_t pcie_win0_lo_pre_val;
static uint32_t pcie_win0_bl_pre_val;

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
    PCIE32(OFF_MISC_WIN0_LO) = (uint32_t)(pcie_addr & 0xFFFFFFFFU); // BCM2711: no size bits in WIN0_LO (mainline Linux approach)
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

    // Re-enable USB HCD power domain (device_id=3) before touching PCIe RC registers.
    // Pi firmware's XHCI_STOP calls SET_POWER_STATE(USB_HCD=3, OFF), disabling the
    // BCM2711 AXI system-bus routing for 0x600000000 → PCIe RC.  ARM MMIO reads at
    // VL805 BAR0 return 0xDEADDEAD (AXI fabric error fill) until this domain is
    // re-enabled — WIN0 and MISC_CTRL programming alone cannot fix it.
    // state=3: power ON + wait for transition to complete.
    kernel_vc_mbox_set_power_state(3U, 3U);
    pcie_udelay(100000);  // 100ms for power domain to stabilize

    // Snapshot Pi firmware register state before we reset anything.
    pcie_win0_lo_pre_val = PCIE32(OFF_MISC_WIN0_LO);
    pcie_win0_bl_pre_val = PCIE32(OFF_MISC_WIN0_BL);

    // Full PERST# + bridge-SW-reset bring-up, matching Linux pcie-brcmstb.c ordering:
    //   MISC_CTRL, WIN0, BAR2, HARD_DEBUG are configured AFTER bridge-reset deassertion
    //   but BEFORE PERST# deassertion.
    //
    // Previous attempts set WIN0/MISC_CTRL after link-up (Fixes 1 & 2). Both left
    // MMIO returning 0xDEADDEAD.  The BCM2711 AXI fabric's ARM→PCIe routing appears
    // to latch SCB_ACCESS_EN / outbound-window state at PERST# deassertion time;
    // writing those registers after the fact has no effect.

    // 1. Assert PERST# + bridge SW reset simultaneously.
    PCIE32(OFF_RGR1_SW_INIT_1) |= (RGR1_PERST | RGR1_BRIDGE_SW_INIT);
    pcie_udelay(100);

    // 2. Deassert bridge SW reset (keep PERST# asserted).
    //    BCM2711 PCIe RC MISC registers now reset to defaults (WIN0_LO=0, etc.).
    PCIE32(OFF_RGR1_SW_INIT_1) &= ~RGR1_BRIDGE_SW_INIT;
    pcie_udelay(100);

    // 3. Power up SerDes (clear IDDQ).
    PCIE32(OFF_MISC_HARD_DEBUG) &= ~HARD_DEBUG_SERDES_IDDQ;
    pcie_udelay(100);

    // 4. Mask MSI interrupts.
    PCIE32(OFF_MSI_MASK_SET) = 0xFFFFFFFFU;
    PCIE32(OFF_MSI_CLR)      = 0xFFFFFFFFU;

    // 5. MISC_CTRL: SCB_ACCESS_EN, CFG_READ_UR_MODE, 128B burst, SCB0=4GB.
    //    MUST be set before PERST# deassertion — matches Linux ordering.
    {
        uint32_t mc = PCIE32(OFF_MISC_MISC_CTRL);
        mc |=  (1U << 12);   // SCB_ACCESS_EN
        mc |=  (1U << 13);   // CFG_READ_UR_MODE
        mc |=  (1U << 20);   // MAX_BURST_SIZE 128B
        mc  = (mc & ~(0x1FU << 27)) | (0x11U << 27);  // SCB0_SIZE = 4GB
        PCIE32(OFF_MISC_MISC_CTRL) = mc;
    }

    // 6. Inbound DMA window (RC BAR2): PCIe 0x400000000 → ARM phys 0x0, 4GB.
    {
        uint64_t bar2_off  = 0x400000000ULL;
        uint64_t bar2_size = 0x100000000ULL;
        uint32_t enc = encode_ibar_size(bar2_size);
        PCIE32(OFF_MISC_RC_BAR2_LO) = (uint32_t)((bar2_off & ~0x1FULL) & 0xFFFFFFFFU) | enc;
        PCIE32(OFF_MISC_RC_BAR2_HI) = (uint32_t)(bar2_off >> 32);
        PCIE32(OFF_MISC_RC_BAR1_LO) &= ~0x1FU;
        PCIE32(OFF_MISC_RC_BAR3_LO) &= ~0x1FU;
    }

    // 7. HARD_DEBUG: clear CLKREQ_DBG_EN (gates endpoint ref-clock), SERDES_IDDQ,
    //    and L1SS_ENA (L1 sub-states can stall MMIO TLPs while MCU boots).
    {
        uint32_t hd = PCIE32(OFF_MISC_HARD_DEBUG);
        hd &= ~HARD_DEBUG_CLKREQ_DBG_EN;
        hd &= ~HARD_DEBUG_SERDES_IDDQ;
        hd &= ~(1U << 21);   // L1SS_ENA
        PCIE32(OFF_MISC_HARD_DEBUG) = hd;
    }

    // 8. Outbound MMIO window 0: CPU phys 0x600000000 → PCIe 0xf8000000, 64MB.
    //    Set BEFORE PERST# deassertion — matches Linux ordering.
    pcie_set_outbound_win0(CPU_WIN_BASE, PCIE_WIN_BASE, WIN_SIZE_MB);
    __asm__ volatile("dsb sy" ::: "memory");

    // 9. Bridge bus numbers: SecBus=1 ensures EXT_CFG generates TYPE-0 TLPs for
    //    bus=1 (endpoint); without this, VL805 returns UR → 0xFFFFFFFF.
    PCIE32(0x0018U) = 0x00010100U;

    // 10. Deassert PERST# — VL805 starts EEPROM firmware load + link training.
    PCIE32(OFF_RGR1_SW_INIT_1) &= ~RGR1_PERST;
    pcie_udelay(120000);

    // 11. Poll for link-up, up to 100ms.
    {
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

    // 12. Read link speed + width from LNKSTA.
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

// Live register readbacks for diagnostics
unsigned int kernel_pcie_win0_lo(void)  { return (unsigned int)PCIE32(OFF_MISC_WIN0_LO);  }
unsigned int kernel_pcie_win0_hi(void)  { return (unsigned int)PCIE32(OFF_MISC_WIN0_HI);  }
unsigned int kernel_pcie_win0_bl(void)  { return (unsigned int)PCIE32(OFF_MISC_WIN0_BL);  }
unsigned int kernel_pcie_win0_bhi(void) { return (unsigned int)PCIE32(OFF_MISC_WIN0_BHI); }
unsigned int kernel_pcie_win0_lhi(void) { return (unsigned int)PCIE32(OFF_MISC_WIN0_LHI); }
unsigned int kernel_pcie_misc_ctrl(void){ return (unsigned int)PCIE32(OFF_MISC_MISC_CTRL); }
unsigned int kernel_pcie_status(void)   { return (unsigned int)PCIE32(OFF_MISC_PCIE_STATUS); }
unsigned int kernel_pcie_win0_lo_pre(void) { return (unsigned int)pcie_win0_lo_pre_val; }
unsigned int kernel_pcie_win0_bl_pre(void) { return (unsigned int)pcie_win0_bl_pre_val; }

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

    // Force D0 power state (pm_state confirmed 0 in practice, but be defensive).
    {
        uint32_t cmd_st = pcie_cfg_rd(1, 0, 0, 0x04);
        if ((cmd_st >> 16) & (1U << 4)) {
            unsigned int caps = pcie_cfg_rd(1, 0, 0, 0x34) & 0xFCU;
            for (int walk = 0; walk < 16 && caps != 0 && caps < 0x100U; walk++) {
                uint32_t cap = pcie_cfg_rd(1, 0, 0, caps);
                if ((cap & 0xFFU) == 0x01U) {
                    uint32_t pmcsr = pcie_cfg_rd(1, 0, 0, caps + 4U);
                    vl805_pm_state_val = pmcsr & 0x3U;
                    if (vl805_pm_state_val != 0U) {
                        pcie_cfg_wr(1, 0, 0, caps + 4U, pmcsr & ~0x3U);
                        pcie_udelay(10000);
                    }
                    break;
                }
                caps = (cap >> 8) & 0xFCU;
            }
        }
    }

    // Capture BAR0 BEFORE our assignment (reveals Pi firmware's state).
    vl805_bar0_lo_pi_val = pcie_cfg_rd(1, 0, 0, 0x10);
    vl805_fw_ver_pre_val = pcie_cfg_rd(1, 0, 0, 0x50);

    // ── Phase 1: assign BAR0 BEFORE NOTIFY, probe MMIO immediately.
    // Pi firmware's XHCI-STOP clears BAR0 but does NOT PERST# the VL805
    // endpoint — the MCU firmware is still running.  Re-assigning BAR0
    // and enabling MemEnable should be enough to read xHCI registers
    // without any NOTIFY/PERST# cycle at all.
    vl805_hard_debug_pre_val = PCIE32(OFF_MISC_HARD_DEBUG);
    pcie_cfg_wr(1, 0, 0, 0x10, (uint32_t)(PCIE_WIN_BASE & 0xFFFFFFFFU));
    pcie_cfg_wr(1, 0, 0, 0x14, (uint32_t)(PCIE_WIN_BASE >> 32));
    {
        uint32_t cmd = pcie_cfg_rd(1, 0, 0, 0x04);
        pcie_cfg_wr(1, 0, 0, 0x04, cmd | 0x6U);
    }
    pcie_udelay(2000000);   // 2s: wait for VL805 to accept memory TLPs after BAR0 restore

    __asm__ volatile("dsb sy" ::: "memory");
    {
        uint64_t _t0, _t1;
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(_t0));
        vl805_mmio_early_val = *(volatile uint32_t *)VL805_MMIO_ARM_PHYS;
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(_t1));
        // Store raw 54 MHz counter ticks: AXI-return ~1-5 ticks, PCIe-UR ~30-100 ticks,
        // PCIe-CTO ~2,700,000 ticks (50ms).  Capped at 0xFFFFFFFF for the uint32 field.
        uint64_t _dt = _t1 - _t0;
        vl805_mmio_early_ticks_val = (uint32_t)(_dt > 0xFFFFFFFFU ? 0xFFFFFFFFU : _dt);
    }

    // Capture DevSts + AER Uncorrectable Error Status.
    // DevSts bit19=URD: UR completion received.
    // AER (PCIe extended cap at 0x100) offset 0x04 = Uncorrectable Error Status:
    //   bit14 = Completion Timeout → TLP was sent but no response came back.
    //   If bit14=0 AND DevSts=0: no TLP was generated (window miss / not routed to PCIe).
    vl805_dev_sts_val  = (PCIE32(OFF_DEV_CTL_STA) >> 16) | (PCIE32(0x104U) << 16);
    // RC error status registers for UR vs CTO classification.
    // rc_psts bit13 = Received Master Abort (UR or CTO from outbound TLP).
    // ext_cap0[15:0] = extended cap ID at 0x100; must be 0x0001 for AER or 0x104 is not AER.
    // rc_2sts bit13 = same on bridge secondary side.
    vl805_rc_psts_val  = PCIE32(0x04U) >> 16;
    vl805_ext_cap0_val = PCIE32(0x100U);
    vl805_rc_2sts_val  = PCIE32(0x1CU) >> 16;
    // AER Uncorrectable Error Status: bit14=CTO, bit20=UR from downstream.
    // The packed dev_sts field only captures bits[15:0] of this register due to
    // uint32_t overflow in the shift; capture the full value here.
    vl805_aer_sts_val  = PCIE32(0x104U);
    vl805_aer_msk_val  = PCIE32(0x108U);

    if (vl805_mmio_early_val != 0xDEADDEADU) {
        // VL805 MCU was alive post-handoff; no NOTIFY needed.
        vl805_mmio_poll_ms_val = 0U;
        vl805_hard_debug_post_val = vl805_hard_debug_pre_val;
        vl805_vc_xhci_reset_val  = -2;  // skipped
        vl805_ok_val = 1;
        return 1;
    }

    // ── Phase 2: NOTIFY_XHCI_RESET (Pi firmware reloads VL805 MCU firmware).
    vl805_vc_xhci_reset_val = kernel_vc_mbox_notify_xhci_reset();

    // Wait 3s for MCU reload + PLL re-lock.
    pcie_udelay(3000000);

    // Re-clear CLKREQ_DBG_EN, SERDES_IDDQ, L1SS_ENA.
    {
        uint32_t hd = PCIE32(OFF_MISC_HARD_DEBUG);
        hd &= ~HARD_DEBUG_CLKREQ_DBG_EN;
        hd &= ~HARD_DEBUG_SERDES_IDDQ;
        hd &= ~(1U << 21);
        PCIE32(OFF_MISC_HARD_DEBUG) = hd;
        vl805_hard_debug_post_val = hd;
    }
    pcie_udelay(10000);

    // Re-assign BAR0 after NOTIFY's PERST# cycle.
    pcie_cfg_wr(1, 0, 0, 0x10, (uint32_t)(PCIE_WIN_BASE & 0xFFFFFFFFU));
    pcie_cfg_wr(1, 0, 0, 0x14, (uint32_t)(PCIE_WIN_BASE >> 32));
    {
        uint32_t cmd = pcie_cfg_rd(1, 0, 0, 0x04);
        pcie_cfg_wr(1, 0, 0, 0x04, cmd | 0x6U);
    }
    pcie_udelay(100000);

    vl805_vc_xhci_payload_val = kernel_vc_mbox_xhci_reset_payload();
    vl805_rom_status_val = pcie_cfg_rd(1, 0, 0, 0x50);

    // Poll MMIO for up to 3s.
    for (unsigned int poll_ms = 0; poll_ms < 3000U; poll_ms += 10U) {
        __asm__ volatile("dsb sy" ::: "memory");
        if (*(volatile uint32_t *)VL805_MMIO_ARM_PHYS != 0xDEADDEADU) {
            vl805_mmio_poll_ms_val = poll_ms;
            vl805_ok_val = 1;
            return 1;
        }
        pcie_udelay(10000);
    }
    vl805_mmio_poll_ms_val = 0xFFFFU;

    // ── Phase 3: direct PERST# cycle + wait for EEPROM MCU auto-load.
    // NOTIFY failed or Pi firmware no longer handles it in this firmware version.
    // Assert PERST# ourselves: VL805 boots from EEPROM (if present) on deassertion.
    PCIE32(OFF_RGR1_SW_INIT_1) |= RGR1_PERST;
    pcie_udelay(200000);    // 200ms in reset

    PCIE32(OFF_RGR1_SW_INIT_1) &= ~RGR1_PERST;

    // Wait for link re-train (standard PCIe: up to 120ms).
    pcie_udelay(150000);
    for (int li = 0; li < 100; li++) {
        uint32_t st = PCIE32(OFF_MISC_PCIE_STATUS);
        if ((st & (STATUS_PHYLINKUP | STATUS_DL_ACTIVE)) ==
                (STATUS_PHYLINKUP | STATUS_DL_ACTIVE))
            break;
        pcie_udelay(10000);
    }

    // Wait 3s for VL805 EEPROM firmware load + MCU startup.
    pcie_udelay(3000000);

    // Re-assign BAR0 (PERST# cleared it).
    pcie_cfg_wr(1, 0, 0, 0x10, (uint32_t)(PCIE_WIN_BASE & 0xFFFFFFFFU));
    pcie_cfg_wr(1, 0, 0, 0x14, (uint32_t)(PCIE_WIN_BASE >> 32));
    {
        uint32_t cmd = pcie_cfg_rd(1, 0, 0, 0x04);
        pcie_cfg_wr(1, 0, 0, 0x04, cmd | 0x6U);
    }
    pcie_udelay(500000);

    // Poll MMIO for up to 2s.
    for (unsigned int poll3 = 0; poll3 < 2000U; poll3 += 10U) {
        __asm__ volatile("dsb sy" ::: "memory");
        if (*(volatile uint32_t *)VL805_MMIO_ARM_PHYS != 0xDEADDEADU) {
            vl805_mmio_poll_ms_val = poll3 + 20000U;  // +20000 flags Phase 3
            vl805_ok_val = 1;
            return 1;
        }
        pcie_udelay(10000);
    }
    vl805_mmio_poll_ms_val = 0xEEEEU;  // Phase 3 timeout sentinel — all phases failed
    vl805_ok_val = 0;
    return 0;
}

unsigned int kernel_vl805_bar0_lo_pi(void)         { return (unsigned int)vl805_bar0_lo_pi_val;       }
unsigned int kernel_vl805_pm_state(void)           { return (unsigned int)vl805_pm_state_val;         }
int          kernel_vl805_vc_xhci_reset(void)      { return vl805_vc_xhci_reset_val;                  }
unsigned int kernel_vl805_vc_xhci_payload(void)    { return (unsigned int)vl805_vc_xhci_payload_val;  }
unsigned int kernel_vl805_fw_ver_pre(void)         { return (unsigned int)vl805_fw_ver_pre_val;       }
unsigned int kernel_vl805_rom_status(void)         { return (unsigned int)vl805_rom_status_val;       }
unsigned int kernel_vl805_mmio_poll_ms(void)       { return (unsigned int)vl805_mmio_poll_ms_val;     }
unsigned int kernel_vl805_mmio_early(void)         { return (unsigned int)vl805_mmio_early_val;       }

int kernel_vl805_ok(void) { return vl805_ok_val; }
unsigned int kernel_vl805_raw_viddid(void) { return vl805_raw_viddid_val; }
unsigned int kernel_vl805_hw_rev(void) { return (unsigned int)vl805_hw_rev_val; }
unsigned int kernel_vl805_pcie_status(void) { return (unsigned int)vl805_pcie_status_val; }
unsigned int kernel_vl805_rgr1(void) { return (unsigned int)vl805_rgr1_val; }
unsigned int kernel_vl805_busnr(void)           { return (unsigned int)vl805_busnr_val;           }
unsigned int kernel_vl805_hard_debug_pre(void)  { return (unsigned int)vl805_hard_debug_pre_val;  }
unsigned int kernel_vl805_hard_debug_post(void) { return (unsigned int)vl805_hard_debug_post_val; }
unsigned int kernel_vl805_dev_sts(void)         { return (unsigned int)vl805_dev_sts_val;          }
unsigned int kernel_vl805_rc_psts(void)         { return (unsigned int)vl805_rc_psts_val;          }
unsigned int kernel_vl805_ext_cap0(void)        { return (unsigned int)vl805_ext_cap0_val;         }
unsigned int kernel_vl805_rc_2sts(void)         { return (unsigned int)vl805_rc_2sts_val;          }
unsigned int kernel_vl805_aer_sts(void)         { return (unsigned int)vl805_aer_sts_val;          }
unsigned int kernel_vl805_aer_msk(void)         { return (unsigned int)vl805_aer_msk_val;          }
unsigned int kernel_vl805_mmio_early_ticks(void){ return (unsigned int)vl805_mmio_early_ticks_val; }

// Exposed for shell / certificate
unsigned int kernel_vl805_vendor(void) { return VL805_VID; }
unsigned int kernel_vl805_device(void) { return VL805_DID; }

// Live config-space readbacks for diagnostics (call only after selftest has run).
unsigned int kernel_vl805_bar0_lo(void) { return pcie_cfg_rd(1, 0, 0, 0x10); }
unsigned int kernel_vl805_bar0_hi(void) { return pcie_cfg_rd(1, 0, 0, 0x14); }
unsigned int kernel_vl805_cmd_reg(void) { return pcie_cfg_rd(1, 0, 0, 0x04); }
// Raw MMIO reads at XHCI_BASE (phys 0x600000000) — diagnostics for outbound window.
unsigned int kernel_vl805_mmio_raw0(void) {
    __asm__ volatile("dsb sy" ::: "memory");
    return *(volatile unsigned int *)0x600000000UL;
}
unsigned int kernel_vl805_mmio_raw4(void) {
    __asm__ volatile("dsb sy" ::: "memory");
    return *(volatile unsigned int *)0x600000004UL;
}
