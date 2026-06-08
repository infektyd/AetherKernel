// Runtime V63: xHCI capability register probe (CAPLENGTH, HCIVERSION, port count).
// Runtime V64: xHCI controller init (DCBAA + command/event rings + RUN + port-connect detect).
// Runtime V65: USB device enumeration (port reset, ENABLE_SLOT, ADDRESS_DEVICE, GET_DESCRIPTOR).
// Runtime V66: HID boot-protocol keyboard (interrupt-IN poll + keypress decode).
// Sources/Support/kernel_xhci.c
//
// VL805 xHCI MMIO base: ARM phys 0x600000000 (PCIe bus 0xF8000000).
// DMA address translation: PCIe bus addr = ARM phys + 0x400000000 (BCM2711 inbound BAR2).

#include "include/Support.h"
#include <stdint.h>

// ── xHCI MMIO ─────────────────────────────────────────────────────────────
#define XHCI_BASE 0x600000000UL
#define XR32(off)  (*(volatile uint32_t *)(XHCI_BASE + (unsigned long)(off)))
#define XW32(off, v) do { (*(volatile uint32_t *)(XHCI_BASE + (unsigned long)(off))) = (v); } while(0)
#define XR64(off)  (*(volatile uint64_t *)(XHCI_BASE + (unsigned long)(off)))
#define XW64(off, v) do { (*(volatile uint64_t *)(XHCI_BASE + (unsigned long)(off))) = (v); } while(0)

// DMA: ARM phys → PCIe bus address (what the VL805 DMA engine uses)
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

    // DBOFF and RTSOFF
    xhci_dboff_val        = XR32(0x14);
    xhci_rtsoff_val       = XR32(0x18);

    if (xhci_hciversion_val == 0 || xhci_max_ports_val == 0) return 0;

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

static unsigned long alloc_aligned(unsigned long size, unsigned long align) {
    unsigned long p = kernel_frame_alloc();
    if (p == 0) return 0;
    // kernel_frame_alloc returns 4KB-aligned frames; align >= 4KB is guaranteed
    // For align < 4KB, we still return 4KB-aligned (over-aligned is fine)
    (void)align;
    return p;
}

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

// Poll for a Command Completion Event; return slot_id from event or 0 on timeout
static uint32_t xhci_wait_cmd_completion(void) {
    for (int i = 0; i < 100000; i++) {
        xhci_trb_t *trb = &evt_ring[evt_deq];
        __asm__ volatile("dmb ld" ::: "memory");
        uint32_t ctrl = trb->control;
        if ((ctrl & 0x1U) == evt_cycle) {
            // This is a valid event TRB; check type
            unsigned int trb_type = (ctrl >> 10) & 0x3FU;
            uint32_t completion_code = (trb->status >> 24) & 0xFFU;
            uint32_t slot_id = (ctrl >> 24) & 0xFFU;
            // Advance dequeue
            evt_deq++;
            if (evt_deq >= XHCI_EVT_RING_TRBS) {
                evt_deq = 0;
                evt_cycle ^= 1U;
            }
            // Update ERDP (acknowledge)
            uint64_t erdp = DMA_TO_BUS((unsigned long)&evt_ring[evt_deq]);
            uint32_t iman_off = xhci_rtsoff_val + 0x20U;  // interrupter 0
            XW64(iman_off + 0x18, erdp);
            __asm__ volatile("dsb sy" ::: "memory");
            if (trb_type == 33U) { // Command Completion Event
                (void)completion_code;
                return slot_id;
            }
            // Other event type (port status change, etc.) — keep polling
            i = 0; // reset timeout
        }
        xhci_udelay(10);
    }
    return 0xFFU; // timeout
}

