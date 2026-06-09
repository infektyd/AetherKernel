// Runtime V63: xHCI capability register probe (CAPLENGTH, HCIVERSION, port count).
// Runtime V64: xHCI controller init (DCBAA + command/event rings + RUN + port-connect detect).
// Runtime V65: USB device enumeration (port reset, ENABLE_SLOT, ADDRESS_DEVICE, GET_DESCRIPTOR).
// Runtime V66: HID boot-protocol keyboard (interrupt-IN poll + keypress decode).
// Sources/Support/kernel_xhci.c
//
// VL805 xHCI MMIO base: ARM phys 0x600000000 (PCIe bus 0xF8000000).
// DMA address translation: PCIe bus addr = ARM phys + 0xC0000000 (BCM2711 inbound BAR2,
// 32-bit window; PCIe 0xC0000000 → ARM 0x0, 1GB — matches Pi firmware default).

#include "include/Support.h"
#include <stdint.h>

// ── Inline UART diagnostics for USB enumeration ───────────────────────────
// Direct PL011 MMIO – same approach as executor.c.
#define XHCI_D_UART 0xFE201000UL
static void xhci_d_putc(char c) {
    while (*(volatile uint32_t *)(XHCI_D_UART + 0x18UL) & (1U << 5)) {}
    *(volatile uint32_t *)(XHCI_D_UART) = (uint32_t)(uint8_t)c;
}
static void xhci_d_puts(const char *s) {
    while (*s) { if (*s == '\n') xhci_d_putc('\r'); xhci_d_putc(*s++); }
}
static void xhci_d_hex(uint32_t v) {
    const char h[] = "0123456789abcdef";
    xhci_d_putc('0'); xhci_d_putc('x');
    for (int i = 28; i >= 0; i -= 4) xhci_d_putc(h[(v >> i) & 0xfU]);
}
static void xhci_d_dec(unsigned int v) {
    if (v >= 10) xhci_d_dec(v / 10);
    xhci_d_putc('0' + (v % 10));
}

// ── xHCI MMIO ─────────────────────────────────────────────────────────────
// xHCI spec §4.2.1: 64-bit registers MUST be accessed as two 32-bit stores
// (low DWORD first). A single 64-bit STR is dropped by VL805/many controllers.
#define XHCI_BASE 0x600000000UL
#define XR32(off)  (*(volatile uint32_t *)(XHCI_BASE + (unsigned long)(off)))
#define XW32(off, v) do { (*(volatile uint32_t *)(XHCI_BASE + (unsigned long)(off))) = (v); } while(0)
#define XR64(off) \
    ((uint64_t)(XR32((unsigned long)(off))) | ((uint64_t)(XR32((unsigned long)(off) + 4UL)) << 32))
#define XW64(off, v) do { \
    XW32((unsigned long)(off),        (uint32_t)((uint64_t)(v) & 0xFFFFFFFFU)); \
    XW32((unsigned long)(off) + 4UL,  (uint32_t)(((uint64_t)(v) >> 32) & 0xFFFFFFFFU)); \
} while(0)

// DMA: ARM phys → PCIe bus address (what the VL805 DMA engine uses).
// 64-bit window: PCIe 0x400000000 → ARM 0x0, 4GB. BAR2_HI=0x4, BAR2_LO=enc.
#define DMA_TO_BUS(p) ((uint64_t)(unsigned long)(p) + 0x400000000ULL)

// ── xHCI timing ──────────────────────────────────────────────────────────
static void xhci_udelay(unsigned int us) {
    uint64_t freq, start, now;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(start));
    uint64_t ticks = (uint64_t)us * freq / 1000000ULL;
    do { __asm__ volatile("mrs %0, cntpct_el0" : "=r"(now)); } while (now - start < ticks);
}

// ── V63 state (capability probe) ─────────────────────────────────────────
static int xhci_cap_ok_val;
static unsigned int xhci_hciversion_val;
static unsigned int xhci_max_ports_val;
static unsigned int xhci_max_slots_val;
static unsigned int xhci_max_scratch_val;
static unsigned int xhci_caplength_val;
static unsigned int xhci_csz_val;   // 0=32B contexts, 1=64B contexts
static unsigned int xhci_dboff_val;
static unsigned int xhci_rtsoff_val;

static int xhci_cap_probed;

int kernel_xhci_selftest(void) {
    if (xhci_cap_probed) return xhci_cap_ok_val;
    xhci_cap_probed = 1;
    xhci_cap_ok_val = 0;

    if (!kernel_vl805_ok()) return 0;

    // HCCAPBASE: [7:0]=CAPLENGTH, [31:16]=HCIVERSION
    uint32_t hccapbase    = XR32(0x00);
    xhci_caplength_val    = hccapbase & 0xFFU;
    xhci_hciversion_val   = (hccapbase >> 16) & 0xFFFFU;

    // HCSPARAMS1: [7:0]=MaxSlots, [18:8]=MaxIntrs, [31:24]=MaxPorts
    uint32_t hcs1         = XR32(0x04);
    xhci_max_slots_val    = hcs1 & 0xFFU;
    xhci_max_ports_val    = (hcs1 >> 24) & 0xFFU;

    // HCSPARAMS2: MaxScratchpadBufs = ((bits[31:27]) | ((bits[25:21]) << 5))
    uint32_t hcs2         = XR32(0x08);
    xhci_max_scratch_val  = ((hcs2 >> 27) & 0x1FU) | (((hcs2 >> 21) & 0x1FU) << 5);

    // HCCPARAMS1: [0]=AC64, [2]=CSZ
    uint32_t hcc1         = XR32(0x10);
    xhci_csz_val          = (hcc1 >> 2) & 0x1U;
    xhci_d_puts("v63:hcc1="); xhci_d_hex(hcc1); xhci_d_puts("\n");

    // DBOFF and RTSOFF
    xhci_dboff_val        = XR32(0x14);
    xhci_rtsoff_val       = XR32(0x18);

    if (hccapbase == 0xDEADDEADU || xhci_hciversion_val == 0 || xhci_max_ports_val == 0) return 0;

    xhci_cap_ok_val = 1;
    return 1;
}

int          kernel_xhci_ok(void)          { return xhci_cap_ok_val;      }
unsigned int kernel_xhci_hciversion(void)  { return xhci_hciversion_val;  }
unsigned int kernel_xhci_ports(void)       { return xhci_max_ports_val;   }
unsigned int kernel_xhci_slots(void)       { return xhci_max_slots_val;   }
unsigned int kernel_xhci_scratch(void)     { return xhci_max_scratch_val; }

// ── V64: xHCI controller init ─────────────────────────────────────────────
// Data structures (allocated from kernel frame allocator)
#define XHCI_CMD_RING_TRBS  256U    // 256 * 16 = 4096 bytes
#define XHCI_EVT_RING_TRBS  256U
#define XHCI_CTX_SIZE       32U     // CSZ=0 → 32-byte contexts

// TRB structure (16 bytes)
typedef struct {
    uint32_t param_lo;
    uint32_t param_hi;
    uint32_t status;
    uint32_t control;
} xhci_trb_t;

// ERST entry (16 bytes)
typedef struct {
    uint32_t base_lo;
    uint32_t base_hi;
    uint32_t size;
    uint32_t rsvd;
} xhci_erst_t;

// Device Context (32-byte slot context + up to 31 endpoint contexts = 32 * 32 = 1024B max)
// Slot context is at offset 0, Endpoint 0 context at offset 32.
typedef struct {
    uint32_t w[8];  // 32 bytes per context
} xhci_ctx32_t;

static int xhci_run_ok_val;
static unsigned int xhci_ports_connected_val;

// Pointers to allocated structures
static xhci_trb_t   *cmd_ring;
static xhci_trb_t   *evt_ring;
static xhci_erst_t  *erst;
static uint64_t     *dcbaa;         // DCBAA: (MaxSlots+1) * 8 bytes
static uint64_t     *scratch_array; // scratchpad buffer array
static unsigned int  cmd_enq;       // command ring enqueue index
static unsigned int  cmd_cycle;     // command ring cycle bit
static unsigned int  evt_deq;       // event ring dequeue index
static unsigned int  evt_cycle;     // event ring cycle bit

// NC DMA buffer allocator.
// All xHCI ring buffers (event ring, cmd ring, DCBAA, ERST, etc.) are shared with VL805
// via PCIe DMA which is NOT cache-coherent with ARM L1/L2. Mapping them as Normal
// Non-Cacheable (MAIR index 2 = 0x44) means CPU reads bypass the cache hierarchy and
// go directly to DRAM, where the VL805 DMA writes land. No dc ivac/cvac needed.
#define XHCI_NC_VBASE 0x200000000UL  // L1[8] — currently unmapped, within TTBR0 39-bit space
static unsigned int xhci_nc_slot;

// Allocates one 4KB frame, flushes WB alias, NC-maps it at XHCI_NC_VBASE+slot*4096,
// zeros it via NC VA (direct to DRAM). Returns PA for DMA_TO_BUS; *nc_out = NC VA.
static unsigned long alloc_dma(void **nc_out) {
    unsigned long pa = kernel_frame_alloc();
    if (pa == 0) { if (nc_out) *nc_out = 0; return 0; }
    // Flush & invalidate WB alias: prevents stale dirty CPU-side data from the identity
    // mapping from aliasing over our NC writes or HC DMA writes.
    for (unsigned long off = 0; off < 4096UL; off += 64UL)
        __asm__ volatile("dc civac, %0" :: "r"(pa + off) : "memory");
    __asm__ volatile("dsb sy" ::: "memory");
    unsigned long nc_va = XHCI_NC_VBASE + (unsigned long)xhci_nc_slot * 4096UL;
    xhci_nc_slot++;
    kernel_vmm_map_4k_nc(nc_va, pa);
    // Zero via NC VA — writes bypass cache and go directly to DRAM.
    volatile uint64_t *p = (volatile uint64_t *)nc_va;
    for (unsigned i = 0; i < 512U; i++) p[i] = 0ULL;
    __asm__ volatile("dsb sy" ::: "memory");
    if (nc_out) *nc_out = (void *)nc_va;
    return pa;
}

static unsigned long evt_pa_g;   // physical address of event ring (for ERDP DMA calc)

static int xhci_run_probed;

// PORTSC register for port n (1-based)
static inline unsigned long portsc_offset(unsigned int n) {
    return (unsigned long)xhci_caplength_val + 0x400UL + (unsigned long)(n - 1U) * 0x10UL;
}

// Write to command ring and ring HC doorbell 0
static void xhci_ring_cmd_doorbell(void) {
    // Host Controller doorbell at DBOFF + 0
    XW32(xhci_dboff_val + 0, 0);
    __asm__ volatile("dsb sy" ::: "memory");
}

// Last command completion code — set by xhci_wait_cmd_completion before returning
static uint32_t xhci_last_cc;

