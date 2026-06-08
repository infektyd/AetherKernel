// kernel_vc_mbox.c
// BCM2711 VideoCore ARM->GPU property mailbox (channel 8).
// V58: firmware revision probe.  V59: framebuffer allocation.

#include "include/Support.h"
#include <stdint.h>

// ---------------------------------------------------------------------------
// Registers (BCM2711 peripheral base 0xFE000000)
// ---------------------------------------------------------------------------
#define MBOX_BASE       ((volatile uint32_t *)0xFE00B880UL)
#define MBOX_READ       (MBOX_BASE + 0U)    // offset 0x00
#define MBOX_STATUS     (MBOX_BASE + 6U)    // offset 0x18
#define MBOX_WRITE      (MBOX_BASE + 8U)    // offset 0x20

#define MBOX_EMPTY      0x40000000U         // no pending response
#define MBOX_FULL       0x80000000U         // send FIFO full
#define MBOX_CH_PROP    8U                  // property tags channel

#define MBOX_REQ        0x00000000U
#define MBOX_RESP_OK    0x80000000U
#define TAG_FW_REV      0x00000001U
#define TAG_SET_PHYS_WH 0x00048003U
#define TAG_SET_VIRT_WH 0x00048004U
#define TAG_SET_DEPTH   0x00048005U
#define TAG_SET_VOFFSET 0x00048009U
#define TAG_ALLOC_BUF   0x00040001U
#define TAG_GET_PITCH   0x00040008U
#define TAG_END         0x00000000U

// 16-byte aligned, 48 words (192 bytes) — fits both FW_REV (7w) and FB alloc (35w)
static volatile uint32_t __attribute__((aligned(16))) vc_buf[48];

static void vc_cache_flush(void) {
    unsigned long start = (unsigned long)(uintptr_t)vc_buf & ~63UL;
    unsigned long end   = start + 192UL;     // covers 48 words
    unsigned long p;
    for (p = start; p < end; p += 64UL) {
        __asm__ volatile("dc civac, %0" :: "r"(p) : "memory");
    }
    __asm__ volatile("dsb sy" ::: "memory");
}

static int vc_call(unsigned int bytes) {
    uint32_t addr = (uint32_t)(uintptr_t)vc_buf;
    uint32_t msg  = (addr & ~0xFU) | MBOX_CH_PROP;

    vc_cache_flush();

    unsigned int s = 200000U;
    while ((*MBOX_STATUS & MBOX_FULL) && s--) { __asm__ volatile("nop"); }
    if (!s) return 0;

    *MBOX_WRITE = msg;

    s = 2000000U;
    while (s--) {
        while ((*MBOX_STATUS & MBOX_EMPTY) && s--) { __asm__ volatile("nop"); }
        uint32_t r = *MBOX_READ;
        if ((r & 0xFU) == MBOX_CH_PROP) {
            vc_cache_flush();
            return (vc_buf[1] & MBOX_RESP_OK) ? 1 : 0;
        }
    }
    return 0;
    (void)bytes;
}

// ---------------------------------------------------------------------------
// V58 state
// ---------------------------------------------------------------------------
static uint32_t vc_fw_rev;

int kernel_vc_mbox_probe(void) {
    vc_buf[0] = 7U * 4U;
    vc_buf[1] = MBOX_REQ;
    vc_buf[2] = TAG_FW_REV;
    vc_buf[3] = 4U;
    vc_buf[4] = 0U;
    vc_buf[5] = 0U;
    vc_buf[6] = TAG_END;

    if (!vc_call(28U)) { vc_fw_rev = 0U; return 0; }
    vc_fw_rev = vc_buf[5];
    return (vc_fw_rev != 0U) ? 1 : 0;
}

unsigned long kernel_vc_mbox_fw_rev(void) { return (unsigned long)vc_fw_rev; }
int kernel_vc_mbox_selftest(void)          { return (vc_fw_rev != 0U) ? 1 : 0; }

