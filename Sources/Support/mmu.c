#include "Support.h"

// AArch64 stage-1 L1 block descriptor bits (MS4_MMU_BRIEF).
#define KERNEL_MMU_L1_ENTRY_COUNT 512U
#define KERNEL_MMU_BLOCK_SIZE 0x40000000UL

#define DESC_BLOCK     (1UL << 0)
#define DESC_AF        (1UL << 10)
#define SH_INNER       (3UL << 8)
#define ATTRIDX_NORMAL (0UL << 2)
#define ATTRIDX_DEVICE (1UL << 2)

#define MAIR_EL1_VAL ((0xFFUL << 0) | (0x00UL << 8))
#define TCR_EL1_VAL  (25UL | (1UL << 8) | (1UL << 10) | (3UL << 12) | (0UL << 14) | (1UL << 23) | (0UL << 32))
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
    {0x00000000UL, 0x00000000UL, KERNEL_MMU_BLOCK_SIZE, KERNEL_MMU_REGION_KIND_NORMAL},
    {0x40000000UL, 0x40000000UL, KERNEL_MMU_BLOCK_SIZE, KERNEL_MMU_REGION_KIND_NORMAL},
    {0x80000000UL, 0x80000000UL, KERNEL_MMU_BLOCK_SIZE, KERNEL_MMU_REGION_KIND_NORMAL},
    {0xC0000000UL, 0xC0000000UL, KERNEL_MMU_BLOCK_SIZE, KERNEL_MMU_REGION_KIND_DEVICE},
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
    l1_table[0] = normal_block(0x00000000UL);
    l1_table[1] = normal_block(0x40000000UL);
    l1_table[2] = normal_block(0x80000000UL);
    l1_table[3] = device_block(0xC0000000UL);
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
    if (KERNEL_MMU_REGION_COUNT != 4U ||
        KERNEL_MMU_L1_ENTRY_COUNT != 512U ||
        KERNEL_MMU_BLOCK_SIZE != 0x40000000UL) {
        return 0;
    }
    if (l1_table[0] != normal_block(0x00000000UL) ||
        l1_table[1] != normal_block(0x40000000UL) ||
        l1_table[2] != normal_block(0x80000000UL) ||
        l1_table[3] != device_block(0xC0000000UL)) {
        return 0;
    }
    for (unsigned int i = 4; i < KERNEL_MMU_L1_ENTRY_COUNT; i++) {
        if (l1_table[i] != 0) {
            return 0;
        }
    }
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

    if (l1i >= 512) return 0;

    // Ensure L1 entry has a table (allocate L2 if this L1 slot is still fault/0)
    if (l1_table[l1i] == 0) {
        unsigned long l2_pa = kernel_vmm_alloc_pt();
        if (l2_pa == 0) return 0;
        l1_table[l1i] = vmm_table_desc(l2_pa);
        // BBM not strictly required for first install into 0, but flush for visibility
        vmm_tlb_flush();
    }

    unsigned long l2_pa = l1_table[l1i] & ~0xfffUL;
    volatile unsigned long *l2 = (volatile unsigned long *)l2_pa;
    if (l2[l2i] == 0) {
        unsigned long l3_pa = kernel_vmm_alloc_pt();
        if (l3_pa == 0) return 0;
        l2[l2i] = vmm_table_desc(l3_pa);
        vmm_tlb_flush();
    }

    unsigned long l3_pa = l2[l2i] & ~0xfffUL;
    volatile unsigned long *l3 = (volatile unsigned long *)l3_pa;
    l3[l3i] = vmm_page_desc(pa);
    vmm_tlb_flush();
    return 1;
}

int kernel_vmm_unmap_4k(unsigned long va) {
    unsigned int l1i = VMM_L1_INDEX(va);
    unsigned int l2i = VMM_L2_INDEX(va);
    unsigned int l3i = VMM_L3_INDEX(va);

    if (l1i >= 512 || l1_table[l1i] == 0) return 0;

    unsigned long l2_pa = l1_table[l1i] & ~0xfffUL;
    volatile unsigned long *l2 = (volatile unsigned long *)l2_pa;
    if (l2[l2i] == 0) return 0;

    unsigned long l3_pa = l2[l2i] & ~0xfffUL;
    volatile unsigned long *l3 = (volatile unsigned long *)l3_pa;
    l3[l3i] = 0;  // invalidate the leaf
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
