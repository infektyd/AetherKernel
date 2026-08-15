#include "Support.h"

// AArch64 stage-1 L1 block descriptor bits (MS4_MMU_BRIEF).
#define KERNEL_MMU_L1_ENTRY_COUNT 512U
#define KERNEL_MMU_BLOCK_SIZE 0x40000000UL

#define DESC_BLOCK     (1UL << 0)
#define DESC_AF        (1UL << 10)
#define SH_INNER       (3UL << 8)
#define ATTRIDX_NORMAL (0UL << 2)
#define ATTRIDX_DEVICE (1UL << 2)
#define ATTRIDX_NC     (2UL << 2)

#define MAIR_EL1_VAL ((0xFFUL << 0) | (0x00UL << 8) | (0x44UL << 16))
#define TCR_EL1_VAL  (25UL | (1UL << 8) | (1UL << 10) | (3UL << 12) | (0UL << 14) | (1UL << 23) | (2UL << 32))
#define SCTLR_MMU_ON  ((1UL << 0) | (1UL << 2) | (1UL << 12))

static unsigned long l1_table[512] __attribute__((aligned(4096)));
static volatile unsigned int l1_table_ready;

typedef struct kernel_mmu_region {
    unsigned long va_base;
    unsigned long pa_base;
    unsigned long size;
    unsigned int kind;
} kernel_mmu_region;

static const kernel_mmu_region mmu_regions[] = {
    {0x000000000UL, 0x000000000UL, KERNEL_MMU_BLOCK_SIZE, KERNEL_MMU_REGION_KIND_NORMAL},
    {0x040000000UL, 0x040000000UL, KERNEL_MMU_BLOCK_SIZE, KERNEL_MMU_REGION_KIND_NORMAL},
    {0x080000000UL, 0x080000000UL, KERNEL_MMU_BLOCK_SIZE, KERNEL_MMU_REGION_KIND_NORMAL},
    {0x0C0000000UL, 0x0C0000000UL, KERNEL_MMU_BLOCK_SIZE, KERNEL_MMU_REGION_KIND_DEVICE},
    {0x600000000UL, 0x600000000UL, KERNEL_MMU_BLOCK_SIZE, KERNEL_MMU_REGION_KIND_DEVICE},
};

#define KERNEL_MMU_REGION_COUNT ((unsigned int)(sizeof(mmu_regions) / sizeof(mmu_regions[0])))

static unsigned long normal_block(unsigned long pa) {
    return pa | DESC_BLOCK | DESC_AF | SH_INNER | ATTRIDX_NORMAL;
}

static unsigned long device_block(unsigned long pa) {
    return pa | DESC_BLOCK | DESC_AF | ATTRIDX_DEVICE;
}

static void msr_mair_el1(unsigned long v) {
    __asm__ volatile("msr mair_el1, %0" :: "r"(v) : "memory");
}

static void msr_tcr_el1(unsigned long v) {
    __asm__ volatile("msr tcr_el1, %0" :: "r"(v) : "memory");
}

static void msr_ttbr0_el1(unsigned long v) {
    __asm__ volatile("msr ttbr0_el1, %0" :: "r"(v) : "memory");
}

static unsigned long mrs_sctlr_el1(void) {
    unsigned long v;
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(v));
    return v;
}

static void msr_sctlr_el1(unsigned long v) {
    __asm__ volatile("msr sctlr_el1, %0" :: "r"(v) : "memory");
}

static void clean_data_cache_range(const void *addr, unsigned long size) {
    unsigned long start = (unsigned long)addr & ~63UL;
    unsigned long end = ((unsigned long)addr + size + 63UL) & ~63UL;
    for (unsigned long p = start; p < end; p += 64UL) {
        __asm__ volatile("dc cvac, %0" :: "r"(p) : "memory");
    }
    __asm__ volatile("dsb sy" ::: "memory");
}

static void build_l1_table_once(void) {
    if (l1_table_ready != 0) {
        return;
    }

    for (unsigned long i = 0; i < 512; i++) {
        l1_table[i] = 0;
    }
    l1_table[0]  = normal_block(0x000000000UL);
    l1_table[1]  = normal_block(0x040000000UL);
    l1_table[2]  = normal_block(0x080000000UL);
    l1_table[3]  = device_block(0x0C0000000UL);
    l1_table[24] = device_block(0x600000000UL);  // BCM2711 PCIe MMIO outbound window
    clean_data_cache_range(l1_table, sizeof(l1_table));

    l1_table_ready = 1;
    clean_data_cache_range((const void *)&l1_table_ready, sizeof(l1_table_ready));
}