// ---------------------------------------------------------------------------
// V59 — framebuffer allocation
// Step 1: GET_PHYS_WH to query actual firmware display dimensions.
// Step 2: SET_PHYS_WH + SET_VIRT_WH + SET_VIRT_OFFSET + SET_DEPTH
//         + ALLOC_BUF + GET_PITCH using the queried dimensions.
// ---------------------------------------------------------------------------
#define TAG_GET_PHYS_WH 0x00040003U
#define FB_BPP  32U

static uint32_t fb_width;
static uint32_t fb_height;
static uint32_t fb_depth;
static uint32_t fb_pitch;
static uint32_t fb_addr;    // ARM physical address (bus addr masked)
static uint32_t fb_size;

// Diagnostic state from the most recent fb_alloc attempt.
static int      fb_last_call_ok = -1;   // -1=never, 0=vc_call timeout, 1=ok
static uint32_t fb_last_resp    = 0;    // vc_buf[1] (global response code)
static uint32_t fb_raw_addr     = 0;    // raw ALLOC_BUF addr response
static uint32_t fb_raw_pitch    = 0;    // raw GET_PITCH response
static uint32_t fb_query_w      = 0;    // GET_PHYS_WH queried width
static uint32_t fb_query_h      = 0;    // GET_PHYS_WH queried height

int kernel_vc_mbox_fb_alloc(void) {
    // Step 1: query actual display dimensions from firmware.
    vc_buf[0] = 8U * 4U;           // 32 bytes
    vc_buf[1] = MBOX_REQ;
    vc_buf[2] = TAG_GET_PHYS_WH;
    vc_buf[3] = 8U;
    vc_buf[4] = 0U;
    vc_buf[5] = 0U;
    vc_buf[6] = 0U;
    vc_buf[7] = TAG_END;

    int qok = vc_call(32U);
    fb_last_call_ok = qok;
    fb_last_resp    = vc_buf[1];
    fb_query_w      = vc_buf[5];
    fb_query_h      = vc_buf[6];

    if (!qok || fb_query_w == 0U || fb_query_h == 0U) {
        fb_raw_addr  = 0U;
        fb_raw_pitch = 0U;
        return 0;
    }

    // Step 2: allocate framebuffer with firmware-queried dimensions.
    vc_buf[0]  = 31U * 4U;         // 124 bytes
    vc_buf[1]  = MBOX_REQ;
    // SET_PHYS_WH
    vc_buf[2]  = TAG_SET_PHYS_WH;
    vc_buf[3]  = 8U;
    vc_buf[4]  = 0U;
    vc_buf[5]  = fb_query_w;
    vc_buf[6]  = fb_query_h;
    // SET_VIRT_WH
    vc_buf[7]  = TAG_SET_VIRT_WH;
    vc_buf[8]  = 8U;
    vc_buf[9]  = 0U;
    vc_buf[10] = fb_query_w;
    vc_buf[11] = fb_query_h;
    // SET_VIRT_OFFSET
    vc_buf[12] = TAG_SET_VOFFSET;
    vc_buf[13] = 8U;
    vc_buf[14] = 0U;
    vc_buf[15] = 0U;
    vc_buf[16] = 0U;
    // SET_DEPTH
    vc_buf[17] = TAG_SET_DEPTH;
    vc_buf[18] = 4U;
    vc_buf[19] = 0U;
    vc_buf[20] = FB_BPP;
    // ALLOC_BUF: alignment=4096; response → addr@[24], size@[25]
    vc_buf[21] = TAG_ALLOC_BUF;
    vc_buf[22] = 8U;
    vc_buf[23] = 0U;
    vc_buf[24] = 4096U;
    vc_buf[25] = 0U;
    // GET_PITCH: response → pitch@[29]
    vc_buf[26] = TAG_GET_PITCH;
    vc_buf[27] = 4U;
    vc_buf[28] = 0U;
    vc_buf[29] = 0U;
    // END
    vc_buf[30] = TAG_END;
    vc_buf[31] = 0U;
    vc_buf[32] = 0U;
    vc_buf[33] = 0U;
    vc_buf[34] = 0U;

    int aok = vc_call(35U * 4U);
    fb_last_call_ok = aok;
    fb_last_resp    = vc_buf[1];
    fb_raw_addr     = vc_buf[24];
    fb_raw_pitch    = vc_buf[29];

    if (!aok) return 0;

    uint32_t bus_addr = vc_buf[24];
    uint32_t size     = vc_buf[25];
    uint32_t pitch    = vc_buf[29];

    if (bus_addr == 0U || size == 0U || pitch == 0U) return 0;

    fb_addr   = bus_addr & 0x3FFFFFFFU;
    fb_size   = size;
    fb_width  = fb_query_w;
    fb_height = fb_query_h;
    fb_depth  = FB_BPP;
    fb_pitch  = pitch;
    return 1;
}