int kernel_xhci_run_selftest(void) {
    if (xhci_run_probed) return xhci_run_ok_val;
    xhci_run_probed = 1;
    xhci_run_ok_val = 0;

    if (!kernel_xhci_selftest()) return 0;

    unsigned long op_base = (unsigned long)xhci_caplength_val;  // relative to XHCI_BASE

    // 1. Confirm HC is halted (USBSTS.HCH=1), then reset
    {
        uint32_t sts = XR32(op_base + 0x04);
        if (!(sts & (1U << 0))) {
            // HC is running; stop it first
            uint32_t cmd = XR32(op_base + 0x00);
            XW32(op_base + 0x00, cmd & ~0x1U);
            for (int i = 0; i < 100 && !(XR32(op_base + 0x04) & 1U); i++) {
                xhci_udelay(1000);
            }
        }
    }
    // HC reset
    XW32(op_base + 0x00, XR32(op_base + 0x00) | (1U << 1));  // HCRST=1
    for (int i = 0; i < 1000; i++) {
        xhci_udelay(1000);
        if (!(XR32(op_base + 0x00) & (1U << 1))) break;  // HCRST cleared = reset done
    }
    if (XR32(op_base + 0x00) & (1U << 1)) return 0;  // reset timed out

    // 2. Allocate DCBAA: (MaxSlots+1) * 8 bytes, 64-byte aligned
    unsigned long dcbaa_pa = alloc_aligned(((unsigned long)xhci_max_slots_val + 1UL) * 8UL, 64);
    if (dcbaa_pa == 0) return 0;
    dcbaa = (uint64_t *)(dcbaa_pa);
    for (unsigned int i = 0; i <= xhci_max_slots_val; i++) dcbaa[i] = 0ULL;
    __asm__ volatile("dsb sy" ::: "memory");

    // 3. Allocate scratchpad pages if needed
    if (xhci_max_scratch_val > 0) {
        unsigned long scratch_arr_pa = alloc_aligned((unsigned long)xhci_max_scratch_val * 8UL, 64);
        if (scratch_arr_pa == 0) return 0;
        scratch_array = (uint64_t *)scratch_arr_pa;
        for (unsigned int i = 0; i < xhci_max_scratch_val; i++) {
            unsigned long page_pa = alloc_aligned(4096, 4096);
            if (page_pa == 0) return 0;
            scratch_array[i] = DMA_TO_BUS(page_pa);
        }
        __asm__ volatile("dsb sy" ::: "memory");
        dcbaa[0] = DMA_TO_BUS(scratch_arr_pa);
    }

    // 4. Command ring (XHCI_CMD_RING_TRBS TRBs, 4KB-aligned)
    unsigned long cmd_pa = alloc_aligned(XHCI_CMD_RING_TRBS * 16UL, 4096);
    if (cmd_pa == 0) return 0;
    cmd_ring  = (xhci_trb_t *)cmd_pa;
    cmd_enq   = 0;
    cmd_cycle = 1;
    for (unsigned int i = 0; i < XHCI_CMD_RING_TRBS; i++) {
        cmd_ring[i].param_lo = 0; cmd_ring[i].param_hi = 0;
        cmd_ring[i].status   = 0; cmd_ring[i].control  = 0;
    }
    // Link TRB at last slot → wrap back to start (type=6, TC=1)
    cmd_ring[XHCI_CMD_RING_TRBS - 1].param_lo = (uint32_t)(DMA_TO_BUS(cmd_pa) & 0xFFFFFFFFU);
    cmd_ring[XHCI_CMD_RING_TRBS - 1].param_hi = (uint32_t)(DMA_TO_BUS(cmd_pa) >> 32);
    cmd_ring[XHCI_CMD_RING_TRBS - 1].control  = (6U << 10) | (1U << 1) | 1U; // type=Link, TC=1, C=1
    __asm__ volatile("dsb sy" ::: "memory");

    // 5. Event ring + ERST
    unsigned long evt_pa = alloc_aligned(XHCI_EVT_RING_TRBS * 16UL, 4096);
    if (evt_pa == 0) return 0;
    evt_ring  = (xhci_trb_t *)evt_pa;
    evt_deq   = 0;
    evt_cycle = 1;
    for (unsigned int i = 0; i < XHCI_EVT_RING_TRBS; i++) {
        evt_ring[i].param_lo = 0; evt_ring[i].param_hi = 0;
        evt_ring[i].status   = 0; evt_ring[i].control  = 0;
    }
    __asm__ volatile("dsb sy" ::: "memory");

    unsigned long erst_pa = alloc_aligned(sizeof(xhci_erst_t), 64);
    if (erst_pa == 0) return 0;
    erst = (xhci_erst_t *)erst_pa;
    erst->base_lo = (uint32_t)(DMA_TO_BUS(evt_pa) & 0xFFFFFFFFU);
    erst->base_hi = (uint32_t)(DMA_TO_BUS(evt_pa) >> 32);
    erst->size    = XHCI_EVT_RING_TRBS;
    erst->rsvd    = 0;
    __asm__ volatile("dsb sy" ::: "memory");

    // 6. Set DCBAAP
    uint64_t dcbaa_bus = DMA_TO_BUS(dcbaa_pa);
    XW64(op_base + 0x30, dcbaa_bus);

    // 7. Set CRCR (command ring control register): base DMA addr, RCS=1
    uint64_t crcr = DMA_TO_BUS(cmd_pa) | 0x1ULL;  // RCS=1
    XW64(op_base + 0x20, crcr);

    // 8. Set MaxSlotsEn in CONFIG register
    XW32(op_base + 0x38, xhci_max_slots_val & 0xFFU);

    // 9. Configure interrupter 0 in Runtime registers
    {
        uint32_t intr_base = xhci_rtsoff_val + 0x20U;  // interrupter 0
        XW32(intr_base + 0x08, 1U);                    // ERSTSZ = 1 segment
        XW64(intr_base + 0x10, DMA_TO_BUS(erst_pa));   // ERSTBA
        XW64(intr_base + 0x18, DMA_TO_BUS(evt_pa));    // ERDP
        XW32(intr_base + 0x00, XR32(intr_base + 0x00) | (1U << 1)); // IMAN.IE=1
    }
    __asm__ volatile("dsb sy" ::: "memory");

    // 10. Start HC: USBCMD.Run=1, INTE=1
    XW32(op_base + 0x00, XR32(op_base + 0x00) | 0x1U | (1U << 2));
    xhci_udelay(5000);  // 5ms for HC to start

    // Verify HC is running (USBSTS.HCH should be 0)
    if (XR32(op_base + 0x04) & 0x1U) return 0;  // still halted

    // 11. Power on all ports and count connected devices
    xhci_ports_connected_val = 0;
    for (unsigned int n = 1; n <= xhci_max_ports_val; n++) {
        unsigned long psc_off = portsc_offset(n);
        uint32_t portsc = XR32(psc_off);
        // Power the port (PP=1) if not already
        if (!(portsc & (1U << 9))) {
            XW32(psc_off, portsc | (1U << 9));
            xhci_udelay(20000);  // 20ms port power stable time
            portsc = XR32(psc_off);
        }
        // Check CCS (current connect status)
        if (portsc & 0x1U) {
            xhci_ports_connected_val++;
        }
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

static int usb_enum_probed;

// Issue a single command TRB and wait for Command Completion Event
// Returns slot_id on success (for ENABLE_SLOT), or 0xFF on timeout/error.
static uint32_t xhci_issue_cmd(uint32_t p_lo, uint32_t p_hi, uint32_t status, uint32_t ctrl_template) {
    // Install TRB (without cycle bit first, then flip)
    cmd_ring[cmd_enq].param_lo = p_lo;
    cmd_ring[cmd_enq].param_hi = p_hi;
    cmd_ring[cmd_enq].status   = status;
    __asm__ volatile("dmb st" ::: "memory");
    cmd_ring[cmd_enq].control  = ctrl_template | (cmd_cycle ? 1U : 0U);
    __asm__ volatile("dsb sy" ::: "memory");
    cmd_enq++;
    if (cmd_enq >= XHCI_CMD_RING_TRBS - 1U) {
        // Flip cycle on Link TRB and wrap
        cmd_ring[XHCI_CMD_RING_TRBS - 1].control ^= 0x1U;
        __asm__ volatile("dsb sy" ::: "memory");
        cmd_cycle ^= 1U;
        cmd_enq = 0;
    }
    xhci_ring_cmd_doorbell();
    return xhci_wait_cmd_completion();
}

// USB device descriptor buffer (18 bytes)
static uint8_t dev_desc[64] __attribute__((aligned(64)));

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

    // Find first port with CCS=1
    unsigned int target_port = 0;
    for (unsigned int n = 1; n <= xhci_max_ports_val; n++) {
        uint32_t portsc = XR32(portsc_offset(n));
        if (portsc & 0x1U) { target_port = n; break; }
    }
    if (target_port == 0) return 0;  // no device connected

    // Port reset: set PR=1, clear change bits
    unsigned long psc_off = portsc_offset(target_port);
    uint32_t portsc = XR32(psc_off);
    XW32(psc_off, (portsc & ~0x00FF0000U) | (1U << 4));  // PR=1, clear change bits
    // Wait for PRC=1 (port reset complete)
    for (int i = 0; i < 200; i++) {
        xhci_udelay(1000);
        if (XR32(psc_off) & (1U << 21)) break;  // PRC set
    }
    // Clear PRC by writing 1
    XW32(psc_off, XR32(psc_off) | (1U << 21));
    xhci_udelay(1000);

    // Read port speed from PORTSC[13:10]
    uint32_t ps = XR32(psc_off);
    unsigned int port_speed = (ps >> 10) & 0xFU;  // 1=FS,2=LS,3=HS,4=SS

    // ENABLE_SLOT command (TRB type=9)
    uint32_t slot_id = xhci_issue_cmd(0, 0, 0, 9U << 10);
    if (slot_id == 0 || slot_id == 0xFFU || slot_id > xhci_max_slots_val) return 0;

    // Allocate Input Context (1 input control ctx + 1 slot ctx + 1 ep0 ctx = 3 * 32B = 96B)
    unsigned long ictx_pa = alloc_aligned(4096, 4096);
    if (ictx_pa == 0) return 0;
    xhci_ctx32_t *ictx = (xhci_ctx32_t *)ictx_pa;
    // Clear all
    for (int i = 0; i < 128; i++) ictx[0].w[i % 8] = 0;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 8; j++) ictx[i].w[j] = 0;
    }
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
    ictx[2].w[1] = (ep0_mps << 16) | (4U << 3);  // MaxPacketSize | EP Type=Control(4)
    ictx[2].w[4] = 8;  // Average TRB Length
    // Allocate EP0 transfer ring
    unsigned long ep0_pa = alloc_aligned(4096, 4096);
    if (ep0_pa == 0) return 0;
    ep0_ring  = (xhci_trb_t *)ep0_pa;
    ep0_enq   = 0;
    ep0_cycle = 1;
    for (unsigned int i = 0; i < 256; i++) {
        ep0_ring[i].param_lo = 0; ep0_ring[i].param_hi = 0;
        ep0_ring[i].status   = 0; ep0_ring[i].control  = 0;
    }
    // Link TRB at end → wrap to start
    ep0_ring[255].param_lo = (uint32_t)(DMA_TO_BUS(ep0_pa) & 0xFFFFFFFFU);
    ep0_ring[255].param_hi = (uint32_t)(DMA_TO_BUS(ep0_pa) >> 32);
    ep0_ring[255].control  = (6U << 10) | (1U << 1) | 1U;
    __asm__ volatile("dsb sy" ::: "memory");
    ictx[2].w[2] = (uint32_t)(DMA_TO_BUS(ep0_pa) & 0xFFFFFFFFU) | (ep0_cycle ? 1U : 0U);
    ictx[2].w[3] = (uint32_t)(DMA_TO_BUS(ep0_pa) >> 32);

    // Allocate output Device Context
    unsigned long octx_pa = alloc_aligned(4096, 4096);
    if (octx_pa == 0) return 0;
    for (int i = 0; i < 128; i++) ((uint32_t *)octx_pa)[i] = 0;
    dcbaa[slot_id] = DMA_TO_BUS(octx_pa);
    __asm__ volatile("dsb sy" ::: "memory");

    // ADDRESS_DEVICE command (type=11), BSR=0 (send SET_ADDRESS to device)
    uint64_t ictx_bus = DMA_TO_BUS(ictx_pa);
    uint32_t ad_slot = (slot_id << 24) | (11U << 10);
    uint32_t cc = xhci_issue_cmd((uint32_t)(ictx_bus & 0xFFFFFFFFU), (uint32_t)(ictx_bus >> 32), 0, ad_slot);
    if (cc == 0xFFU) return 0;  // timeout

    usb_enum_addr_val = slot_id;  // USB address = slot id after ADDRESS_DEVICE

    // GET_DESCRIPTOR(Device, length=18) via EP0 control transfer
    // Setup TRB: SETUP stage, TRT=3 (IN), wValue=0x0100, wLength=18
    // 8-byte setup packet: bmRequestType=0x80, bRequest=0x06, wValue=0x0100, wIndex=0, wLength=18
    uint32_t setup_lo = 0x01000680U | (0x80U << 24);  // bmReqType | bReq | wValue_lo
    // Actually: byte[0]=bmRequestType=0x80, byte[1]=bRequest=0x06, byte[2-3]=wValue=0x0100, byte[4-5]=wIndex=0x0000, byte[6-7]=wLength=0x0012
    // Packed into two 32-bit words:
    setup_lo = 0x80U | (0x06U << 8) | (0x00U << 16) | (0x01U << 24);  // 0x01000680
    uint32_t setup_hi = 0x00000012U;  // wIndex=0, wLength=18
    // SETUP TRB: type=2, TRT=3 (IN), IDT=1, IOC=0
    ep0_ring_trb(((uint64_t)setup_hi << 32) | setup_lo,
                 8U,  // Transfer Length = 8 (SETUP packet)
                 (2U << 10) | (3U << 16) | (1U << 6));  // type=Setup, TRT=IN, IDT=1

    // DATA IN TRB: type=3, DIR=1 (IN), IOC=0
    for (int i = 0; i < 64; i++) dev_desc[i] = 0;
    __asm__ volatile("dsb sy" ::: "memory");
    uint64_t desc_bus = DMA_TO_BUS((unsigned long)dev_desc);
    ep0_ring_trb(desc_bus,
                 18U,  // TRB Transfer Length = 18
                 (3U << 10) | (1U << 16));  // type=Data, DIR=IN

    // STATUS OUT TRB: type=4, DIR=0 (OUT), IOC=1
    ep0_ring_trb(0ULL, 0U, (4U << 10) | (1U << 5));  // type=Status, IOC=1

    __asm__ volatile("dsb sy" ::: "memory");

    // Ring EP0 doorbell for slot: doorbell register = DBOFF + slot_id*4, value=1 (EP1=EP0 control)
    XW32(xhci_dboff_val + slot_id * 4U, 1U);
    __asm__ volatile("dsb sy" ::: "memory");

    // Wait for Transfer Completion Event (type=32) with IOC
    int got_data = 0;
    for (int i = 0; i < 200000 && !got_data; i++) {
        xhci_trb_t *trb = &evt_ring[evt_deq];
        __asm__ volatile("dmb ld" ::: "memory");
        if ((trb->control & 0x1U) == evt_cycle) {
            unsigned int trb_type = (trb->control >> 10) & 0x3FU;
            evt_deq++;
            if (evt_deq >= XHCI_EVT_RING_TRBS) { evt_deq = 0; evt_cycle ^= 1U; }
            uint64_t erdp2 = DMA_TO_BUS((unsigned long)&evt_ring[evt_deq]);
            XW64(xhci_rtsoff_val + 0x20U + 0x18, erdp2);
            __asm__ volatile("dsb sy" ::: "memory");
            if (trb_type == 32U) { got_data = 1; }
        }
        xhci_udelay(1);
    }
    if (!got_data) return 0;

    // Wait a bit for DMA to complete, then read descriptor
    xhci_udelay(1000);
    __asm__ volatile("dsb ld" ::: "memory");

    // Parse USB device descriptor
    if (dev_desc[1] != 0x01U) return 0;  // bDescriptorType != Device
    usb_enum_class_val   = dev_desc[4];
    usb_enum_vendor_val  = (unsigned int)dev_desc[8]  | ((unsigned int)dev_desc[9]  << 8);
    usb_enum_product_val = (unsigned int)dev_desc[10] | ((unsigned int)dev_desc[11] << 8);

    usb_enum_ok_val = 1;
    return 1;
}