void mmu_enable(void) {
    build_l1_table_once();

    msr_mair_el1(MAIR_EL1_VAL);
    msr_tcr_el1(TCR_EL1_VAL);
    msr_ttbr0_el1((unsigned long)l1_table);
    __asm__ volatile("isb" ::: "memory");

    __asm__ volatile("dsb ishst" ::: "memory");
    __asm__ volatile("tlbi vmalle1" ::: "memory");
    __asm__ volatile("dsb ish" ::: "memory");
    __asm__ volatile("isb" ::: "memory");

    {
        unsigned long sctlr = mrs_sctlr_el1();
        sctlr |= SCTLR_MMU_ON;
        __asm__ volatile("dsb ish" ::: "memory");
        msr_sctlr_el1(sctlr);
        __asm__ volatile("isb" ::: "memory");
    }
}

unsigned int kernel_mmu_l1_entry_count(void) {
    return KERNEL_MMU_L1_ENTRY_COUNT;
}

unsigned long kernel_mmu_block_size(void) {
    return KERNEL_MMU_BLOCK_SIZE;
}

unsigned int kernel_mmu_region_count(void) {
    return KERNEL_MMU_REGION_COUNT;
}

unsigned long kernel_mmu_region_va_base(unsigned int index) {
    if (index >= KERNEL_MMU_REGION_COUNT) {
        return 0;
    }
    return mmu_regions[index].va_base;
}

unsigned long kernel_mmu_region_pa_base(unsigned int index) {
    if (index >= KERNEL_MMU_REGION_COUNT) {
        return 0;
    }
    return mmu_regions[index].pa_base;
}

unsigned long kernel_mmu_region_size(unsigned int index) {
    if (index >= KERNEL_MMU_REGION_COUNT) {
        return 0;
    }
    return mmu_regions[index].size;
}

unsigned int kernel_mmu_region_kind(unsigned int index) {
    if (index >= KERNEL_MMU_REGION_COUNT) {
        return KERNEL_MMU_REGION_KIND_FAULT;
    }
    return mmu_regions[index].kind;
}

unsigned long kernel_mmu_tcr_value(void) {
    return TCR_EL1_VAL;
}

unsigned long kernel_mmu_mair_value(void) {
    return MAIR_EL1_VAL;
}

int kernel_mmu_selftest(void) {
    if (KERNEL_MMU_REGION_COUNT != 5U ||
        KERNEL_MMU_L1_ENTRY_COUNT != 512U ||
        KERNEL_MMU_BLOCK_SIZE != 0x40000000UL) {
        return 0;
    }
    if (l1_table[0]  != normal_block(0x000000000UL) ||
        l1_table[1]  != normal_block(0x040000000UL) ||
        l1_table[2]  != normal_block(0x080000000UL) ||
        l1_table[3]  != device_block(0x0C0000000UL) ||
        l1_table[24] != device_block(0x600000000UL)) {
        return 0;
    }
    // Dynamic VMM now allocates tables in L1[4]+, so we no longer assert they are 0.
    return 1;
}

//===----------------------------------------------------------------------===//
// V45 dynamic VMM: page table allocator + 3-level install for 4 KiB pages.
// Tables come from the frame allocator (kernel_frame_alloc). We install into
// the existing live l1_table (entries 4+ are currently 0/fault). High VA range
// keeps the 0-4 GiB 1 GiB block identity map completely untouched.
// Barriers + tlbi follow the same style as mmu_enable (vmalle1 is simple and safe).
// Break-before-make is explicit for any future change of a valid entry.
//===----------------------------------------------------------------------===//

#define VMM_L1_INDEX(va) (((va) >> 30) & 0x1ffUL)
#define VMM_L2_INDEX(va) (((va) >> 21) & 0x1ffUL)
#define VMM_L3_INDEX(va) (((va) >> 12) & 0x1ffUL)

#define VMM_TABLE_DESC (0x3UL)          // valid + table (not block)
#define VMM_PAGE_DESC  (0x3UL)          // valid + page (leaf at L3)
#define VMM_AF         (1UL << 10)
#define VMM_SH_INNER   (3UL << 8)
#define VMM_ATTR_NORMAL (0UL << 2)