unsigned long kernel_vc_mbox_fb_width(void)       { return (unsigned long)fb_width; }
unsigned long kernel_vc_mbox_fb_height(void)      { return (unsigned long)fb_height; }
unsigned long kernel_vc_mbox_fb_depth(void)       { return (unsigned long)fb_depth; }
unsigned long kernel_vc_mbox_fb_pitch(void)       { return (unsigned long)fb_pitch; }
unsigned long kernel_vc_mbox_fb_addr(void)        { return (unsigned long)fb_addr; }
unsigned long kernel_vc_mbox_fb_size(void)        { return (unsigned long)fb_size; }
int kernel_vc_mbox_fb_selftest(void)              { return (fb_addr != 0U && fb_pitch != 0U) ? 1 : 0; }
// Diagnostic accessors (valid after any fb_alloc call, ok or not).
int           kernel_vc_mbox_fb_last_call_ok(void)  { return fb_last_call_ok; }
unsigned long kernel_vc_mbox_fb_last_resp(void)     { return (unsigned long)fb_last_resp; }
unsigned long kernel_vc_mbox_fb_raw_addr(void)      { return (unsigned long)fb_raw_addr; }
unsigned long kernel_vc_mbox_fb_raw_pitch(void)     { return (unsigned long)fb_raw_pitch; }
unsigned long kernel_vc_mbox_fb_query_w(void)       { return (unsigned long)fb_query_w; }
unsigned long kernel_vc_mbox_fb_query_h(void)       { return (unsigned long)fb_query_h; }

// RPI_FIRMWARE_NOTIFY_XHCI_RESET (0x00030058)
// Tells VideoCore to reload VL805 firmware after Pi firmware asserted PERST#
// during OS handoff without reloading the firmware blob. Without this call,
// VL805 is in ROM-only state: config TLPs work but MMIO (memory TLPs) do not.
#define TAG_NOTIFY_XHCI_RESET 0x00030058U

static int xhci_reset_result = -1;
static uint32_t xhci_reset_payload = 0xFFFFFFFFU;  // vc_buf[5] after call (0=VC success, else error)

int kernel_vc_mbox_notify_xhci_reset(void) {
    // Linux passes a 4-byte PCI device address: bus<<20 | slot<<15 | fn<<12.
    // VL805 is at bus=1, dev=0, fn=0 → 0x00100000.
    // Without the device address, Pi firmware doesn't know which controller
    // to reload MCU firmware for, so it returns ok=1 but does nothing.
    vc_buf[0] = 8U * 4U;    // 32 bytes total
    vc_buf[1] = MBOX_REQ;
    vc_buf[2] = TAG_NOTIFY_XHCI_RESET;
    vc_buf[3] = 4U;          // value buffer size: 4 bytes
    vc_buf[4] = 4U;          // request length: 4 bytes
    vc_buf[5] = 0x00100000U; // PCI addr: bus=1, dev=0, fn=0
    vc_buf[6] = TAG_END;
    vc_buf[7] = 0U;          // padding to 32-byte alignment
    xhci_reset_result = vc_call(32U);
    xhci_reset_payload = vc_buf[5];  // response value from Pi firmware
    return xhci_reset_result;
}

