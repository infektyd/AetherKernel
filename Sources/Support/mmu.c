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
