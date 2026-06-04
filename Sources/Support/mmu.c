#include "Support.h"

// AArch64 stage-1 L1 block descriptor bits (MS4_MMU_BRIEF).
#define DESC_BLOCK     (1UL << 0)
#define DESC_AF        (1UL << 10)
#define SH_INNER       (3UL << 8)
#define ATTRIDX_NORMAL (0UL << 2)
#define ATTRIDX_DEVICE (1UL << 2)

#define MAIR_EL1_VAL ((0xFFUL << 0) | (0x00UL << 8))
#define TCR_EL1_VAL  (25UL | (1UL << 8) | (1UL << 10) | (3UL << 12) | (0UL << 14) | (1UL << 23) | (0UL << 32))
#define SCTLR_MMU_ON  ((1UL << 0) | (1UL << 2) | (1UL << 12))

static unsigned long l1_table[512] __attribute__((aligned(4096)));

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

void mmu_enable(void) {
    unsigned long i;

    for (i = 0; i < 512; i++) {
        l1_table[i] = 0;
    }
    l1_table[0] = normal_block(0x00000000UL);
    l1_table[1] = normal_block(0x40000000UL);
    l1_table[2] = normal_block(0x80000000UL);
    l1_table[3] = device_block(0xC0000000UL);

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