static unsigned long vmm_page_desc(unsigned long pa) {
    return pa | VMM_PAGE_DESC | VMM_AF | VMM_SH_INNER | VMM_ATTR_NORMAL;
}

static unsigned long vmm_page_desc_nc(unsigned long pa) {
    return pa | VMM_PAGE_DESC | VMM_AF | VMM_SH_INNER | ATTRIDX_NC;
}

static unsigned long vmm_table_desc(unsigned long pa) {
    return pa | VMM_TABLE_DESC;
}

static void vmm_tlb_flush(void) {
    __asm__ volatile("dsb sy" ::: "memory");
    __asm__ volatile("tlbi vmalle1" ::: "memory");
    __asm__ volatile("dsb sy" ::: "memory");
    __asm__ volatile("isb" ::: "memory");
}

unsigned long kernel_vmm_alloc_pt(void) {
    unsigned long pa = kernel_frame_alloc();
    if (pa == 0) {
        return 0;
    }
    // Zero the table (512 entries of 8 bytes). Use volatile writes for safety.
    volatile unsigned long *pt = (volatile unsigned long *)pa;
    for (int i = 0; i < 512; i++) {
        pt[i] = 0;
    }
    // Make the zeroing visible before the table is installed.
    clean_data_cache_range((const void *)pa, 4096);
    __asm__ volatile("dsb sy" ::: "memory");
    return pa;
}

int kernel_vmm_free_pt(unsigned long pa) {
    if (pa == 0) {
        return 0;
    }
    return kernel_frame_free(pa);
}

int kernel_vmm_pt_alloc_selftest(void) {
    unsigned long a = kernel_vmm_alloc_pt();
    unsigned long b = kernel_vmm_alloc_pt();
    if (a == 0 || b == 0 || a == b) {
        if (a) kernel_vmm_free_pt(a);
        if (b) kernel_vmm_free_pt(b);
        return 0;
    }
    // Basic sanity: tables are page aligned and non-zero (we zeroed content but pa valid)
    if ((a & (KERNEL_PAGE_SIZE-1)) != 0 || (b & (KERNEL_PAGE_SIZE-1)) != 0) {
        kernel_vmm_free_pt(a);
        kernel_vmm_free_pt(b);
        return 0;
    }
    int ok = kernel_vmm_free_pt(a) && kernel_vmm_free_pt(b);
    // Re-alloc should succeed and preferably reuse (frame allocator allows)
    unsigned long c = kernel_vmm_alloc_pt();
    if (c == 0) {
        ok = 0;
    } else {
        kernel_vmm_free_pt(c);
    }
    return ok;
}

// Install a 4 KiB mapping at va -> pa with normal attrs. Allocates L2/L3 tables
// on demand for the VA (assumes va is in a range where L1 entry was fault).
// Writes to the live l1_table (which is identity-mapped and covered by block).
// Always performs full TLB maintenance after install.
int kernel_vmm_map_4k(unsigned long va, unsigned long pa, unsigned long attrs) {
    (void)attrs; // attrs reserved for future (we use fixed normal for V45-2)
    unsigned int l1i = VMM_L1_INDEX(va);
    unsigned int l2i = VMM_L2_INDEX(va);
    unsigned int l3i = VMM_L3_INDEX(va);

    if (l1i >= 512) {
        return 0; // Out of bounds
    }

    if (l1_table[l1i] == 0) {
        unsigned long l2_pa = kernel_vmm_alloc_pt();
        if (l2_pa == 0) return 0;
        l1_table[l1i] = vmm_table_desc(l2_pa);
        clean_data_cache_range((const void *)&l1_table[l1i], 8);
        vmm_tlb_flush();
    }

    unsigned long l2_pa = l1_table[l1i] & ~0xfffUL;
    volatile unsigned long *l2 = (volatile unsigned long *)l2_pa;
    if (l2[l2i] == 0) {
        unsigned long l3_pa = kernel_vmm_alloc_pt();
        if (l3_pa == 0) return 0;
        l2[l2i] = vmm_table_desc(l3_pa);
        clean_data_cache_range((const void *)&l2[l2i], 8);
        vmm_tlb_flush();
    }

    unsigned long l3_pa = l2[l2i] & ~0xfffUL;
    volatile unsigned long *l3 = (volatile unsigned long *)l3_pa;
    l3[l3i] = vmm_page_desc(pa);
    clean_data_cache_range((const void *)&l3[l3i], 8);
    vmm_tlb_flush();
    return 1;
}