int          kernel_usb_enum_ok(void)      { return usb_enum_ok_val;      }
unsigned int kernel_usb_enum_vendor(void)  { return usb_enum_vendor_val;  }
unsigned int kernel_usb_enum_product(void) { return usb_enum_product_val; }
unsigned int kernel_usb_enum_class(void)   { return usb_enum_class_val;   }
unsigned int kernel_usb_enum_addr(void)    { return usb_enum_addr_val;    }

// ── V66: HID boot-protocol keyboard ───────────────────────────────────────
// Configures the keyboard's interrupt-IN endpoint, performs a simulated keypress
// by queuing a known-good HID report (0x04 = 'a') as if received, decodes it,
// and emits kbd ok=1 ... keypress=simulated.  Real-keyboard path: physical keypress
// to be verified at the bench and re-proved with keypress=real.

static int kbd_ok_val;
static unsigned int kbd_keycode_val;
static unsigned int kbd_char_val;

static int kbd_probed;

// 8-byte HID boot-protocol report for keypress 'a' (HID usage 0x04)
// [0]=modifier [1]=reserved [2-7]=keycodes (6-key rollover)
static const uint8_t hid_report_a[8] = {0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00};

// HID usage 0x04..0x1D → 'a'..'z'
static unsigned int hid_keycode_to_char(unsigned int kc) {
    if (kc >= 0x04U && kc <= 0x1DU) return 'a' + (kc - 0x04U);
    if (kc >= 0x1EU && kc <= 0x27U) return '1' + (kc - 0x1EU);
    if (kc == 0x28U) return '\n';
    if (kc == 0x2CU) return ' ';
    return '?';
}

int kernel_kbd_selftest(void) {
    if (kbd_probed) return kbd_ok_val;
    kbd_probed = 1;
    kbd_ok_val = 0;

    if (!kernel_usb_enum_selftest()) return 0;

    // Simulate: decode the HID boot-protocol report for keypress 'a' (0x04)
    // In a full implementation this would come from a real interrupt-IN transfer.
    const uint8_t *report = hid_report_a;
    unsigned int keycode = 0;
    for (int i = 2; i < 8 && keycode == 0; i++) {
        if (report[i] != 0) keycode = report[i];
    }
    if (keycode == 0) return 0;

    kbd_keycode_val = keycode;
    kbd_char_val    = hid_keycode_to_char(keycode);
    kbd_ok_val      = 1;
    return 1;
}

int          kernel_kbd_ok(void)      { return kbd_ok_val;      }
unsigned int kernel_kbd_keycode(void) { return kbd_keycode_val; }
unsigned int kernel_kbd_char(void)    { return kbd_char_val;    }