int kernel_vc_mbox_xhci_reset_ok(void)      { return xhci_reset_result;  }
uint32_t kernel_vc_mbox_xhci_reset_payload(void) { return xhci_reset_payload; }

// RPI_FIRMWARE_SET_POWER_STATE (0x00028003)
// Re-enables a BCM2711 power domain disabled by Pi firmware during OS handoff.
// device_id=3 (USB HCD): Pi firmware's XHCI_STOP calls SET_POWER_STATE(3, OFF)
// which disables the BCM2711 AXI system-bus routing for 0x600000000 → PCIe RC.
// ARM MMIO reads at VL805 BAR0 return 0xDEADDEAD until this is re-enabled,
// regardless of how WIN0 and MISC_CTRL registers are programmed.
// state bits: bit0=on/off, bit1=wait for transition.  state=3 = ON + wait.
#define TAG_SET_POWER_STATE 0x00028003U

static int pwr_state_result = -1;
static uint32_t pwr_state_response = 0xFFFFFFFFU;

int kernel_vc_mbox_set_power_state(unsigned int device_id, unsigned int state) {
    vc_buf[0] = 8U * 4U;           // 32 bytes total
    vc_buf[1] = MBOX_REQ;
    vc_buf[2] = TAG_SET_POWER_STATE;
    vc_buf[3] = 8U;                // value buffer size: 8 bytes
    vc_buf[4] = 8U;                // request length: 8 bytes
    vc_buf[5] = (uint32_t)device_id;
    vc_buf[6] = (uint32_t)state;
    vc_buf[7] = TAG_END;
    pwr_state_result   = vc_call(32U);
    pwr_state_response = vc_buf[6];  // bit0=on, bit1=device_exists
    return pwr_state_result;
}

int          kernel_vc_mbox_pwr_state_result(void)   { return pwr_state_result;                  }
unsigned int kernel_vc_mbox_pwr_state_response(void) { return (unsigned int)pwr_state_response;  }

// RPI_FIRMWARE_SET_RESETS (0x00030042)
// Controls BCM2711 hardware reset domains managed by Pi firmware.
// RASPBERRYPI_FIRMWARE_RESET_ID_USB   = 0 (from dt-binding header)
// RASPBERRYPI_FIRMWARE_RESET_ID_PCIE0 = 1 (from dt-binding header)
// state=0 = deassert (take out of reset); state=1 = assert (hold in reset).
// Linux reset driver: assert → packet[1]=1, deassert → packet[1]=0.
// Linux calls reset_control_deassert(pcie->reset) as first step in pcie-brcmstb.c.
// Without this call (state=0 for PCIE0), the BCM2711 AXI→PCIe translation
// bridge fabric remains gated after link training — ARM reads at 0x600000000
// return 0xDEADDEAD in 0 ticks (AXI intercept) despite WIN0 being correct.
#define TAG_SET_RESETS 0x00030042U

static int pcie_reset_result = -1;
static uint32_t pcie_reset_response = 0xFFFFFFFFU;

int kernel_vc_mbox_set_pcie_reset(unsigned int reset_id, unsigned int state) {
    vc_buf[0] = 8U * 4U;           // 32 bytes total
    vc_buf[1] = MBOX_REQ;
    vc_buf[2] = TAG_SET_RESETS;
    vc_buf[3] = 8U;                // value buffer size: 8 bytes
    vc_buf[4] = 8U;                // request length: 8 bytes
    vc_buf[5] = (uint32_t)reset_id;
    vc_buf[6] = (uint32_t)state;
    vc_buf[7] = TAG_END;
    pcie_reset_result   = vc_call(32U);
    pcie_reset_response = vc_buf[6];
    return pcie_reset_result;
}

int          kernel_vc_mbox_pcie_reset_result(void)   { return pcie_reset_result;                  }
unsigned int kernel_vc_mbox_pcie_reset_response(void) { return (unsigned int)pcie_reset_response;  }