int kernel_vmm_unmap_4k(unsigned long va) {
    unsigned int l1i = VMM_L1_INDEX(va);
    unsigned int l2i = VMM_L2_INDEX(va);
    unsigned int l3i = VMM_L3_INDEX(va);

    if (l1i >= 512 || l1_table[l1i] == 0) {
        return 0;
    }

    unsigned long l2_pa = l1_table[l1i] & ~0xfffUL;
    volatile unsigned long *l2 = (volatile unsigned long *)l2_pa;
    if (l2[l2i] == 0) {
        return 0;
    }

    unsigned long l3_pa = l2[l2i] & ~0xfffUL;
    volatile unsigned long *l3 = (volatile unsigned long *)l3_pa;
    
    l3[l3i] = 0;  // invalidate the leaf
    clean_data_cache_range((const void *)&l3[l3i], 8);
    vmm_tlb_flush();

    // (We leave the L2/L3 tables allocated for simplicity in V45-2; free in full vmm later if desired.)
    return 1;
}

int kernel_vmm_vmm_selftest(void) {
    // Table alloc selftest
    if (!kernel_vmm_pt_alloc_selftest()) return 0;

    // Simple high-VA map (use a VA in L1[4] range, e.g. 0x100000000 + offset in a free frame area)
    // For selftest we map a frame we alloc, write a sentinel via the VA, read back.
    unsigned long frame = kernel_frame_alloc();
    if (frame == 0) return 0;

    unsigned long test_va = 0x100000000UL + 0x1000;  // high VA, L1 idx 4, some offset
    if (!kernel_vmm_map_4k(test_va, frame, 0)) {
        kernel_frame_free(frame);
        return 0;
    }

    volatile unsigned long *p = (volatile unsigned long *)test_va;
    *p = 0xA45A45A45ULL;
    unsigned long readback = *p;
    int ok = (readback == 0xA45A45A45ULL);

    kernel_vmm_unmap_4k(test_va);
    kernel_frame_free(frame);
    // After unmap + flush, the old mapping should be gone (we don't re-access here to avoid fault).
    return ok ? 1 : 0;
}

//===----------------------------------------------------------------------===//
// Runtime V46: kernel/user address-space split and isolated page tables.
//===----------------------------------------------------------------------===//

void kernel_vmm_init_space(unsigned long l1_pa) {
    if (l1_pa == 0) return;
    volatile unsigned long *l1 = (volatile unsigned long *)l1_pa;
    l1[0] = l1_table[0];
    l1[1] = l1_table[1];
    l1[2] = l1_table[2];
    l1[3] = l1_table[3];
    clean_data_cache_range((const void *)l1_pa, 4096);
    __asm__ volatile("dsb sy" ::: "memory");
}

unsigned long kernel_vmm_lookup_in_table(unsigned long l1_pa, unsigned long va, unsigned long *attrs_out) {
    unsigned int l1i = VMM_L1_INDEX(va);
    unsigned int l2i = VMM_L2_INDEX(va);
    unsigned int l3i = VMM_L3_INDEX(va);

    if (l1i >= 512 || l1_pa == 0) {
        return 0;
    }

    volatile unsigned long *l1 = (volatile unsigned long *)l1_pa;
    unsigned long l1e = l1[l1i];
    if ((l1e & 0x3UL) != VMM_TABLE_DESC) {
        return 0;
    }

    unsigned long l2_pa = l1e & ~0xfffUL;
    volatile unsigned long *l2 = (volatile unsigned long *)l2_pa;
    unsigned long l2e = l2[l2i];
    if ((l2e & 0x3UL) != VMM_TABLE_DESC) {
        return 0;
    }

    unsigned long l3_pa = l2e & ~0xfffUL;
    volatile unsigned long *l3 = (volatile unsigned long *)l3_pa;
    unsigned long l3e = l3[l3i];
    if ((l3e & 0x3UL) != VMM_PAGE_DESC) {
        return 0;
    }

    if (attrs_out) {
        *attrs_out = l3e;
    }
    return l3e & ~0xfffUL;
}