// Poll for a Command Completion Event; return slot_id from event or 0xFF on timeout
// Sets xhci_last_cc to the completion code of the CCE consumed.
static uint32_t xhci_wait_cmd_completion(void) {
    for (int i = 0; i < 100000; i++) {
        // NC memory: CPU reads go directly to DRAM — no dc ivac needed.
        __asm__ volatile("dsb sy" ::: "memory");
        xhci_trb_t *trb = &evt_ring[evt_deq];
        uint32_t ctrl = trb->control;
        if ((ctrl & 0x1U) == evt_cycle) {
            unsigned int trb_type = (ctrl >> 10) & 0x3FU;
            uint32_t completion_code = (trb->status >> 24) & 0xFFU;
            uint32_t slot_id = (ctrl >> 24) & 0xFFU;
            xhci_d_puts("v65:evt type="); xhci_d_dec(trb_type);
            xhci_d_puts(" slot="); xhci_d_dec(slot_id);
            xhci_d_puts(" cc="); xhci_d_dec(completion_code);
            xhci_d_puts(" ctrl="); xhci_d_hex(ctrl); xhci_d_puts("\n");
            evt_deq++;
            if (evt_deq >= XHCI_EVT_RING_TRBS) {
                evt_deq = 0;
                evt_cycle ^= 1U;
            }
            // ERDP update: use PA (evt_pa_g) not NC VA for DMA address.
            uint64_t erdp = DMA_TO_BUS(evt_pa_g + (uint64_t)evt_deq * 16UL);
            uint32_t iman_off = xhci_rtsoff_val + 0x20U;
            XW64(iman_off + 0x18, erdp);
            __asm__ volatile("dsb sy" ::: "memory");
            if (trb_type == 33U) {
                xhci_last_cc = completion_code;
                return slot_id;
            }
            i = 0;
        }
        xhci_udelay(10);
    }
    xhci_last_cc = 0xFFU;
    return 0xFFU;
}

int kernel_xhci_run_selftest(void) {
    if (xhci_run_probed) return xhci_run_ok_val;
    xhci_run_probed = 1;
    xhci_run_ok_val = 0;

    if (!kernel_xhci_selftest()) return 0;

    unsigned long op_base = (unsigned long)xhci_caplength_val;  // relative to XHCI_BASE

    xhci_d_puts("v64:dboff="); xhci_d_hex(xhci_dboff_val);
    xhci_d_puts(" rtsoff="); xhci_d_hex(xhci_rtsoff_val); xhci_d_puts("\n");

    // 1. Confirm HC is halted (USBSTS.HCH=1), then reset
    {
        uint32_t sts = XR32(op_base + 0x04);
        xhci_d_puts("v64:sts_pre="); xhci_d_hex(sts); xhci_d_puts("\n");
        if (!(sts & (1U << 0))) {
            // HC is running; stop it first
            uint32_t cmd = XR32(op_base + 0x00);
            XW32(op_base + 0x00, cmd & ~0x1U);
            for (int i = 0; i < 100 && !(XR32(op_base + 0x04) & 1U); i++) {
                xhci_udelay(1000);
            }
        }
    }
    // HCRST: resets the xHCI controller's digital state (rings, interrupts, config registers).
    // NOTE: HCRST does NOT reset VL805 EEPROM firmware's USB2 port link state — port
    // remains in whatever state VL805 firmware left it (Polling / U0 / etc). The USB2 PHY
    // is controlled entirely by VL805 firmware, independently of xHCI HCRST.
    {
        uint32_t cmd = XR32(op_base + 0x00);
        XW32(op_base + 0x00, cmd | (1U << 1));  // HCRST=1
        // xHCI spec §4.2.1: HCRST self-clears when reset is complete (up to 1s)
        int rst_ok = 0;
        for (int i = 0; i < 1000; i++) {
            xhci_udelay(1000);
            if (!(XR32(op_base + 0x00) & (1U << 1))) { rst_ok = 1; break; }
        }
        xhci_d_puts("v64:hcrst_ok="); xhci_d_dec((unsigned int)rst_ok); xhci_d_puts("\n");
        if (!rst_ok) { xhci_d_puts("v64:HCRST_TIMEOUT\n"); return 0; }
    }

    // xHCI spec: wait for CNR=0 (Controller Not Ready) before accessing operational regs
    for (int i = 0; i < 500; i++) {
        if (!(XR32(op_base + 0x04) & (1U << 11))) break;
        xhci_udelay(1000);
    }
    {
        uint32_t sts = XR32(op_base + 0x04);
        xhci_d_puts("v64:sts_rst="); xhci_d_hex(sts); xhci_d_puts("\n");
        if (sts & (1U << 11)) { xhci_d_puts("v64:CNR_TIMEOUT\n"); return 0; }
    }

    // PP=0 while HC HALTED (RS=0): must happen before RS=1.
    // Pi VC's VPU issues xhci_set_port_power(0) on all ports with xHCI stopped, holds for
    // ~1s, then starts the HC and sets PP=1. When PP=0 is written in halted state, VL805
    // EEPROM firmware treats it as a full USB2 PHY power-down (chirp state cleared).
    // When PP=0 is written while HC is running (our previous approach), VL805 firmware
    // only power-cycles VBUS and preserves its internal USB2 state — port returns to
    // Polling (HS chirp looping) immediately on PP=1 restore.
    {
        // PED (bit1) is RW1CS: writing 1 CLEARS it (disables port). Must write 0 to preserve.
        static const uint32_t rw1cs_h =
            (1U<<1)|(1U<<17)|(1U<<18)|(1U<<19)|(1U<<20)|(1U<<21)|(1U<<22)|(1U<<23);
        xhci_d_puts("v64:pp_pre="); xhci_d_hex(XR32(portsc_offset(1))); xhci_d_puts("\n");
        for (unsigned int n = 1; n <= xhci_max_ports_val; n++) {
            unsigned long off = portsc_offset(n);
            uint32_t v = XR32(off); v &= ~rw1cs_h; v &= ~(1U << 9);
            XW32(off, v);
        }
        xhci_d_puts("v64:pp0="); xhci_d_hex(XR32(portsc_offset(1))); xhci_d_puts("\n");
        xhci_udelay(1000000);  // 1s PP=0 hold while HC is HALTED
        xhci_d_puts("v64:pp0_done="); xhci_d_hex(XR32(portsc_offset(1))); xhci_d_puts("\n");
    }

    // 2. Allocate DCBAA as NC DMA page
    void *dcbaa_nc;
    unsigned long dcbaa_pa = alloc_dma(&dcbaa_nc);
    if (dcbaa_pa == 0) return 0;
    dcbaa = (uint64_t *)dcbaa_nc;
    // alloc_dma already zeroed the page; entries stay 0 until slots are assigned

    // 3. Allocate scratchpad array + pages as NC DMA pages
    if (xhci_max_scratch_val > 0) {
        void *sarr_nc;
        unsigned long scratch_arr_pa = alloc_dma(&sarr_nc);
        if (scratch_arr_pa == 0) return 0;
        scratch_array = (uint64_t *)sarr_nc;
        for (unsigned int i = 0; i < xhci_max_scratch_val; i++) {
            unsigned long page_pa = alloc_dma((void **)0);  // HC manages content; no CPU VA needed
            if (page_pa == 0) return 0;
            scratch_array[i] = DMA_TO_BUS(page_pa);
        }
        __asm__ volatile("dsb sy" ::: "memory");
        dcbaa[0] = DMA_TO_BUS(scratch_arr_pa);
    }

    // 4. Command ring — NC DMA page
    void *cmd_nc;
    unsigned long cmd_pa = alloc_dma(&cmd_nc);
    if (cmd_pa == 0) return 0;
    cmd_ring  = (xhci_trb_t *)cmd_nc;
    cmd_enq   = 0;
    cmd_cycle = 1;
    // alloc_dma zeroed the ring; set Link TRB at last slot
    cmd_ring[XHCI_CMD_RING_TRBS - 1].param_lo = (uint32_t)(DMA_TO_BUS(cmd_pa) & 0xFFFFFFFFU);
    cmd_ring[XHCI_CMD_RING_TRBS - 1].param_hi = (uint32_t)(DMA_TO_BUS(cmd_pa) >> 32);
    cmd_ring[XHCI_CMD_RING_TRBS - 1].control  = (6U << 10) | (1U << 1) | 1U;
    __asm__ volatile("dsb sy" ::: "memory");

    // 5. Event ring + ERST — NC DMA pages; CPU reads go directly to DRAM (VL805 writes via DMA)
    void *evt_nc;
    unsigned long evt_pa = alloc_dma(&evt_nc);
    if (evt_pa == 0) return 0;
    evt_ring  = (xhci_trb_t *)evt_nc;
    evt_deq   = 0;
    evt_cycle = 1;
    evt_pa_g  = evt_pa;   // save PA for ERDP DMA address computations
    xhci_d_puts("v64:evt_pa="); xhci_d_hex((uint32_t)evt_pa); xhci_d_puts("\n");
    xhci_d_puts("v64:dma_hi="); xhci_d_hex((uint32_t)(DMA_TO_BUS(evt_pa) >> 32)); xhci_d_puts("\n");
    xhci_d_puts("v64:dma_lo="); xhci_d_hex((uint32_t)(DMA_TO_BUS(evt_pa))); xhci_d_puts("\n");

    void *erst_nc;
    unsigned long erst_pa = alloc_dma(&erst_nc);
    if (erst_pa == 0) return 0;
    erst = (xhci_erst_t *)erst_nc;
    erst->base_lo = (uint32_t)(DMA_TO_BUS(evt_pa) & 0xFFFFFFFFU);
    erst->base_hi = (uint32_t)(DMA_TO_BUS(evt_pa) >> 32);
    erst->size    = XHCI_EVT_RING_TRBS;
    erst->rsvd    = 0;
    __asm__ volatile("dsb sy" ::: "memory");

    // 6. Set DCBAAP
    uint64_t dcbaa_bus = DMA_TO_BUS(dcbaa_pa);
    XW64(op_base + 0x30, dcbaa_bus);

    // 7. Set CRCR (command ring control register): base DMA addr, RCS=1
    // xHCI spec Table 5-18: CRCR at operational_base + 0x18 (not 0x20).
    uint64_t crcr = DMA_TO_BUS(cmd_pa) | 0x1ULL;  // RCS=1
    XW64(op_base + 0x18, crcr);

    // 8. Set MaxSlotsEn in CONFIG register
    XW32(op_base + 0x38, xhci_max_slots_val & 0xFFU);

    // 9. Configure interrupter 0 in Runtime registers
    {
        uint32_t intr_base = xhci_rtsoff_val + 0x20U;  // interrupter 0
        XW32(intr_base + 0x08, 1U);                    // ERSTSZ = 1 segment
        XW64(intr_base + 0x10, DMA_TO_BUS(erst_pa));   // ERSTBA
        XW64(intr_base + 0x18, DMA_TO_BUS(evt_pa));    // ERDP
        XW32(intr_base + 0x00, XR32(intr_base + 0x00) | (1U << 1)); // IMAN.IE=1
        // Read back ERSTBA to confirm HC latched our value (cross-check DMA address)
        xhci_d_puts("v64:erstba=");
        xhci_d_hex(XR32(intr_base + 0x14)); xhci_d_putc(':');
        xhci_d_hex(XR32(intr_base + 0x10)); xhci_d_puts("\n");
    }
    __asm__ volatile("dsb sy" ::: "memory");

    // PED (bit1) is RW1CS: writing 1 CLEARS it (disables port). Must zero bit1 in all writes.
    static const uint32_t rw1c =
        (1U<<1)|(1U<<17)|(1U<<18)|(1U<<19)|(1U<<20)|(1U<<21)|(1U<<22)|(1U<<23);

    // 10. PP=1 while HC is still HALTED (RS=0 — do NOT start HC yet).
    //     Pi firmware pattern: PP=1 → XHCI-STOP(HCRST) at T+10ms → ring setup → RS=1 → PR=1.
    //     HCRST while VL805 is in early HS Polling (5ms in) resets VL805 USB2 PHY state.
    //     Without a second HCRST after PP=1, VL805 EEPROM firmware stays in an irrecoverable
    //     HS chirp loop; PR=1 at any timing results in speed=3 PED=0 indefinitely.
    {
        for (unsigned int n = 1; n <= xhci_max_ports_val; n++) {
            unsigned long off = portsc_offset(n);
            uint32_t v = XR32(off); v &= ~rw1c; v |= (1U << 9);
            XW32(off, v);
        }
        xhci_d_puts("v64:pp1_pre="); xhci_d_hex(XR32(portsc_offset(1))); xhci_d_puts("\n");
        xhci_udelay(5000);  // 5ms: VL805 detects device, begins HS chirp (Polling state)
        xhci_d_puts("v64:pp1_5ms="); xhci_d_hex(XR32(portsc_offset(1))); xhci_d_puts("\n");
    }

    // 11. Second HCRST (Pi firmware's XHCI-STOP at T+10ms after PP=1).
    //     Fires at ~5ms into VL805 HS chirp → VL805 firmware resets USB2 PHY state.
    //     Re-program HC registers after HCRST (it clears all operational regs).
    {
        XW32(op_base + 0x00, (1U << 1));  // HCRST=1, RS=0
        int rst2_ok = 0;
        for (int i = 0; i < 500; i++) {
            xhci_udelay(1000);
            if (!(XR32(op_base + 0x00) & (1U << 1))) { rst2_ok = 1; break; }
        }
        xhci_d_puts("v64:hcrst2_ok="); xhci_d_dec((unsigned int)rst2_ok); xhci_d_puts("\n");
        if (!rst2_ok) return 0;
        for (int i = 0; i < 500; i++) {
            if (!(XR32(op_base + 0x04) & (1U << 11))) break;
            xhci_udelay(1000);
        }
        // Re-program registers cleared by HCRST
        XW64(op_base + 0x30, dcbaa_bus);
        XW64(op_base + 0x18, DMA_TO_BUS(cmd_pa) | 0x1ULL);
        XW32(op_base + 0x38, xhci_max_slots_val & 0xFFU);
        {
            uint32_t intr_base = xhci_rtsoff_val + 0x20U;
            XW32(intr_base + 0x08, 1U);
            XW64(intr_base + 0x10, DMA_TO_BUS(erst_pa));
            XW64(intr_base + 0x18, DMA_TO_BUS(evt_pa));
            XW32(intr_base + 0x00, XR32(intr_base + 0x00) | (1U << 1));
        }
        // Reset software ring indices to match HC's reset state
        cmd_enq = 0; cmd_cycle = 1;
        evt_deq = 0; evt_cycle = 1;
        __asm__ volatile("dsb sy" ::: "memory");
        xhci_d_puts("v64:hcrst2_sts="); xhci_d_hex(XR32(op_base + 0x04)); xhci_d_puts("\n");
    }

    // 12. RS=1: start HC. Immediately issue PR=1 on connected ports.
    //     Total PP=1→PR=1 time: 5ms(Polling) + ~50ms(HCRST2) + ~5ms(RS) ≈ 60ms.
    //     Well within Pi firmware's ~230ms window before VL805 enters deep HS retry loop.
    XW32(op_base + 0x00, XR32(op_base + 0x00) | 0x1U | (1U << 2));
    xhci_udelay(5000);
    {
        uint32_t sts_run = XR32(op_base + 0x04);
        uint32_t cmd_run = XR32(op_base + 0x00);
        xhci_d_puts("v64:sts_run="); xhci_d_hex(sts_run); xhci_d_puts("\n");
        xhci_d_puts("v64:cmd_run="); xhci_d_hex(cmd_run); xhci_d_puts("\n");
        if (sts_run & 0x1U) return 0;
    }

    // PR=1 immediately on all connected ports.
    // After PRC fires (~50ms): port achieves PED=1/U0. VL805 EEPROM firmware drops the
    // port back to Polling if ENABLE_SLOT doesn't follow within ~100ms. So we return
    // IMMEDIATELY after clearing PRC — usb_enum must start within milliseconds.
    {
        xhci_d_puts("v64:pp_post="); xhci_d_hex(XR32(portsc_offset(1))); xhci_d_puts("\n");
        for (unsigned int n = 1; n <= xhci_max_ports_val; n++) {
            uint32_t psc = XR32(portsc_offset(n));
            if (psc & 0x1U) {
                uint32_t v = psc; v &= ~rw1c; v |= (1U << 4);  // PR=1
                XW32(portsc_offset(n), v);
            }
        }
        // Poll for PRC: check IMMEDIATELY, then every 1ms (max 200ms)
        for (int i = 0; i < 200; i++) {
            if (XR32(portsc_offset(1)) & (1U << 21)) break;
            xhci_udelay(1000);
        }
        {
            uint32_t psc = XR32(portsc_offset(1));
            xhci_d_puts("v64:pr_prc="); xhci_d_hex(psc); xhci_d_puts("\n");
            // PED may be 1 here (HS established). Clear PRC but do NOT wait — return
            // immediately so usb_enum catches PED=1 before VL805 firmware times out.
            if (psc & (1U << 21)) {
                uint32_t v = psc; v &= ~rw1c; v |= (1U << 21);
                XW32(portsc_offset(1), v);
            }
        }
        // NO PED wait — usb_enum must call PR=1 and ENABLE_SLOT while port is still in U0.
        xhci_d_puts("v64:pr_post="); xhci_d_hex(XR32(portsc_offset(1))); xhci_d_puts("\n");
    }

    // Count connected ports
    xhci_ports_connected_val = 0;
    for (unsigned int n = 1; n <= xhci_max_ports_val; n++) {
        if (XR32(portsc_offset(n)) & 0x1U) xhci_ports_connected_val++;
    }

    xhci_run_ok_val = 1;
    return 1;
}

