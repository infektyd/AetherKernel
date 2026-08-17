// System DMA NC page allocator.
// Sources/Support/kernel_dma.c
//
// GENET (AXI/system) writes frames into DRAM. Those pages are mapped
// Normal Non-Cacheable (MAIR index 2) so CPU reads see the device store
// without cache maintenance. Returns the ARM physical address — not the
// xHCI PCIe inbound window. VA range is past xHCI's NC slots.

#include "include/Support.h"
#include <stdint.h>

#define DMA_NC_VBASE 0x200800000UL
#define DMA_NC_MAX 256U

static unsigned int dma_nc_slot;
static unsigned long dma_nc_pa[DMA_NC_MAX];
static void *dma_nc_va[DMA_NC_MAX];

int kernel_dma_nc_from_pa(unsigned long pa, void **nc_out) {
    unsigned long page = pa & ~0xfffUL;
    unsigned long off = pa & 0xfffUL;
    unsigned int n = dma_nc_slot;
    unsigned int i;
    if (n > DMA_NC_MAX) n = DMA_NC_MAX;
    for (i = 0; i < n; i++) {
        if (dma_nc_pa[i] == page) {
            if (nc_out) *nc_out = (void *)((unsigned long)dma_nc_va[i] + off);
            return 1;
        }
    }
    if (nc_out) *nc_out = 0;
    return 0;
}

int kernel_dma_alloc_nc(unsigned long *pa_out, void **nc_out) {
    unsigned long pa = kernel_frame_alloc();
    if (pa == 0UL) {
        if (pa_out) *pa_out = 0;
        if (nc_out) *nc_out = 0;
        return 0;
    }

    // Drop any dirty/stale lines on the identity WB alias before NC use.
    for (unsigned long off = 0; off < 4096UL; off += 64UL) {
        __asm__ volatile("dc civac, %0" :: "r"(pa + off) : "memory");
    }
    __asm__ volatile("dsb sy" ::: "memory");

    unsigned int slot = dma_nc_slot;
    unsigned long nc_va = DMA_NC_VBASE + (unsigned long)slot * 4096UL;
    dma_nc_slot++;
    if (!kernel_vmm_map_4k_nc(nc_va, pa)) {
        kernel_frame_free(pa);
        if (pa_out) *pa_out = 0;
        if (nc_out) *nc_out = 0;
        return 0;
    }
    if (slot < DMA_NC_MAX) {
        dma_nc_pa[slot] = pa;
        dma_nc_va[slot] = (void *)nc_va;
    }

    volatile uint64_t *p = (volatile uint64_t *)nc_va;
    for (unsigned i = 0; i < 512U; i++) {
        p[i] = 0ULL;
    }
    __asm__ volatile("dsb sy" ::: "memory");

    if (pa_out) *pa_out = pa;
    if (nc_out) *nc_out = (void *)nc_va;
    return 1;
}