int kernel_vmm_map_in_table(unsigned long l1_pa, unsigned long va, unsigned long pa, unsigned long attrs) {
    unsigned int l1i = VMM_L1_INDEX(va);
    unsigned int l2i = VMM_L2_INDEX(va);
    unsigned int l3i = VMM_L3_INDEX(va);

    if (l1i >= 512 || l1_pa == 0) {
        return 0; // Out of bounds
    }

    volatile unsigned long *l1 = (volatile unsigned long *)l1_pa;

    if (l1[l1i] == 0) {
        unsigned long l2_pa = kernel_vmm_alloc_pt();
        if (l2_pa == 0) return 0;
        l1[l1i] = vmm_table_desc(l2_pa);
        clean_data_cache_range((const void *)&l1[l1i], 8);
        vmm_tlb_flush();
    }

    unsigned long l2_pa = l1[l1i] & ~0xfffUL;
    volatile unsigned long *l2 = (volatile unsigned long *)l2_pa;
    if (l2[l2i] == 0) {
        unsigned long l3_pa = kernel_vmm_alloc_pt();
        if (l3_pa == 0) return 0;
        l2[l2i] = vmm_table_desc(l3_pa);
        clean_data_cache_range((const void *)&l2[l2i], 8);
        vmm_tlb_flush();
    }

    unsigned long l3_pa = l2[l2i] & ~0xfffUL;
    volatile unsigned long *l3 = (volatile unsigned long *)l3_pa;
    l3[l3i] = vmm_page_desc(pa) | attrs;
    clean_data_cache_range((const void *)&l3[l3i], 8);
    vmm_tlb_flush();
    return 1;
}

int kernel_vmm_unmap_in_table(unsigned long l1_pa, unsigned long va) {
    unsigned int l1i = VMM_L1_INDEX(va);
    unsigned int l2i = VMM_L2_INDEX(va);
    unsigned int l3i = VMM_L3_INDEX(va);

    if (l1i >= 512 || l1_pa == 0) {
        return 0;
    }

    volatile unsigned long *l1 = (volatile unsigned long *)l1_pa;
    if (l1[l1i] == 0) {
        return 0;
    }

    unsigned long l2_pa = l1[l1i] & ~0xfffUL;
    volatile unsigned long *l2 = (volatile unsigned long *)l2_pa;
    if (l2[l2i] == 0) {
        return 0;
    }

    unsigned long l3_pa = l2[l2i] & ~0xfffUL;
    volatile unsigned long *l3 = (volatile unsigned long *)l3_pa;
    
    l3[l3i] = 0;  // invalidate the leaf
    clean_data_cache_range((const void *)&l3[l3i], 8);
    vmm_tlb_flush();
    return 1;
}

void kernel_vmm_free_space(unsigned long l1_pa) {
    if (l1_pa == 0) return;
    volatile unsigned long *l1 = (volatile unsigned long *)l1_pa;
    for (int i = 4; i < 512; i++) {
        if (l1[i] != 0 && (l1[i] & 3UL) == 3UL) {
            unsigned long l2_pa = l1[i] & ~0xfffUL;
            volatile unsigned long *l2 = (volatile unsigned long *)l2_pa;
            for (int j = 0; j < 512; j++) {
                if (l2[j] != 0 && (l2[j] & 3UL) == 3UL) {
                    unsigned long l3_pa = l2[j] & ~0xfffUL;
                    kernel_vmm_free_pt(l3_pa);
                }
            }
            kernel_vmm_free_pt(l2_pa);
        }
    }
    kernel_vmm_free_pt(l1_pa);
}

void kernel_vmm_switch_pt(unsigned long l1_pa) {
    if (l1_pa == 0) {
        msr_ttbr0_el1((unsigned long)l1_table);
    } else {
        msr_ttbr0_el1(l1_pa);
    }
    vmm_tlb_flush();
}

void kernel_vmm_switch_pt_asid(unsigned long l1_pa, unsigned int asid) {
    unsigned long ttbr;
    if (l1_pa == 0) {
        ttbr = (unsigned long)l1_table;
    } else {
        ttbr = l1_pa | (((unsigned long)asid & 0xffffUL) << 48);
    }
    msr_ttbr0_el1(ttbr);
    __asm__ volatile("dsb sy" ::: "memory");
    __asm__ volatile("isb" ::: "memory");
}

static void debug_uart_putc(char c) {
    while (mmio_read32(0xFE201000UL + 0x18UL) & (1U << 5)) {}
    mmio_write32(0xFE201000UL + 0x00UL, (unsigned int)(unsigned char)c);
}