int          kernel_xhci_run_ok(void)              { return xhci_run_ok_val;           }
unsigned int kernel_xhci_ports_connected(void)     { return xhci_ports_connected_val;  }

// ── V65: USB device enumeration ───────────────────────────────────────────
// Enumerates the first connected port: port reset → ENABLE_SLOT → ADDRESS_DEVICE
// → GET_DESCRIPTOR(Device) → extract vendor/product/class.

static int usb_enum_ok_val;
static unsigned int usb_enum_vendor_val;
static unsigned int usb_enum_product_val;
static unsigned int usb_enum_class_val;
static unsigned int usb_enum_addr_val;
static unsigned int usb_enum_stage_val;    // progress: 0=no-port,1=port,2=prc,3=slot,4=addr,5=desc
static unsigned int usb_enum_portsc_val;  // PORTSC before reset
static unsigned int usb_enum_portscR_val; // PORTSC after reset
static unsigned int usb_enum_slot_raw_val;// raw ENABLE_SLOT result
static unsigned int usb_enum_ad_raw_val;  // raw ADDRESS_DEVICE result

static int usb_enum_probed;

// Issue a single command TRB and wait for Command Completion Event
// Returns slot_id on success (for ENABLE_SLOT), or 0xFF on timeout/error.
static uint32_t xhci_issue_cmd(uint32_t p_lo, uint32_t p_hi, uint32_t status, uint32_t ctrl_template) {
    // Install TRB (without cycle bit first, then flip)
    unsigned int trb_idx = cmd_enq;
    cmd_ring[trb_idx].param_lo = p_lo;
    cmd_ring[trb_idx].param_hi = p_hi;
    cmd_ring[trb_idx].status   = status;
    __asm__ volatile("dmb st" ::: "memory");
    cmd_ring[trb_idx].control  = ctrl_template | (cmd_cycle ? 1U : 0U);
    // NC cmd_ring: writes go directly to DRAM — no dc civac needed
    __asm__ volatile("dsb sy" ::: "memory");
    cmd_enq++;
    if (cmd_enq >= XHCI_CMD_RING_TRBS - 1U) {
        cmd_ring[XHCI_CMD_RING_TRBS - 1].control ^= 0x1U;
        __asm__ volatile("dsb sy" ::: "memory");
        cmd_cycle ^= 1U;
        cmd_enq = 0;
    }
    xhci_ring_cmd_doorbell();
    // Diagnostic: check USBSTS and first 4 event ring entries after doorbell ring.
    // All 4 TRBs at indices 0-3 fit on one 64-byte cache line; one IVAC covers all.
    xhci_udelay(50000);  // 50ms wait for HC to process command
    uint32_t sts_after = XR32(((unsigned long)xhci_caplength_val) + 0x04);
    xhci_d_puts("v65:sts50ms="); xhci_d_hex(sts_after); xhci_d_puts("\n");
    {
        // NC event ring: scan all 256 TRBs for first non-zero (no cache ops needed)
        __asm__ volatile("dsb sy" ::: "memory");
        unsigned int first_nz = 0xFFFFU;
        for (unsigned int di = 0U; di < XHCI_EVT_RING_TRBS; di++) {
            if (evt_ring[di].control != 0U || evt_ring[di].status != 0U) {
                if (first_nz == 0xFFFFU) first_nz = di;
            }
        }
        xhci_d_puts("v65:ring_first_nz="); xhci_d_dec(first_nz); xhci_d_puts("\n");
        // Print first 8 TRBs
        for (unsigned int di = 0U; di < 8U; di++) {
            xhci_d_puts("v65:ev["); xhci_d_dec(di); xhci_d_puts("]=");
            xhci_d_hex(evt_ring[di].control); xhci_d_puts(",");
            xhci_d_hex(evt_ring[di].status); xhci_d_puts("\n");
        }
        xhci_d_puts("v65:iman="); xhci_d_hex(XR32(xhci_rtsoff_val + 0x20U)); xhci_d_puts("\n");
    }
    return xhci_wait_cmd_completion();
}

// dev_desc is now allocated as NC DMA memory inside kernel_usb_enum_selftest().

// Append a TRB to the endpoint 0 transfer ring for a given slot.
// We use an ad-hoc single-page ring for the control transfer.
static xhci_trb_t *ep0_ring;
static unsigned int ep0_enq;
static unsigned int ep0_cycle;

static void ep0_ring_trb(uint64_t param, uint32_t status, uint32_t ctrl) {
    ep0_ring[ep0_enq].param_lo = (uint32_t)(param & 0xFFFFFFFFU);
    ep0_ring[ep0_enq].param_hi = (uint32_t)(param >> 32);
    ep0_ring[ep0_enq].status   = status;
    __asm__ volatile("dmb st" ::: "memory");
    ep0_ring[ep0_enq].control  = ctrl | (ep0_cycle ? 1U : 0U);
    __asm__ volatile("dsb sy" ::: "memory");
    ep0_enq++;
}

