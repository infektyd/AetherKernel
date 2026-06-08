// kernel_vc_mbox.c
// BCM2711 VideoCore ARM->GPU property mailbox (channel 8).
// V58: firmware revision probe.

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
#define TAG_END         0x00000000U

// 16-byte aligned buffer: 7 words = 28 bytes for GET_FIRMWARE_REVISION
static volatile uint32_t __attribute__((aligned(16))) vc_buf[32];

static void vc_cache_flush(void) {
    unsigned long start = (unsigned long)(uintptr_t)vc_buf & ~63UL;
    unsigned long end   = start + 128UL;     // covers 32 words
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
            return (vc_buf[1] == MBOX_RESP_OK) ? 1 : 0;
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