static void debug_uart_puts(const char *s) {
    while (*s) {
        debug_uart_putc(*s++);
    }
}

static void debug_uart_puthex(unsigned long v) {
    char hex[16] = "0123456789ABCDEF";
    debug_uart_puts("0x");
    for (int i = 15; i >= 0; i--) {
        debug_uart_putc(hex[(v >> (i * 4)) & 0xf]);
    }
}

int kernel_vmm_asplit_selftest(void) {
    unsigned long pt1 = kernel_vmm_alloc_pt();
    unsigned long pt2 = kernel_vmm_alloc_pt();
    
    debug_uart_puts("asplit selftest: pt1="); debug_uart_puthex(pt1); debug_uart_puts(" pt2="); debug_uart_puthex(pt2); debug_uart_puts("\n");

    if (pt1 == 0 || pt2 == 0) {
        if (pt1) kernel_vmm_free_pt(pt1);
        if (pt2) kernel_vmm_free_pt(pt2);
        return 0;
    }

    kernel_vmm_init_space(pt1);
    kernel_vmm_init_space(pt2);

    unsigned long frame1 = kernel_frame_alloc();
    unsigned long frame2 = kernel_frame_alloc();
    
    debug_uart_puts("frame1="); debug_uart_puthex(frame1); debug_uart_puts(" frame2="); debug_uart_puthex(frame2); debug_uart_puts("\n");

    if (frame1 == 0 || frame2 == 0) {
        if (frame1) kernel_frame_free(frame1);
        if (frame2) kernel_frame_free(frame2);
        kernel_vmm_free_space(pt1);
        kernel_vmm_free_space(pt2);
        return 0;
    }

    // Map as Non-Global (nG = 1UL << 11) to use ASID segregation in TLB
    unsigned long test_va = 0x100000000UL;
    if (!kernel_vmm_map_in_table(pt1, test_va, frame1, (1UL << 11)) ||
        !kernel_vmm_map_in_table(pt2, test_va, frame2, (1UL << 11))) {
        debug_uart_puts("mapping failed\n");
        kernel_frame_free(frame1);
        kernel_frame_free(frame2);
        kernel_vmm_free_space(pt1);
        kernel_vmm_free_space(pt2);
        return 0;
    }

    // Inspect pt1 entries
    {
        unsigned long *l1 = (unsigned long *)pt1;
        debug_uart_puts("pt1[4]="); debug_uart_puthex(l1[4]); debug_uart_puts("\n");
        unsigned long l2_pa = l1[4] & ~0xfffUL;
        unsigned long *l2 = (unsigned long *)l2_pa;
        debug_uart_puts("l2[0]="); debug_uart_puthex(l2[0]); debug_uart_puts("\n");
        unsigned long l3_pa = l2[0] & ~0xfffUL;
        unsigned long *l3 = (unsigned long *)l3_pa;
        debug_uart_puts("l3[0]="); debug_uart_puthex(l3[0]); debug_uart_puts("\n");
    }
    // Inspect pt2 entries
    {
        unsigned long *l1 = (unsigned long *)pt2;
        debug_uart_puts("pt2[4]="); debug_uart_puthex(l1[4]); debug_uart_puts("\n");
        unsigned long l2_pa = l1[4] & ~0xfffUL;
        unsigned long *l2 = (unsigned long *)l2_pa;
        debug_uart_puts("pt2_l2[0]="); debug_uart_puthex(l2[0]); debug_uart_puts("\n");
        unsigned long l3_pa = l2[0] & ~0xfffUL;
        unsigned long *l3 = (unsigned long *)l3_pa;
        debug_uart_puts("pt2_l3[0]="); debug_uart_puthex(l3[0]); debug_uart_puts("\n");
    }

    // Switch to pt1 with ASID 1
    kernel_vmm_switch_pt_asid(pt1, 1);
    debug_uart_puts("switched to pt1\n");
    volatile unsigned long *p = (volatile unsigned long *)test_va;
    *p = 0xDE1DE1DE1ULL;
    debug_uart_puts("wrote to pt1 test_va\n");

    // Switch to pt2 with ASID 2
    kernel_vmm_switch_pt_asid(pt2, 2);
    debug_uart_puts("switched to pt2\n");
    unsigned long read2 = *p;
    *p = 0xAD2AD2AD2ULL;
    debug_uart_puts("wrote to pt2 test_va\n");

    // Switch to pt1 with ASID 1
    kernel_vmm_switch_pt_asid(pt1, 1);
    unsigned long read1 = *p;

    // Switch to pt2 with ASID 2
    kernel_vmm_switch_pt_asid(pt2, 2);
    unsigned long read2_again = *p;

    // Restore boot page table (ASID 0)
    kernel_vmm_switch_pt_asid(0, 0);

    debug_uart_puts("read2="); debug_uart_puthex(read2);
    debug_uart_puts(" read1="); debug_uart_puthex(read1);
    debug_uart_puts(" read2_again="); debug_uart_puthex(read2_again);
    debug_uart_puts("\n");

    kernel_frame_free(frame1);
    kernel_frame_free(frame2);
    kernel_vmm_free_space(pt1);
    kernel_vmm_free_space(pt2);

    int ok = (read2 != 0xDE1DE1DE1ULL) && (read1 == 0xDE1DE1DE1ULL) && (read2_again == 0xAD2AD2AD2ULL);
    debug_uart_puts("selftest ok="); debug_uart_putc(ok ? '1' : '0'); debug_uart_puts("\n");
    return ok ? 1 : 0;
}