int kernel_usb_enum_selftest(void) {
    if (usb_enum_probed) return usb_enum_ok_val;
    usb_enum_probed = 1;
    usb_enum_ok_val = 0;

    if (!kernel_xhci_run_selftest()) return 0;

    unsigned long op_base = (unsigned long)xhci_caplength_val;

    // Find first port with CCS=1; print all port states for diagnosis
    unsigned int target_port = 0;
    for (unsigned int n = 1; n <= xhci_max_ports_val; n++) {
        uint32_t portsc = XR32(portsc_offset(n));
        xhci_d_puts("v65:scan port="); xhci_d_dec(n);
        xhci_d_puts(" portsc="); xhci_d_hex(portsc); xhci_d_puts("\n");
        if ((portsc & 0x1U) && target_port == 0) { target_port = n; }
    }
    if (target_port == 0) {
        xhci_d_puts("v65:NO_PORT\n");
        return 0;
    }

    unsigned long psc_off = portsc_offset(target_port);

    uint32_t portsc = XR32(psc_off);
    usb_enum_stage_val = 1;
    usb_enum_portsc_val = portsc;
    xhci_d_puts("v65:target="); xhci_d_dec(target_port);
    xhci_d_puts(" portsc="); xhci_d_hex(portsc); xhci_d_puts("\n");

    // PED (bit1) is RW1CS: writing 1 CLEARS it (disables port). Include in mask.
    static const uint32_t k_portsc_rw1c =
        (1U<<1)|(1U<<17)|(1U<<18)|(1U<<19)|(1U<<20)|(1U<<21)|(1U<<22)|(1U<<23);

    int ped_got = (portsc & (1U << 1)) ? 1 : 0;  // skip PR if already enabled by chirp
    uint32_t pscR = portsc;

    // Try up to 3 PR=1 attempts; each waits 500ms for PED after PRC fires.
    for (int attempt = 0; attempt < 3 && !ped_got; attempt++) {
        // Pre-clear any stale PRC
        {
            uint32_t pre = XR32(psc_off);
            if (pre & (1U << 21)) {
                pre &= ~k_portsc_rw1c; pre |= (1U << 21);
                XW32(psc_off, pre); xhci_udelay(1000);
            }
        }
        // Assert PR=1; mask RW1C bits to avoid accidental clears
        uint32_t cur = XR32(psc_off);
        cur &= ~k_portsc_rw1c; cur |= (1U << 4);  // PR=1
        XW32(psc_off, cur);

        // Wait for PRC=1 (port reset complete, up to 200ms)
        // Check PRC immediately, then poll every 1ms (PRC fires ~50ms after PR=1)
        int prc = 0;
        for (int i = 0; i < 200; i++) {
            if (XR32(psc_off) & (1U << 21)) { prc = 1; break; }
            xhci_udelay(1000);
        }
        if (prc) {
            // Read PED BEFORE clearing PRC (PED is RW1CS — zeroed in k_portsc_rw1c mask)
            pscR = XR32(psc_off);
            if (pscR & (1U << 1)) ped_got = 1;
            // Clear PRC (k_portsc_rw1c zeros PED bit so we don't accidentally clear it)
            uint32_t pv = pscR; pv &= ~k_portsc_rw1c; pv |= (1U << 21);
            XW32(psc_off, pv);
            // Wait up to 500ms for PED=1 if not already got it
            for (int i = 0; i < 500 && !ped_got; i++) {
                pscR = XR32(psc_off);
                if (pscR & (1U << 1)) { ped_got = 1; break; }
                xhci_udelay(1000);
            }
        }
        xhci_d_puts("v65:attempt="); xhci_d_dec(attempt);
        xhci_d_puts(" prc="); xhci_d_dec(prc);
        xhci_d_puts(" ped="); xhci_d_dec(ped_got);
        xhci_d_puts(" pscR="); xhci_d_hex(pscR); xhci_d_puts("\n");
        if (!ped_got) xhci_udelay(100000);  // 100ms between attempts
    }

    // PP cycle fallback: power off 500ms, power on, wait 1500ms for reconnect+chirp
    if (!ped_got) {
        xhci_d_puts("v65:pp_cycle\n");
        uint32_t p = XR32(psc_off);
        p &= ~k_portsc_rw1c; p &= ~(1U << 9);  // PP=0
        XW32(psc_off, p);
        xhci_udelay(500000);  // 500ms discharge
        p = XR32(psc_off); p &= ~k_portsc_rw1c; p |= (1U << 9);  // PP=1
        XW32(psc_off, p);
        // After PP=1, wait for CCS=1 then immediately issue PR=1
        for (int i = 0; i < 800 && !(XR32(psc_off) & 0x1U); i++) xhci_udelay(1000);
        xhci_d_puts("v65:pp_ccs="); xhci_d_hex(XR32(psc_off)); xhci_d_puts("\n");
        // PR=1 immediately on reconnect (Pi firmware pattern)
        uint32_t cur = XR32(psc_off); cur &= ~k_portsc_rw1c; cur |= (1U << 4);
        XW32(psc_off, cur);
        int prc = 0;
        for (int i = 0; i < 200; i++) {
            if (XR32(psc_off) & (1U << 21)) { prc = 1; break; }
            xhci_udelay(1000);
        }
        if (prc) {
            pscR = XR32(psc_off);
            if (pscR & (1U << 1)) ped_got = 1;
            uint32_t pv = pscR; pv &= ~k_portsc_rw1c; pv |= (1U << 21);
            XW32(psc_off, pv);
            for (int i = 0; i < 500 && !ped_got; i++) {
                pscR = XR32(psc_off);
                if (pscR & (1U << 1)) { ped_got = 1; break; }
                xhci_udelay(1000);
            }
        }
        xhci_d_puts("v65:pp_wait prc="); xhci_d_dec(prc);
        xhci_d_puts(" ped="); xhci_d_dec(ped_got);
        xhci_d_puts(" pscR="); xhci_d_hex(pscR); xhci_d_puts("\n");
    }
    usb_enum_portscR_val = pscR;
    usb_enum_stage_val = 2;

    // Read port speed from PORTSC[13:10]
    unsigned int port_speed = (pscR >> 10) & 0xFU;  // 1=FS,2=LS,3=HS,4=SS
    xhci_d_puts("v65:speed="); xhci_d_dec(port_speed); xhci_d_puts("\n");

    // ENABLE_SLOT command (TRB type=9)
    xhci_d_puts("v65:ENABLE_SLOT\n");
    uint32_t slot_id = xhci_issue_cmd(0, 0, 0, 9U << 10);
    usb_enum_slot_raw_val = slot_id;
    xhci_d_puts("v65:slot="); xhci_d_hex(slot_id); xhci_d_puts("\n");
    if (slot_id == 0 || slot_id == 0xFFU || slot_id > xhci_max_slots_val) {
        xhci_d_puts("v65:SLOT_BAD\n");
        return 0;
    }
    usb_enum_stage_val = 3;

    // Allocate Input Context as NC DMA page
    void *ictx_nc;
    unsigned long ictx_pa = alloc_dma(&ictx_nc);
    if (ictx_pa == 0) return 0;
    xhci_ctx32_t *ictx = (xhci_ctx32_t *)ictx_nc;
    // alloc_dma zeroed the page
    // Input Control Context (ictx[0]): A1=1 (enable slot), A2=1 (enable EP0)
    ictx[0].w[1] = (1U << 0) | (1U << 1);  // Add Context flags A0 (slot) + A1 (EP0)
    // Slot Context (ictx[1]): Route string=0, Context Entries=1, speed, port number
    unsigned int route_str = 0;
    unsigned int speed = port_speed;  // use xHCI speed encoding
    ictx[1].w[0] = (route_str & 0xFFFFFU) | (speed << 20) | (1U << 27); // Context Entries=1
    ictx[1].w[1] = (target_port << 16) & 0x00FF0000U;  // root hub port number
    // EP0 Context (ictx[2]): EP Type=4 (Control), Max Packet Size
    unsigned int ep0_mps;
    if (port_speed == 4U)      ep0_mps = 512;  // SuperSpeed
    else if (port_speed == 3U) ep0_mps = 64;   // High Speed
    else                       ep0_mps = 8;    // Full/Low Speed (use 8 for initial)
    ictx[2].w[1] = (ep0_mps << 16) | (4U << 3) | (3U << 1);  // MaxPacketSize | EP Type=Control(4) | CErr=3
    ictx[2].w[4] = 8;  // Average TRB Length
    // Allocate EP0 transfer ring as NC DMA page
    void *ep0_nc;
    unsigned long ep0_pa = alloc_dma(&ep0_nc);
    if (ep0_pa == 0) return 0;
    ep0_ring  = (xhci_trb_t *)ep0_nc;
    ep0_enq   = 0;
    ep0_cycle = 1;
    // alloc_dma zeroed the ring; set Link TRB at end
    ep0_ring[255].param_lo = (uint32_t)(DMA_TO_BUS(ep0_pa) & 0xFFFFFFFFU);
    ep0_ring[255].param_hi = (uint32_t)(DMA_TO_BUS(ep0_pa) >> 32);
    ep0_ring[255].control  = (6U << 10) | (1U << 1) | 1U;
    __asm__ volatile("dsb sy" ::: "memory");
    ictx[2].w[2] = (uint32_t)(DMA_TO_BUS(ep0_pa) & 0xFFFFFFFFU) | (ep0_cycle ? 1U : 0U);
    ictx[2].w[3] = (uint32_t)(DMA_TO_BUS(ep0_pa) >> 32);

    // Allocate output Device Context as NC DMA page
    void *octx_nc;
    unsigned long octx_pa = alloc_dma(&octx_nc);
    if (octx_pa == 0) return 0;
    dcbaa[slot_id] = DMA_TO_BUS(octx_pa);
    __asm__ volatile("dsb sy" ::: "memory");

    // Dump ictx contents to verify before issuing ADDRESS_DEVICE
    xhci_d_puts("v65:ictx0="); xhci_d_hex(ictx[0].w[0]); xhci_d_putc(','); xhci_d_hex(ictx[0].w[1]); xhci_d_puts("\n");
    xhci_d_puts("v65:ictx1="); xhci_d_hex(ictx[1].w[0]); xhci_d_putc(','); xhci_d_hex(ictx[1].w[1]); xhci_d_puts("\n");
    xhci_d_puts("v65:ictx2="); xhci_d_hex(ictx[2].w[1]); xhci_d_putc(','); xhci_d_hex(ictx[2].w[2]); xhci_d_putc(','); xhci_d_hex(ictx[2].w[3]); xhci_d_puts("\n");

    uint64_t ictx_bus = DMA_TO_BUS(ictx_pa);

    // ADDRESS_DEVICE command (type=11), BSR=0 (send SET_ADDRESS to device)
    uint32_t ad_slot = (slot_id << 24) | (11U << 10);  // BSR=0
    xhci_d_puts("v65:ADDRESS_DEVICE slot="); xhci_d_dec(slot_id); xhci_d_puts("\n");
    xhci_issue_cmd((uint32_t)(ictx_bus & 0xFFFFFFFFU), (uint32_t)(ictx_bus >> 32), 0, ad_slot);
    uint32_t ad_cc = xhci_last_cc;
    usb_enum_ad_raw_val = ad_cc;
    xhci_d_puts("v65:ad_cc="); xhci_d_dec(ad_cc); xhci_d_puts("\n");
    if (ad_cc != 1U) {
        xhci_d_puts("v65:AD_ERR cc="); xhci_d_dec(ad_cc); xhci_d_puts("\n");
        return 0;
    }

    usb_enum_addr_val = slot_id;  // USB address = slot id after ADDRESS_DEVICE
    usb_enum_stage_val = 4;

    // GET_DESCRIPTOR(Device, wLength=18) via EP0 control transfer.
    // 8-byte setup packet: bmRequestType=0x80, bRequest=0x06, wValue=0x0100, wIndex=0, wLength=18
    // param_lo [31:0] = {byte3=wValue_hi, byte2=wValue_lo, byte1=bRequest, byte0=bmRequestType}
    // param_hi [31:0] = {byte7=wLength_hi, byte6=wLength_lo, byte5=wIndex_hi, byte4=wIndex_lo}
    // BUG-FIX: wLength must be in byte6 (bits[23:16]), NOT byte0 (bits[7:0]).
    // Old (wrong):  setup_hi = 0x00000012U  →  wLength=0, wIndex=0x12 (garbage)
    // New (correct): setup_hi = 0x00120000U  →  wLength=18, wIndex=0
    uint32_t setup_lo = 0x80U | (0x06U << 8) | (0x00U << 16) | (0x01U << 24);  // 0x01000680
    uint32_t setup_hi = 0x00120000U;  // wLength=18 in byte6 (bits[23:16]), wIndex=0
    // SETUP TRB: type=2, TRT=3 (IN data stage), IDT=1, IOC=0
    ep0_ring_trb(((uint64_t)setup_hi << 32) | setup_lo,
                 8U,  // Transfer Length = 8 (SETUP packet always 8 bytes)
                 (2U << 10) | (3U << 16) | (1U << 6));  // type=Setup, TRT=IN, IDT=1

    // DATA IN TRB: type=3, DIR=1 (IN), IOC=0, transfer length = wLength = 18
    // Use NC DMA buffer so VL805 DMA writes land directly in DRAM with no cache aliasing.
    void *desc_nc;
    unsigned long desc_pa = alloc_dma(&desc_nc);
    if (desc_pa == 0) { usb_enum_stage_val = 5; return 0; }
    volatile uint8_t *dev_desc = (volatile uint8_t *)desc_nc;
    uint64_t desc_bus = DMA_TO_BUS(desc_pa);
    ep0_ring_trb(desc_bus,
                 18U,  // TRB Transfer Length = 18 (matches wLength in SETUP)
                 (3U << 10) | (1U << 16));  // type=Data, DIR=IN

    // STATUS OUT TRB: type=4, DIR=0 (OUT), IOC=1
    ep0_ring_trb(0ULL, 0U, (4U << 10) | (1U << 5));  // type=Status, IOC=1

    __asm__ volatile("dsb sy" ::: "memory");

    // Ring EP0 doorbell for slot: doorbell register = DBOFF + slot_id*4, value=1 (EP1=EP0 control)
    xhci_d_puts("v65:EP0_RING slot="); xhci_d_dec(slot_id); xhci_d_puts("\n");
    XW32(xhci_dboff_val + slot_id * 4U, 1U);
    __asm__ volatile("dsb sy" ::: "memory");

    // Wait for Transfer Completion Events — drain until we see a Transfer Event (type=32)
    // which signals IOC completion of the Status TRB (final stage).
    int got_data = 0;
    unsigned int tevt_cc = 0;
    for (int i = 0; i < 500000 && !got_data; i++) {
        __asm__ volatile("dsb sy" ::: "memory");
        xhci_trb_t *trb = &evt_ring[evt_deq];
        if ((trb->control & 0x1U) == evt_cycle) {
            unsigned int trb_type = (trb->control >> 10) & 0x3FU;
            unsigned int cc = (trb->status >> 24) & 0x7FU;
            xhci_d_puts("v65:tevt type="); xhci_d_dec(trb_type);
            xhci_d_puts(" cc="); xhci_d_dec(cc);
            xhci_d_puts(" ctrl="); xhci_d_hex(trb->control); xhci_d_puts("\n");
            evt_deq++;
            if (evt_deq >= XHCI_EVT_RING_TRBS) { evt_deq = 0; evt_cycle ^= 1U; }
            uint64_t erdp2 = DMA_TO_BUS(evt_pa_g + (uint64_t)evt_deq * 16UL);
            XW64(xhci_rtsoff_val + 0x20U + 0x18, erdp2);
            __asm__ volatile("dsb sy" ::: "memory");
            if (trb_type == 32U) { got_data = 1; tevt_cc = cc; }
        }
        xhci_udelay(1);
    }
    xhci_d_puts("v65:got_data="); xhci_d_dec(got_data);
    xhci_d_puts(" cc="); xhci_d_dec(tevt_cc); xhci_d_puts("\n");
    if (!got_data || tevt_cc != 1U) {
        usb_enum_stage_val = 5;
        return 0;
    }

    // dev_desc is NC (Non-Cacheable): VL805 DMA writes go directly to DRAM; CPU reads bypass
    // cache. No DC IVAC needed.
    __asm__ volatile("dsb sy" ::: "memory");
    xhci_d_puts("v65:desc[0]="); xhci_d_hex(dev_desc[0]);
    xhci_d_puts(" [1]="); xhci_d_hex(dev_desc[1]);
    xhci_d_puts(" [4]="); xhci_d_hex(dev_desc[4]); xhci_d_puts("\n");

    // Parse full 18-byte USB device descriptor
    if (dev_desc[1] != 0x01U) {
        usb_enum_stage_val = 6;
        return 0;
    }
    usb_enum_class_val   = dev_desc[4];
    usb_enum_vendor_val  = (unsigned int)dev_desc[8]  | ((unsigned int)dev_desc[9]  << 8);
    usb_enum_product_val = (unsigned int)dev_desc[10] | ((unsigned int)dev_desc[11] << 8);
    usb_enum_stage_val   = 7;

    usb_enum_ok_val = 1;
    return 1;
}

