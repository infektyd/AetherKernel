// Runtime V101: BCM2711 DMA engine memcpy after GENET.
// Channel 4 copies 32 bytes NC src→dst. Fail-closed on timeout, CS.ERR,
// missing CS.END, or src!=dst. Control block lives in the NC page — not
// on the 4 KiB core0 stack. Legacy 32-bit DMA uses PA|0xC0000000.
// No EL0 enter (I-abort esr=0xbf000002 elr=0x100002000 after GENET DMA).
// No boot event emit. Does not write system-timer C0/C2. Does not arm
// or reset the watchdog. Does not touch UART GPIO 14/15 pull registers.

#include "Support.h"
#include <stdint.h>

#define DMA_BASE        0xFE007000UL
#define DMA_ENABLE      0xFE007FF0UL
#define DMA_CHAN        4U
#define DMA_CHAN_BASE   (DMA_BASE + (unsigned long)DMA_CHAN * 0x100UL)

#define DMA_CS          0x00U
#define DMA_CONBLK_AD   0x04U

#define DMA_CS_ACTIVE       (1U << 0)
#define DMA_CS_END          (1U << 1)
#define DMA_CS_INT          (1U << 2)
#define DMA_CS_ERR          (1U << 8)
#define DMA_CS_WAIT_WRITES  (1U << 28)
#define DMA_CS_ABORT        (1U << 30)
#define DMA_CS_RESET        (1U << 31)

#define DMA_TI_WAIT_RESP    (1U << 3)
#define DMA_TI_DEST_INC     (1U << 4)
#define DMA_TI_SRC_INC      (1U << 8)

#define ST_CLO          0xFE003004UL
#define XFER_BYTES      32U
#define SRC_OFF         64U
#define DST_OFF         128U
#define TIMEOUT_TICKS   100000U

static int dma2_probed;
static int dma2_ok_val;
static int dma2_match_val;
static unsigned int dma2_chan_val;
static unsigned int dma2_bytes_val;

static unsigned int dma_read(unsigned int off) {
    return mmio_read32(DMA_CHAN_BASE + (unsigned long)off);
}

static void dma_write(unsigned int off, unsigned int val) {
    mmio_write32(DMA_CHAN_BASE + (unsigned long)off, val);
}

static unsigned int clo_now(void) {
    return mmio_read32(ST_CLO);
}

static void clo_spin(unsigned int ticks) {
    unsigned int start = clo_now();
    while ((clo_now() - start) < ticks) {
        __asm__ volatile("nop");
    }
}

static void dma_park(void) {
    unsigned int cs = dma_read(DMA_CS);
    if ((cs & DMA_CS_ACTIVE) != 0U) {
        dma_write(DMA_CS, DMA_CS_ABORT);
        clo_spin(20U);
    }
    dma_write(DMA_CS, DMA_CS_RESET);
    clo_spin(20U);
    dma_write(DMA_CS, DMA_CS_INT | DMA_CS_END);
}

int kernel_dma2_selftest(void) {
    unsigned int i;
    unsigned int cs;
    unsigned int start;
    unsigned long pa;
    void *nc;
    volatile uint32_t *page;
    volatile uint32_t *src;
    volatile uint32_t *dst;
    unsigned int cb_bus;
    unsigned int src_bus;
    unsigned int dst_bus;
    int match;

    if (dma2_probed) return dma2_ok_val;
    dma2_probed = 1;
    dma2_ok_val = 0;
    dma2_match_val = 0;
    dma2_chan_val = DMA_CHAN;
    dma2_bytes_val = XFER_BYTES;

    if (!kernel_dma_alloc_nc(&pa, &nc) || nc == 0 || pa == 0UL) return 0;
    if (pa >= 0x40000000UL) return 0;

    page = (volatile uint32_t *)nc;
    src = (volatile uint32_t *)((unsigned long)nc + SRC_OFF);
    dst = (volatile uint32_t *)((unsigned long)nc + DST_OFF);

    for (i = 0; i < (XFER_BYTES / 4U); i++) {
        src[i] = 0xD1010000U + i;
        dst[i] = 0U;
    }

    page[0] = DMA_TI_WAIT_RESP | DMA_TI_DEST_INC | DMA_TI_SRC_INC;
    page[1] = (unsigned int)(pa + SRC_OFF) | 0xC0000000U;
    page[2] = (unsigned int)(pa + DST_OFF) | 0xC0000000U;
    page[3] = XFER_BYTES;
    page[4] = 0U;
    page[5] = 0U;
    page[6] = 0U;
    page[7] = 0U;
    __asm__ volatile("dsb sy" ::: "memory");

    cb_bus = (unsigned int)pa | 0xC0000000U;
    src_bus = page[1];
    dst_bus = page[2];
    (void)src_bus;
    (void)dst_bus;

    mmio_write32(DMA_ENABLE, mmio_read32(DMA_ENABLE) | (1U << DMA_CHAN));
    dma_park();

    dma_write(DMA_CONBLK_AD, cb_bus);
    __asm__ volatile("dsb sy" ::: "memory");
    dma_write(DMA_CS, DMA_CS_ACTIVE | DMA_CS_WAIT_WRITES);

    start = clo_now();
    do {
        cs = dma_read(DMA_CS);
        if ((cs & DMA_CS_ACTIVE) == 0U) break;
        if ((cs & DMA_CS_ERR) != 0U) break;
    } while ((clo_now() - start) < TIMEOUT_TICKS);

    __asm__ volatile("dsb sy" ::: "memory");
    cs = dma_read(DMA_CS);

    match = 1;
    for (i = 0; i < (XFER_BYTES / 4U); i++) {
        if (dst[i] != src[i]) match = 0;
    }
    dma2_match_val = match;

    dma_park();

    if ((cs & DMA_CS_ERR) != 0U) return 0;
    if ((cs & DMA_CS_END) == 0U) return 0;
    if ((cs & DMA_CS_ACTIVE) != 0U) return 0;
    if (!match) return 0;

    dma2_ok_val = 1;
    return 1;
}

int          kernel_dma2_ok(void)    { return dma2_ok_val; }
int          kernel_dma2_match(void) { return dma2_match_val; }
unsigned int kernel_dma2_chan(void)  { return dma2_chan_val; }
unsigned int kernel_dma2_bytes(void) { return dma2_bytes_val; }