int kernel_vmm_map_4k_nc(unsigned long va, unsigned long pa) {
    unsigned int l1i = VMM_L1_INDEX(va);
    unsigned int l2i = VMM_L2_INDEX(va);
    unsigned int l3i = VMM_L3_INDEX(va);
    if (l1i >= 512) return 0;
    if (l1_table[l1i] == 0) {
        unsigned long l2_pa = kernel_vmm_alloc_pt();
        if (l2_pa == 0) return 0;
        l1_table[l1i] = vmm_table_desc(l2_pa);
        clean_data_cache_range((const void *)&l1_table[l1i], 8);
        vmm_tlb_flush();
    }
    unsigned long l2_pa = l1_table[l1i] & ~0xfffUL;
    volatile unsigned long *l2 = (volatile unsigned long *)l2_pa;
    if (l2[l2i] == 0) {
        unsigned long l3_pa = kernel_vmm_alloc_pt();
        if (l3_pa == 0) return 0;
        l2[l2i] = vmm_table_desc(l3_pa);
        clean_data_cache_range((const void *)&l2[l2i], 8);
        vmm_tlb_flush();
    }
    unsigned long l3_pa = l2[l2i] & ~0xfffUL;
    volatile unsigned long *l3 = (volatile unsigned long *)l3_pa;
    l3[l3i] = vmm_page_desc_nc(pa);
    clean_data_cache_range((const void *)&l3[l3i], 8);
    vmm_tlb_flush();
    return 1;
}

volatile unsigned long el0_test_result_x0 = 0;
volatile unsigned long el0_test_result_x1 = 0;
volatile int el0_test_result_handled = 0;

// Runtime V50: set to 1 when a Data Abort from EL0 (EC=0x24) is caught and
// contained rather than escalated to kernel_exception_handler.
volatile int kernel_el0_fault_contained = 0;

int kernel_el0_fault_contained_read(void) {
    return (int)kernel_el0_fault_contained;
}

void kernel_el0_sync_handler(user_context_t *ctx) {
    unsigned long esr;
    __asm__ volatile("mrs %0, esr_el1" : "=r"(esr));
    unsigned long ec = (esr >> 26) & 0x3fUL;

    if (ec == 0x15) { // SVC from EL0
        if (ctx->regs[0] < (unsigned long)kernel_syscall_table_size()) {
            kernel_syscall_handle_svc(ctx);
        } else {
            el0_test_result_x0 = ctx->regs[0];
            el0_test_result_x1 = ctx->regs[1];
            el0_test_result_handled = 1;
        }
    } else if (ec == 0x24) { // Data Abort from EL0 — contain, don't panic
        kernel_el0_fault_contained = 1;
    } else {
        debug_uart_puts("EL0 sync exception EC=");
        debug_uart_puthex(ec);
        debug_uart_puts(" ELR=");
        debug_uart_puthex(ctx->elr_el1);
        debug_uart_puts("\n");
        unsigned long far;
        __asm__ volatile("mrs %0, far_el1" : "=r"(far));
        extern void kernel_exception_handler(unsigned long esr, unsigned long elr, unsigned long far);
        kernel_exception_handler(esr, ctx->elr_el1, far);
    }
}