int          kernel_usb_enum_ok(void)       { return usb_enum_ok_val;        }
unsigned int kernel_usb_enum_vendor(void)   { return usb_enum_vendor_val;    }
unsigned int kernel_usb_enum_product(void)  { return usb_enum_product_val;   }
unsigned int kernel_usb_enum_class(void)    { return usb_enum_class_val;     }
unsigned int kernel_usb_enum_addr(void)     { return usb_enum_addr_val;      }
unsigned int kernel_usb_enum_stage(void)    { return usb_enum_stage_val;    }
unsigned int kernel_usb_enum_portsc(void)   { return usb_enum_portsc_val;   }
unsigned int kernel_usb_enum_portscR(void)  { return usb_enum_portscR_val;  }
unsigned int kernel_usb_enum_slot_raw(void) { return usb_enum_slot_raw_val; }
unsigned int kernel_usb_enum_ad_raw(void)   { return usb_enum_ad_raw_val;   }

// ── V66: HID boot-protocol keyboard ──────────────────────────────────────────
// Hub (VIA Labs 0x2109:0x3431, slot=1) at root port 1.
// Keyboard is behind the hub: enumerate downstream ports, find HID device,
// configure boot protocol, poll interrupt-IN for a real physical keypress.

static int kbd_ok_val;
static unsigned int kbd_keycode_val;
static unsigned int kbd_char_val;
static int kbd_probed;

static unsigned int hid_keycode_to_char(unsigned int kc) {
    if (kc >= 0x04U && kc <= 0x1DU) return 'a' + (kc - 0x04U);
    if (kc >= 0x1EU && kc <= 0x27U) return '1' + (kc - 0x1EU);
    if (kc == 0x28U) return '\n';
    if (kc == 0x2CU) return ' ';
    return '?';
}

// Generic EP0 control transfer. ep0_ring/ep0_enq/ep0_cycle must point to the
// target slot's ring before calling. Returns Transfer Event completion code.
static unsigned int v66_ctrl_xfer(unsigned int slot_id,
                                   uint32_t setup_lo, uint32_t setup_hi,
                                   uint64_t data_bus, unsigned int data_len,
                                   int dir_in) {
    unsigned int trt = (data_len > 0U) ? (dir_in ? 3U : 2U) : 0U;
    ep0_ring_trb(((uint64_t)setup_hi << 32) | (uint64_t)setup_lo,
                 8U, (2U << 10) | (trt << 16) | (1U << 6));
    if (data_len > 0U)
        ep0_ring_trb(data_bus, data_len,
                     (3U << 10) | (dir_in ? (1U << 16) : 0U));
    // STATUS direction is opposite of data phase
    ep0_ring_trb(0ULL, 0U,
                 (4U << 10) | (1U << 5) | (dir_in ? 0U : (1U << 16)));
    __asm__ volatile("dsb sy" ::: "memory");
    XW32(xhci_dboff_val + slot_id * 4U, 1U);
    __asm__ volatile("dsb sy" ::: "memory");
    for (int i = 0; i < 1000000; i++) {
        __asm__ volatile("dsb sy" ::: "memory");
        xhci_trb_t *trb = &evt_ring[evt_deq];
        if ((trb->control & 0x1U) == evt_cycle) {
            unsigned int tp = (trb->control >> 10) & 0x3FU;
            unsigned int cc = (trb->status >> 24) & 0x7FU;
            unsigned int ev_slot = (trb->control >> 24) & 0xFFU;
            xhci_d_puts("v66:xe tp="); xhci_d_dec(tp);
            xhci_d_puts(" cc="); xhci_d_dec(cc);
            xhci_d_puts(" sl="); xhci_d_dec(ev_slot);
            xhci_d_puts(" xslot="); xhci_d_dec(slot_id); xhci_d_puts("\n");
            evt_deq++;
            if (evt_deq >= XHCI_EVT_RING_TRBS) { evt_deq = 0; evt_cycle ^= 1U; }
            XW64(xhci_rtsoff_val + 0x20U + 0x18U,
                 DMA_TO_BUS(evt_pa_g + (uint64_t)evt_deq * 16UL));
            __asm__ volatile("dsb sy" ::: "memory");
            if (tp == 32U) return cc;
        }
        xhci_udelay(1);
    }
    return 0xFFU;
}

// Issue a command TRB (type encoded in ctrl[15:10]) and wait for completion.
// Bypasses the v65 debug dump in xhci_issue_cmd.
static uint32_t v66_cmd(uint32_t p_lo, uint32_t p_hi, uint32_t status, uint32_t ctrl) {
    unsigned int idx = cmd_enq;
    cmd_ring[idx].param_lo = p_lo;
    cmd_ring[idx].param_hi = p_hi;
    cmd_ring[idx].status   = status;
    __asm__ volatile("dmb st" ::: "memory");
    cmd_ring[idx].control  = ctrl | (cmd_cycle ? 1U : 0U);
    __asm__ volatile("dsb sy" ::: "memory");
    cmd_enq++;
    if (cmd_enq >= XHCI_CMD_RING_TRBS - 1U) {
        cmd_ring[XHCI_CMD_RING_TRBS - 1U].control ^= 0x1U;
        __asm__ volatile("dsb sy" ::: "memory");
        cmd_cycle ^= 1U;
        cmd_enq = 0U;
    }
    xhci_ring_cmd_doorbell();
    return xhci_wait_cmd_completion();
}