int kernel_vmm_el0_selftest(void) {
    static int probed = 0, result = 0;
    if (probed) return result;
    probed = 1;
    unsigned long pt = kernel_vmm_alloc_pt();
    if (pt == 0) return 0;
    kernel_vmm_init_space(pt);

    unsigned long code_frame = kernel_frame_alloc();
    unsigned long stack_frame = kernel_frame_alloc();
    if (code_frame == 0 || stack_frame == 0) {
        if (code_frame) kernel_frame_free(code_frame);
        if (stack_frame) kernel_frame_free(stack_frame);
        kernel_vmm_free_space(pt);
        return 0;
    }

    // Copy el0_test_stub to code_frame
    unsigned long stub_size = (unsigned long)el0_test_stub_end - (unsigned long)el0_test_stub;
    if (stub_size > 4096) stub_size = 4096;
    
    unsigned char *dst = (unsigned char *)code_frame;
    const unsigned char *src = (const unsigned char *)el0_test_stub;
    for (unsigned long i = 0; i < stub_size; i++) {
        dst[i] = src[i];
    }
    clean_data_cache_range((const void *)code_frame, stub_size);
    __asm__ volatile("isb" ::: "memory");

    unsigned long user_code_va = 0x100000000UL;
    unsigned long user_stack_va = 0x100001000UL;
    unsigned long attrs = KERNEL_VMM_ATTR_USER | (1UL << 11); // Non-Global

    if (!kernel_vmm_map_in_table(pt, user_code_va, code_frame, attrs) ||
        !kernel_vmm_map_in_table(pt, user_stack_va, stack_frame, attrs)) {
        debug_uart_puts("el0 mapping failed\n");
        kernel_frame_free(code_frame);
        kernel_frame_free(stack_frame);
        kernel_vmm_free_space(pt);
        return 0;
    }

    unsigned long irq_flags = irq_save();

    // Switch page table with ASID 3
    kernel_vmm_switch_pt_asid(pt, 3);

    el0_test_result_x0 = 0;
    el0_test_result_x1 = 0;
    el0_test_result_handled = 0;

    // Enter EL0
    kernel_enter_el0_and_wait(user_code_va, user_stack_va + 4096);

    // Restore kernel page table (ASID 0)
    kernel_vmm_switch_pt_asid(0, 0);

    irq_restore(irq_flags);

    kernel_frame_free(code_frame);
    kernel_frame_free(stack_frame);
    kernel_vmm_free_space(pt);

    result = (el0_test_result_handled && (el0_test_result_x0 == 0x47) && (el0_test_result_x1 == 0x2026)) ? 1 : 0;
    debug_uart_puts("el0 ok=");
    debug_uart_putc(result ? '1' : '0');
    debug_uart_puts(" version=47\n");

    return result;
}

/* Runtime V45 boot snapshot — write-once at boot; cert/shell readers (no event ring scan). */
static struct {
    int sealed;
    int pt_ok;
    int vmm_ok;
    int asplit_ok;
    int el0_ok;
} vmm_boot_snapshot;

static int vmm_boot_norm(int v) {
    return v != 0 ? 1 : 0;
}

int kernel_vmm_boot_snapshot_seal(int pt_ok, int vmm_ok, int asplit_ok, int el0_ok) {
    if (vmm_boot_snapshot.sealed) {
        return 0;
    }
    vmm_boot_snapshot.pt_ok = vmm_boot_norm(pt_ok);
    vmm_boot_snapshot.vmm_ok = vmm_boot_norm(vmm_ok);
    vmm_boot_snapshot.asplit_ok = vmm_boot_norm(asplit_ok);
    vmm_boot_snapshot.el0_ok = vmm_boot_norm(el0_ok);
    vmm_boot_snapshot.sealed = 1;
    return 1;
}

int kernel_vmm_boot_pt_proven(void) {
    return vmm_boot_snapshot.sealed ? vmm_boot_snapshot.pt_ok : 0;
}

int kernel_vmm_boot_vmm_proven(void) {
    return vmm_boot_snapshot.sealed ? vmm_boot_snapshot.vmm_ok : 0;
}

int kernel_vmm_boot_asplit_proven(void) {
    return vmm_boot_snapshot.sealed ? vmm_boot_snapshot.asplit_ok : 0;
}

int kernel_vmm_boot_el0_proven(void) {
    return vmm_boot_snapshot.sealed ? vmm_boot_snapshot.el0_ok : 0;
}