int kernel_kbd_selftest(void) {
    if (kbd_probed) return kbd_ok_val;
    kbd_probed = 1;
    kbd_ok_val = 0;

    // Requires hub enumeration from v65
    if (!kernel_usb_enum_selftest()) return 0;

    unsigned int hub_tt_think_g = 0;  // TT Think Time extracted from hub descriptor

    // ── Phase 0: SET_CONFIGURATION(1) on the hub (slot=1) ────────────────────
    // Hub must be in Configured state before hub-class requests work.
    {
        // bmRequestType=0x00 host→dev standard device, bRequest=SET_CONFIGURATION=0x09, wValue=1
        uint32_t slo = 0x00U | (0x09U << 8) | (0x01U << 16);
        unsigned int cc = v66_ctrl_xfer(1U, slo, 0U, 0ULL, 0U, 0);  // dir_in=0: Control WRITE
        xhci_d_puts("v66:hub_setcfg cc="); xhci_d_dec(cc); xhci_d_puts("\n");
        // cc=1 (success) or cc=5 (STALL — hub may already be configured). Both acceptable.
        if (cc != 1U && cc != 5U) return 0;
    }

    // ── Phase 1: GET_DESCRIPTOR(HUB) → port count ────────────────────────────
    // bmRequestType=0xA0 (dev→host, class, device), bRequest=GET_DESCRIPTOR=0x06,
    // wValue=0x2900 (type=HUB=0x29, index=0), wIndex=0, wLength=9
    unsigned int hub_ports = 0;
    {
        void *hub_nc;
        unsigned long hub_pa = alloc_dma(&hub_nc);
        if (hub_pa == 0) return 0;
        volatile uint8_t *hd = (volatile uint8_t *)hub_nc;
        uint32_t slo = 0xA0U | (0x06U << 8) | (0x00U << 16) | (0x29U << 24);
        uint32_t shi = 0x00U | (0x00U << 8) | (9U << 16);
        unsigned int cc = v66_ctrl_xfer(1U, slo, shi, DMA_TO_BUS(hub_pa), 9U, 1);
        xhci_d_puts("v66:hub_desc cc="); xhci_d_dec(cc);
        xhci_d_puts(" ports="); xhci_d_dec(hd[2]); xhci_d_puts("\n");
        if (cc != 1U) return 0;
        hub_ports = hd[2];
        if (hub_ports == 0 || hub_ports > 8U) hub_ports = 4U;  // sanity clamp
        // Extract TT Think Time from wHubCharacteristics bits[6:5] (bytes 3-4)
        uint16_t wHubChar = (uint16_t)hd[3] | ((uint16_t)hd[4] << 8);
        unsigned int hub_tt_think = (wHubChar >> 5) & 0x3U;
        xhci_d_puts("v66:hub_wchar="); xhci_d_hex(wHubChar);
        xhci_d_puts(" tt_think="); xhci_d_dec(hub_tt_think); xhci_d_puts("\n");
        // Store for use in keyboard Slot Context
        hub_tt_think_g = hub_tt_think;
    }
    xhci_d_puts("v66:hub_ports="); xhci_d_dec(hub_ports); xhci_d_puts("\n");

    // ── Phase 2: Power and probe hub downstream ports ─────────────────────────
    // SET_PORT_FEATURE(PORT_POWER=8) then wait 100ms, then GET_PORT_STATUS.
    // Find first connected port with a non-hub FS/LS device.
    unsigned int kbd_hub_port = 0;
    unsigned int kbd_speed    = 0;  // xHCI speed encoding (1=FS,2=LS,3=HS)
    {
        void *pst_nc;
        unsigned long pst_pa = alloc_dma(&pst_nc);
        if (pst_pa == 0) return 0;
        volatile uint8_t *pst = (volatile uint8_t *)pst_nc;

        for (unsigned int port = 1; port <= hub_ports; port++) {
            // SET_PORT_FEATURE(PORT_POWER=8, portN)
            uint32_t slo = 0x23U | (0x03U << 8) | (8U << 16);
            uint32_t shi = (port & 0xFFU);
            v66_ctrl_xfer(1U, slo, shi, 0ULL, 0U, 0);  // dir_in=0: Control WRITE
        }
        xhci_udelay(100000);  // 100ms power-on delay

        for (unsigned int port = 1; port <= hub_ports && kbd_hub_port == 0; port++) {
            // Clear port status buffer
            volatile uint64_t *p64 = (volatile uint64_t *)pst_nc;
            p64[0] = 0ULL;
            __asm__ volatile("dsb sy" ::: "memory");

            // GET_PORT_STATUS(portN): bmRequestType=0xA3, bRequest=0x00, wValue=0, wIndex=port, wLength=4
            uint32_t slo = 0xA3U | (0x00U << 8);
            uint32_t shi = (port & 0xFFU) | (4U << 16);
            unsigned int cc = v66_ctrl_xfer(1U, slo, shi, DMA_TO_BUS(pst_pa), 4U, 1);
            __asm__ volatile("dsb sy" ::: "memory");
            uint16_t wPortStatus = (uint16_t)pst[0] | ((uint16_t)pst[1] << 8);
            xhci_d_puts("v66:pst port="); xhci_d_dec(port);
            xhci_d_puts(" cc="); xhci_d_dec(cc);
            xhci_d_puts(" wps="); xhci_d_hex(wPortStatus); xhci_d_puts("\n");

            if (cc != 1U) continue;
            if (!(wPortStatus & 0x0001U)) continue;  // CCS=0: no device connected

            // Device connected on this port — issue PORT_RESET
            uint32_t rslo = 0x23U | (0x03U << 8) | (4U << 16);  // SET_PORT_FEATURE(PORT_RESET=4)
            uint32_t rshi = (port & 0xFFU);
            v66_ctrl_xfer(1U, rslo, rshi, 0ULL, 0U, 0);  // dir_in=0: Control WRITE
            xhci_udelay(60000);  // 60ms: hub resets port, chirp negotiation, recovery

            // GET_PORT_STATUS again — check PES=1 (enabled) and speed
            p64[0] = 0ULL;
            __asm__ volatile("dsb sy" ::: "memory");
            cc = v66_ctrl_xfer(1U, slo, shi, DMA_TO_BUS(pst_pa), 4U, 1);
            __asm__ volatile("dsb sy" ::: "memory");
            wPortStatus = (uint16_t)pst[0] | ((uint16_t)pst[1] << 8);
            xhci_d_puts("v66:pst_r port="); xhci_d_dec(port);
            xhci_d_puts(" cc="); xhci_d_dec(cc);
            xhci_d_puts(" wps="); xhci_d_hex(wPortStatus); xhci_d_puts("\n");

            if (cc != 1U || !(wPortStatus & 0x0002U)) continue;  // PES=0: not enabled

            // Decode USB 2.0 hub port speed bits[11:10] of wPortStatus
            unsigned int pspd = (wPortStatus >> 9) & 0x3U;
            // pspd: 00=FS, 01=LS, 10=HS (USB 2.0 hub encoding)
            // Map to xHCI speed: FS=1, LS=2, HS=3
            unsigned int xspd = (pspd == 1U) ? 2U : (pspd == 2U) ? 3U : 1U;
            xhci_d_puts("v66:pspd="); xhci_d_dec(pspd);
            xhci_d_puts(" xspd="); xhci_d_dec(xspd); xhci_d_puts("\n");

            kbd_hub_port = port;
            kbd_speed    = xspd;
        }
    }
    xhci_d_puts("v66:kbd_port="); xhci_d_dec(kbd_hub_port);
    xhci_d_puts(" spd="); xhci_d_dec(kbd_speed); xhci_d_puts("\n");
    if (kbd_hub_port == 0) return 0;

    // ── Phase 3: Enumerate keyboard ──────────────────────────────────────────
    // ENABLE_SLOT — xhci_wait_cmd_completion returns slot_id directly (already extracted)
    uint32_t kbd_slot = v66_cmd(0, 0, 0, 9U << 10);
    xhci_d_puts("v66:kbd_slot="); xhci_d_dec(kbd_slot);
    xhci_d_puts(" cc="); xhci_d_dec(xhci_last_cc); xhci_d_puts("\n");
    if (xhci_last_cc != 1U || kbd_slot == 0 || kbd_slot > xhci_max_slots_val) return 0;

    // Allocate Input Context for keyboard
    void *kictx_nc;
    unsigned long kictx_pa = alloc_dma(&kictx_nc);
    if (kictx_pa == 0) return 0;
    xhci_ctx32_t *kictx = (xhci_ctx32_t *)kictx_nc;

    // Allocate EP0 ring for keyboard
    void *kep0_nc;
    unsigned long kep0_pa = alloc_dma(&kep0_nc);
    if (kep0_pa == 0) return 0;

    // Allocate Output Device Context for keyboard
    void *koctx_nc;
    unsigned long koctx_pa = alloc_dma(&koctx_nc);
    if (koctx_pa == 0) return 0;
    dcbaa[kbd_slot] = DMA_TO_BUS(koctx_pa);
    __asm__ volatile("dsb sy" ::: "memory");

    // Set up EP0 ring globals to point to keyboard's ring
    ep0_ring  = (xhci_trb_t *)kep0_nc;
    ep0_enq   = 0;
    ep0_cycle = 1;
    ep0_ring[255].param_lo = (uint32_t)(DMA_TO_BUS(kep0_pa) & 0xFFFFFFFFU);
    ep0_ring[255].param_hi = (uint32_t)(DMA_TO_BUS(kep0_pa) >> 32);
    ep0_ring[255].control  = (6U << 10) | (1U << 1) | 1U;
    __asm__ volatile("dsb sy" ::: "memory");

    // Input Control Context: A0 (slot) + A1 (EP0)
    kictx[0].w[1] = (1U << 0) | (1U << 1);
    // Slot Context: VL805 requires route string = hub port (vendor behaviour, not spec)
    // Root Hub Port = 1 (the xHCI root port the hub is on)
    // TT Hub Slot ID = hub slot (1), TT Port = kbd_hub_port
    // Note: VL805 rejects TT Think Time in bits[17:16] (treats as reserved) — leave at 0
    kictx[1].w[0] = (kbd_hub_port & 0xFU) | (kbd_speed << 20) | (1U << 27);
    kictx[1].w[1] = (1U << 16);  // Root Hub Port Number = 1
    if (kbd_speed < 3U) {
        // FS or LS keyboard behind HS hub — TT Hub SlotID in [7:0], TT Port in [15:8]
        kictx[1].w[2] = 1U | (kbd_hub_port << 8);
    }
    // EP0 Context: EP Type=4 (Control), MPS=8 (FS keyboard default), CErr=3
    kictx[2].w[1] = (8U << 16) | (4U << 3) | (3U << 1);
    kictx[2].w[2] = (uint32_t)(DMA_TO_BUS(kep0_pa) & 0xFFFFFFFFU) | 1U;
    kictx[2].w[3] = (uint32_t)(DMA_TO_BUS(kep0_pa) >> 32);
    kictx[2].w[4] = 8U;
    __asm__ volatile("dsb sy" ::: "memory");

    xhci_d_puts("v66:kictx0="); xhci_d_hex(kictx[0].w[0]); xhci_d_putc(','); xhci_d_hex(kictx[0].w[1]); xhci_d_puts("\n");
    xhci_d_puts("v66:kictx1="); xhci_d_hex(kictx[1].w[0]); xhci_d_putc(','); xhci_d_hex(kictx[1].w[1]); xhci_d_putc(','); xhci_d_hex(kictx[1].w[2]); xhci_d_puts("\n");
    xhci_d_puts("v66:kictx2="); xhci_d_hex(kictx[2].w[1]); xhci_d_putc(','); xhci_d_hex(kictx[2].w[2]); xhci_d_putc(','); xhci_d_hex(kictx[2].w[3]); xhci_d_puts("\n");

    // ADDRESS_DEVICE for keyboard (BSR=0 → sends SET_ADDRESS on the bus)
    uint64_t kictx_bus = DMA_TO_BUS(kictx_pa);
    uint32_t ad_ctrl = ((kbd_slot & 0xFFU) << 24) | (11U << 10);
    v66_cmd((uint32_t)(kictx_bus & 0xFFFFFFFFU),
            (uint32_t)(kictx_bus >> 32), 0U, ad_ctrl);
    xhci_d_puts("v66:kbd_ad_cc="); xhci_d_dec(xhci_last_cc); xhci_d_puts("\n");
    if (xhci_last_cc != 1U) return 0;
    xhci_udelay(10000);  // 10ms: USB TDSETADDR recovery after SET_ADDRESS

    // ── Phase 4: GET_DESCRIPTOR(Device) on keyboard ───────────────────────────
    void *kdesc_nc;
    unsigned long kdesc_pa = alloc_dma(&kdesc_nc);
    if (kdesc_pa == 0) return 0;
    volatile uint8_t *kdesc = (volatile uint8_t *)kdesc_nc;

    {
        // Fetch first 8 bytes to discover bMaxPacketSize0 (byte 7).
        // Use wLength=8 (single-packet for any FS device) to avoid Babble from MPS mismatch.
        uint32_t slo = 0x80U | (0x06U << 8) | (0x00U << 16) | (0x01U << 24);
        uint32_t shi = 0x00U | (8U << 16);
        unsigned int cc8 = v66_ctrl_xfer(kbd_slot, slo, shi,
                                          DMA_TO_BUS(kdesc_pa), 8U, 1);
        __asm__ volatile("dsb sy" ::: "memory");
        unsigned int kbd_ep0_mps = kdesc[7];
        xhci_d_puts("v66:kbd_desc8 cc="); xhci_d_dec(cc8);
        xhci_d_puts(" b0="); xhci_d_hex(kdesc[0]);
        xhci_d_puts(" mps="); xhci_d_dec(kbd_ep0_mps); xhci_d_puts("\n");
        if (cc8 != 1U) return 0;
        if (kbd_ep0_mps < 8U || kbd_ep0_mps > 64U) kbd_ep0_mps = 8U;

        // If bMaxPacketSize0 != 8, we need EVALUATE_CONTEXT to update EP0 MPS before
        // issuing GET_DESCRIPTOR(18) — otherwise the VL805 babbles (packet > expected MPS).
        if (kbd_ep0_mps != 8U) {
            xhci_d_puts("v66:eval_ctx mps="); xhci_d_dec(kbd_ep0_mps); xhci_d_puts("\n");
            volatile uint32_t *kictx_raw2 = (volatile uint32_t *)kictx_nc;
            for (unsigned i = 0; i < 256U; i++) kictx_raw2[i] = 0U;
            __asm__ volatile("dsb sy" ::: "memory");
            kictx[0].w[1] = (1U << 1);  // A1 = update EP0 context only
            kictx[2].w[1] = (kbd_ep0_mps << 16) | (4U << 3) | (3U << 1);
            kictx[2].w[2] = (uint32_t)(DMA_TO_BUS(kep0_pa) & 0xFFFFFFFFU) | 1U;
            kictx[2].w[3] = (uint32_t)(DMA_TO_BUS(kep0_pa) >> 32);
            kictx[2].w[4] = kbd_ep0_mps;
            __asm__ volatile("dsb sy" ::: "memory");
            uint64_t kictx_bus2 = DMA_TO_BUS(kictx_pa);
            uint32_t ec_ctrl = ((kbd_slot & 0xFFU) << 24) | (13U << 10);
            v66_cmd((uint32_t)(kictx_bus2 & 0xFFFFFFFFU),
                    (uint32_t)(kictx_bus2 >> 32), 0U, ec_ctrl);
            xhci_d_puts("v66:eval_cc="); xhci_d_dec(xhci_last_cc); xhci_d_puts("\n");
            if (xhci_last_cc != 1U) return 0;
        }

        // Now fetch full 18-byte descriptor with correct MPS programmed
        shi = 0x00U | (18U << 16);
        unsigned int cc = v66_ctrl_xfer(kbd_slot, slo, shi,
                                         DMA_TO_BUS(kdesc_pa), 18U, 1);
        __asm__ volatile("dsb sy" ::: "memory");
        xhci_d_puts("v66:kbd_desc cc="); xhci_d_dec(cc);
        xhci_d_puts(" cls="); xhci_d_hex(kdesc[4]); xhci_d_puts("\n");
        if (cc != 1U) return 0;
    }

    // ── Phase 5: GET_DESCRIPTOR(Configuration, wLength=64) ────────────────────
    void *kcfg_nc;
    unsigned long kcfg_pa = alloc_dma(&kcfg_nc);
    if (kcfg_pa == 0) return 0;
    volatile uint8_t *kcfg = (volatile uint8_t *)kcfg_nc;

    unsigned int int_ep_addr  = 0;   // USB endpoint address byte (e.g. 0x81)
    unsigned int int_ep_mps   = 8;   // max packet size (boot keyboard = 8)
    unsigned int int_ep_ivl   = 10;  // bInterval in ms (typical boot keyboard)
    unsigned int kbd_iface_num = 0;  // interface index for SET_PROTOCOL wIndex
    {
        uint32_t slo = 0x80U | (0x06U << 8) | (0x00U << 16) | (0x02U << 24);
        uint32_t shi = 0x00U | (255U << 16);
        unsigned int cc = v66_ctrl_xfer(kbd_slot, slo, shi,
                                         DMA_TO_BUS(kcfg_pa), 255U, 1);
        __asm__ volatile("dsb sy" ::: "memory");
        xhci_d_puts("v66:cfg cc="); xhci_d_dec(cc); xhci_d_puts("\n");
        if (cc != 1U) return 0;

        // Scan all descriptors for the best interrupt-IN endpoint from a keyboard
        // interface. Score each HID interface:
        //   3 = class=3 sub=1 proto=1 (HID boot keyboard — best)
        //   2 = class=3 sub=? proto=1 (any HID with keyboard protocol)
        //   1 = class=3 (other HID, e.g. mouse — fallback only)
        //   0 = not HID
        // Always prefer higher score; break immediately on score 3.
        unsigned int total = (unsigned int)kcfg[2] | ((unsigned int)kcfg[3] << 8);
        xhci_d_puts("v66:cfg_total="); xhci_d_dec(total); xhci_d_puts("\n");
        if (total > 255U) total = 255U;
        unsigned int off = 0;
        unsigned int cur_iface_num   = 0;
        unsigned int cur_iface_class = 0;
        unsigned int cur_iface_sub   = 0;
        unsigned int cur_iface_proto = 0;
        unsigned int int_ep_score    = 0;
        while (off + 4U <= total) {
            unsigned int bLen  = kcfg[off];
            unsigned int bType = kcfg[off + 1];
            if (bLen == 0U) break;
            if (bType == 0x04U && bLen >= 9U) {  // Interface Descriptor
                cur_iface_num   = kcfg[off + 2];
                cur_iface_class = kcfg[off + 5];
                cur_iface_sub   = kcfg[off + 6];
                cur_iface_proto = kcfg[off + 7];
                xhci_d_puts("v66:iface="); xhci_d_dec(cur_iface_num);
                xhci_d_puts(" cls="); xhci_d_hex(cur_iface_class);
                xhci_d_puts(" sub="); xhci_d_hex(cur_iface_sub);
                xhci_d_puts(" proto="); xhci_d_hex(cur_iface_proto); xhci_d_puts("\n");
            } else if (bType == 0x05U && bLen >= 7U) {  // Endpoint Descriptor
                unsigned int addr  = kcfg[off + 2];
                unsigned int attrs = kcfg[off + 3];
                if ((addr & 0x80U) && (attrs & 0x03U) == 0x03U) {
                    // Interrupt-IN endpoint: score this interface
                    unsigned int score = 0;
                    if (cur_iface_class == 0x03U) {
                        if (cur_iface_proto == 0x01U) {
                            score = (cur_iface_sub == 0x01U) ? 3U : 2U;
                        } else {
                            score = 1U;  // other HID (mouse, consumer, etc.)
                        }
                    }
                    if (score > int_ep_score || int_ep_addr == 0U) {
                        int_ep_addr    = addr;
                        int_ep_mps     = (unsigned int)kcfg[off + 4] |
                                         ((unsigned int)kcfg[off + 5] << 8);
                        int_ep_ivl     = kcfg[off + 6];
                        kbd_iface_num  = cur_iface_num;
                        int_ep_score   = score;
                        xhci_d_puts("v66:ep_cand="); xhci_d_hex(addr);
                        xhci_d_puts(" score="); xhci_d_dec(score);
                        xhci_d_puts(" mps="); xhci_d_dec(int_ep_mps); xhci_d_puts("\n");
                        if (score == 3U) break;  // boot keyboard — can't do better
                    }
                }
            }
            off += bLen;
        }
        xhci_d_puts("v66:int_ep="); xhci_d_hex(int_ep_addr);
        xhci_d_puts(" mps="); xhci_d_dec(int_ep_mps);
        xhci_d_puts(" ivl="); xhci_d_dec(int_ep_ivl);
        xhci_d_puts(" iface="); xhci_d_dec(kbd_iface_num);
        xhci_d_puts(" score="); xhci_d_dec(int_ep_score); xhci_d_puts("\n");
        if (int_ep_addr == 0) return 0;
    }

    // ── Phase 6: SET_CONFIGURATION(1) on keyboard ────────────────────────────
    {
        uint32_t slo = 0x00U | (0x09U << 8) | (0x01U << 16);
        unsigned int cc = v66_ctrl_xfer(kbd_slot, slo, 0U, 0ULL, 0U, 0);  // dir_in=0: Control WRITE
        xhci_d_puts("v66:kbd_setcfg cc="); xhci_d_dec(cc); xhci_d_puts("\n");
        if (cc != 1U && cc != 5U) return 0;
    }

    // ── Phase 7: SET_PROTOCOL(0) — HID boot protocol ─────────────────────────
    // bmRequestType=0x21 (host→dev, class, interface), bRequest=SET_PROTOCOL=0x0B,
    // wValue=0 (boot), wIndex=kbd_iface_num (must match the HID interface), wLength=0
    {
        uint32_t slo = 0x21U | (0x0BU << 8) | (0x00U << 16);
        uint32_t shi = (0U << 16) | (kbd_iface_num & 0xFFU);  // wLength=0, wIndex=interface
        unsigned int cc = v66_ctrl_xfer(kbd_slot, slo, shi, 0ULL, 0U, 0);  // dir_in=0: Control WRITE
        xhci_d_puts("v66:set_proto cc="); xhci_d_dec(cc);
        xhci_d_puts(" iface="); xhci_d_dec(kbd_iface_num); xhci_d_puts("\n");
        // cc=1: success. cc=5 (TRB error) or cc=6 (Stall Error): device does not
        // support SET_PROTOCOL (non-boot interface, subclass=0) — still proceed.
        if (cc != 1U && cc != 5U && cc != 6U) return 0;
    }

    // ── Phase 8: CONFIGURE_ENDPOINT — add interrupt-IN endpoint ──────────────
    // Allocate interrupt-IN transfer ring (NC DMA page)
    void *int_ring_nc;
    unsigned long int_ring_pa = alloc_dma(&int_ring_nc);
    if (int_ring_pa == 0) return 0;
    xhci_trb_t *int_ring = (xhci_trb_t *)int_ring_nc;
    // Link TRB at position 255
    int_ring[255].param_lo = (uint32_t)(DMA_TO_BUS(int_ring_pa) & 0xFFFFFFFFU);
    int_ring[255].param_hi = (uint32_t)(DMA_TO_BUS(int_ring_pa) >> 32);
    int_ring[255].control  = (6U << 10) | (1U << 1) | 1U;
    __asm__ volatile("dsb sy" ::: "memory");

    // Build CONFIGURE_ENDPOINT Input Context
    // ep_num = USB EP number (bits[3:0] of addr), dir=IN → xHCI context index = ep_num*2+1
    unsigned int ep_num = int_ep_addr & 0x0FU;
    unsigned int ctx_idx = ep_num * 2U + 1U;  // e.g. EP1 IN → index 3
    xhci_d_puts("v66:ctx_idx="); xhci_d_dec(ctx_idx); xhci_d_puts("\n");
    if (ctx_idx < 2U || ctx_idx > 31U) return 0;

    // Reuse kictx page for CONFIGURE_ENDPOINT input context
    // Clear the page first (volatile writes via NC mapping)
    volatile uint32_t *kictx_raw = (volatile uint32_t *)kictx_nc;
    for (unsigned i = 0; i < 256U; i++) kictx_raw[i] = 0U;
    __asm__ volatile("dsb sy" ::: "memory");

    // Input Control Context: A0 (slot) + A_ctx_idx (interrupt-IN endpoint)
    kictx[0].w[1] = (1U << 0) | (1U << ctx_idx);
    // Slot Context: update Context Entries to include interrupt-IN
    kictx[1].w[0] = (kbd_hub_port & 0xFU) | (kbd_speed << 20) | (ctx_idx << 27);
    kictx[1].w[1] = (1U << 16);
    if (kbd_speed < 3U) {
        kictx[1].w[2] = 1U | (kbd_hub_port << 8);
    }
    // Interrupt-IN EP Context at kictx[ctx_idx + 1] (ictx[0]=InputCtrl, ictx[1]=Slot, ictx[2]=EP0...)
    // In the Input Context, context index N is at kictx[N+1] for CSZ=0.
    // ctx_idx for EP1 IN = 3, so kictx[4].
    xhci_ctx32_t *ep_ctx = &kictx[ctx_idx + 1U];
    // xHCI Interval for FS device: bInterval is in ms. Convert to 125µs units: bInterval*8.
    // xHCI Interval field = roundup(log2(bInterval*8)).
    unsigned int ivl8 = int_ep_ivl * 8U;
    unsigned int xhci_ivl = 0;
    while ((1U << xhci_ivl) < ivl8) xhci_ivl++;
    if (xhci_ivl < 3U) xhci_ivl = 3U;  // minimum sane value
    if (xhci_ivl > 15U) xhci_ivl = 15U;
    ep_ctx->w[0] = (xhci_ivl << 16);
    ep_ctx->w[1] = ((int_ep_mps & 0x7FFU) << 16) | (7U << 3) | (3U << 1);  // MPS|Type=IntrIN(7)|CErr=3
    ep_ctx->w[2] = (uint32_t)(DMA_TO_BUS(int_ring_pa) & 0xFFFFFFFFU) | 1U;  // DCS=1
    ep_ctx->w[3] = (uint32_t)(DMA_TO_BUS(int_ring_pa) >> 32);
    ep_ctx->w[4] = (uint32_t)int_ep_mps;
    __asm__ volatile("dsb sy" ::: "memory");

    // CONFIGURE_ENDPOINT command (type=12)
    uint32_t ce_ctrl = ((kbd_slot & 0xFFU) << 24) | (12U << 10);
    v66_cmd((uint32_t)(kictx_bus & 0xFFFFFFFFU),
            (uint32_t)(kictx_bus >> 32), 0U, ce_ctrl);
    xhci_d_puts("v66:cfg_ep cc="); xhci_d_dec(xhci_last_cc); xhci_d_puts("\n");
    if (xhci_last_cc != 1U) return 0;

    // ── Phase 9: Poll interrupt-IN for a real keypress ────────────────────────
    // Allocate NC buffer for HID boot report (use int_ep_mps or 8, whichever is larger)
    void *report_nc;
    unsigned long report_pa = alloc_dma(&report_nc);
    if (report_pa == 0) return 0;
    volatile uint8_t *report = (volatile uint8_t *)report_nc;
    unsigned int rlen = (int_ep_mps > 8U) ? int_ep_mps : 8U;

    unsigned int int_db_target = ctx_idx;
    xhci_d_puts("v66:int_db slot="); xhci_d_dec(kbd_slot);
    xhci_d_puts(" tgt="); xhci_d_dec(int_db_target); xhci_d_puts("\n");

    xhci_d_puts("v66:WAIT_KEY (press a key within 600s)\n");
    int got_key = 0;
    unsigned int key_cc = 0;
    unsigned int int_cycle = 1U;
    unsigned int int_enq = 0U;
    unsigned int cc36_count = 0U;

    // Queue first Normal TRB and ring doorbell
    int_ring[int_enq].param_lo = (uint32_t)(DMA_TO_BUS(report_pa) & 0xFFFFFFFFU);
    int_ring[int_enq].param_hi = (uint32_t)(DMA_TO_BUS(report_pa) >> 32);
    int_ring[int_enq].status   = rlen;
    __asm__ volatile("dmb st" ::: "memory");
    int_ring[int_enq].control  = (1U << 10) | (1U << 5) | int_cycle;
    __asm__ volatile("dsb sy" ::: "memory");
    XW32(xhci_dboff_val + kbd_slot * 4U, int_db_target);
    __asm__ volatile("dsb sy" ::: "memory");
    int_enq = 1U;

    for (int i = 0; i < 600000000 && !got_key; i++) {
        __asm__ volatile("dsb sy" ::: "memory");
        xhci_trb_t *trb = &evt_ring[evt_deq];
        if ((trb->control & 0x1U) == evt_cycle) {
            unsigned int tp = (trb->control >> 10) & 0x3FU;
            unsigned int cc = (trb->status >> 24) & 0x7FU;
            xhci_d_puts("v66:evt tp="); xhci_d_dec(tp);
            xhci_d_puts(" cc="); xhci_d_dec(cc); xhci_d_puts("\n");
            evt_deq++;
            if (evt_deq >= XHCI_EVT_RING_TRBS) { evt_deq = 0; evt_cycle ^= 1U; }
            XW64(xhci_rtsoff_val + 0x20U + 0x18U,
                 DMA_TO_BUS(evt_pa_g + (uint64_t)evt_deq * 16UL));
            __asm__ volatile("dsb sy" ::: "memory");
            if (tp == 32U) {
                if (cc == 1U || cc == 13U) {
                    // Success or Short Packet — check for non-zero valid keycode inline.
                    // If mps>=9 the device uses a 1-byte report-ID prefix; only accept
                    // report ID 0x01 (keyboard) and extract keycodes from bytes[3..8].
                    // If mps<9 use standard 8-byte boot-protocol layout (keycodes at [2..7]).
                    // Only accept keycodes in the valid HID range [0x04, 0xDD].
                    // A zero or out-of-range keycode (key-release, rollover sentinel, garbage)
                    // means re-queue and keep waiting.
                    __asm__ volatile("dsb sy" ::: "memory");
                    unsigned int kc_inline = 0;
                    if (int_ep_mps >= 9U) {
                        // Report-ID prefix likely present: byte[0]=report ID.
                        // Canonical HID keyboard report ID is 0x01 — keycodes at [3..8].
                        // For unknown report formats (non-boot interface), scan [2..rlen-1]
                        // broadly for any valid HID keycode in [0x04, 0xDD].
                        unsigned int scan_start = 2U;
                        unsigned int scan_end = (rlen < 32U) ? rlen : 32U;
                        if (report[0] == 0x01U) {
                            scan_start = 3U;
                            scan_end   = (rlen < 9U) ? rlen : 9U;
                        }
                        for (unsigned int ki = scan_start; ki < scan_end && kc_inline == 0; ki++) {
                            unsigned int kc = report[ki];
                            if (kc >= 0x04U && kc <= 0xDDU) kc_inline = kc;
                        }
                    } else {
                        // Standard 8-byte boot protocol: modifier[0], reserved[1], keycodes[2..7].
                        for (int ki = 2; ki < 8 && kc_inline == 0; ki++) {
                            unsigned int kc = report[ki];
                            if (kc >= 0x04U && kc <= 0xDDU) kc_inline = kc;
                        }
                    }
                    if (kc_inline != 0U) {
                        got_key = 1;
                        key_cc  = cc;
                    } else {
                        // Key-release or ZLP — re-queue at next slot and keep polling
                        if (int_enq >= 254U) {
                            int_enq = 0U;
                            int_cycle ^= 1U;
                            int_ring[255].control = (6U << 10) | (1U << 1) | int_cycle;
                            __asm__ volatile("dsb sy" ::: "memory");
                        }
                        int_ring[int_enq].param_lo = (uint32_t)(DMA_TO_BUS(report_pa) & 0xFFFFFFFFU);
                        int_ring[int_enq].param_hi = (uint32_t)(DMA_TO_BUS(report_pa) >> 32);
                        int_ring[int_enq].status   = rlen;
                        __asm__ volatile("dmb st" ::: "memory");
                        int_ring[int_enq].control  = (1U << 10) | (1U << 5) | int_cycle;
                        __asm__ volatile("dsb sy" ::: "memory");
                        XW32(xhci_dboff_val + kbd_slot * 4U, int_db_target);
                        __asm__ volatile("dsb sy" ::: "memory");
                        int_enq++;
                    }
                } else if (cc == 36U) {
                    // Split Transaction Error on VL805: endpoint stays Running (not Halted).
                    // RESET_ENDPOINT/SET_TR_DEQUEUE_POINTER both fail (cc=19 Context State Error).
                    // Correct recovery: advance enqueue to next slot and re-queue there.
                    cc36_count++;
                    // Wrap enqueue before slot 255 (Link TRB); toggle cycle on wrap.
                    if (int_enq >= 254U) {
                        int_enq = 0U;
                        int_cycle ^= 1U;
                        // Update Link TRB cycle bit to match new producer cycle state.
                        int_ring[255].control = (6U << 10) | (1U << 1) | int_cycle;
                        __asm__ volatile("dsb sy" ::: "memory");
                    }
                    int_ring[int_enq].param_lo = (uint32_t)(DMA_TO_BUS(report_pa) & 0xFFFFFFFFU);
                    int_ring[int_enq].param_hi = (uint32_t)(DMA_TO_BUS(report_pa) >> 32);
                    int_ring[int_enq].status   = rlen;
                    __asm__ volatile("dmb st" ::: "memory");
                    int_ring[int_enq].control  = (1U << 10) | (1U << 5) | int_cycle;
                    __asm__ volatile("dsb sy" ::: "memory");
                    XW32(xhci_dboff_val + kbd_slot * 4U, int_db_target);
                    __asm__ volatile("dsb sy" ::: "memory");
                    int_enq++;
                    if ((cc36_count % 10U) == 1U) {
                        xhci_d_puts("v66:cc36 n="); xhci_d_dec(cc36_count);
                        xhci_d_puts(" enq="); xhci_d_dec(int_enq); xhci_d_puts("\n");
                    }
                    xhci_udelay(5000);  // 5ms recovery between retries
                } // other errors: just continue waiting
            }
        }
        xhci_udelay(1);
    }
    xhci_d_puts("v66:got_key="); xhci_d_dec(got_key);
    xhci_d_puts(" cc="); xhci_d_dec(key_cc); xhci_d_puts("\n");
    if (!got_key) return 0;

    // Read HID report from NC buffer (print rlen bytes for full visibility)
    __asm__ volatile("dsb sy" ::: "memory");
    xhci_d_puts("v66:report=");
    for (unsigned int i = 0; i < rlen && i < 9U; i++) xhci_d_hex(report[i]);
    xhci_d_puts("\n");

    // Parse keycodes using the same logic as the inline loop check above.
    unsigned int keycode = 0;
    if (int_ep_mps >= 9U) {
        unsigned int scan_start = 2U;
        unsigned int scan_end = (rlen < 32U) ? rlen : 32U;
        if (report[0] == 0x01U) { scan_start = 3U; scan_end = (rlen < 9U) ? rlen : 9U; }
        for (unsigned int i = scan_start; i < scan_end && keycode == 0U; i++) {
            unsigned int kc = report[i];
            if (kc >= 0x04U && kc <= 0xDDU) keycode = kc;
        }
    } else {
        for (unsigned int i = 2; i < 8U && keycode == 0U; i++) {
            unsigned int kc = report[i];
            if (kc >= 0x04U && kc <= 0xDDU) keycode = kc;
        }
    }
    if (keycode == 0U) return 0;

    kbd_keycode_val = keycode;
    kbd_char_val    = hid_keycode_to_char(keycode);
    kbd_ok_val      = 1;
    return 1;
}

int          kernel_kbd_ok(void)      { return kbd_ok_val;      }
unsigned int kernel_kbd_keycode(void) { return kbd_keycode_val; }
unsigned int kernel_kbd_char(void)    { return kbd_char_val;    }
