// Runtime V50: EPIC A capstone — EL0 syscall round-trip + user fault containment.
#include "Support.h"

extern void usermode_fault_stub(void);
extern void usermode_fault_stub_end(void);

// kernel_el0_fault_contained is defined in mmu.c; shared between the sync
// handler (which sets it) and this selftest (which resets and reads it).
extern volatile int kernel_el0_fault_contained;

static void usermode_sync_icache(const void *addr, unsigned long size) {
    unsigned long start = (unsigned long)addr & ~63UL;
    unsigned long end = ((unsigned long)addr + size + 63UL) & ~63UL;
    for (unsigned long p = start; p < end; p += 64UL) {
        __asm__ volatile("dc cvau, %0" :: "r"(p) : "memory");
    }
    __asm__ volatile("dsb ish" ::: "memory");
    for (unsigned long p = start; p < end; p += 64UL) {
        __asm__ volatile("ic ivau, %0" :: "r"(p) : "memory");
    }
    __asm__ volatile("dsb ish" ::: "memory");
    __asm__ volatile("isb" ::: "memory");
}

int kernel_usermode_selftest(void) {
    static int probed = 0, result = 0;
    if (probed) return result;
    probed = 1;
    // Part A: verify the syscall ABI round-trip (re-runs the v48 test).
    if (!kernel_syscall_selftest()) return 0;

    // Part B: run an EL0 stub that accesses an unmapped VA at 0x200000000
    // (8 GiB), which triggers a Data Abort (EC=0x24).  The sync handler must
    // set kernel_el0_fault_contained and return to EL1 without panicking.
    unsigned long pt = kernel_vmm_alloc_pt();
    if (!pt) return 0;
    kernel_vmm_init_space(pt);

    unsigned long code_frame  = kernel_frame_alloc();
    unsigned long stack_frame = kernel_frame_alloc();
    if (!code_frame || !stack_frame) {
        if (code_frame)  kernel_frame_free(code_frame);
        if (stack_frame) kernel_frame_free(stack_frame);
        kernel_vmm_free_space(pt);
        return 0;
    }

    unsigned long stub_size = (unsigned long)usermode_fault_stub_end -
                              (unsigned long)usermode_fault_stub;
    if (stub_size > 4096) stub_size = 4096;

    unsigned char       *dst = (unsigned char *)code_frame;
    const unsigned char *src = (const unsigned char *)usermode_fault_stub;
    for (unsigned long i = 0; i < stub_size; i++) dst[i] = src[i];
    usermode_sync_icache((const void *)code_frame, stub_size);
    __asm__ volatile("isb" ::: "memory");

    unsigned long user_code_va  = 0x100002000UL;
    unsigned long user_stack_va = 0x100003000UL;
    unsigned long attrs = KERNEL_VMM_ATTR_USER | (1UL << 11);

    if (!kernel_vmm_map_in_table(pt, user_code_va, code_frame, attrs) ||
        !kernel_vmm_map_in_table(pt, user_stack_va, stack_frame, attrs)) {
        kernel_frame_free(code_frame);
        kernel_frame_free(stack_frame);
        kernel_vmm_free_space(pt);
        return 0;
    }

    unsigned long irq_flags = irq_save();
    kernel_vmm_switch_pt_asid(pt, 6);
    kernel_el0_fault_contained = 0;
    kernel_enter_el0_and_wait(user_code_va, user_stack_va + 4096);
    kernel_vmm_switch_pt_asid(0, 0);
    irq_restore(irq_flags);

    int fault_caught = (int)kernel_el0_fault_contained;

    kernel_frame_free(code_frame);
    kernel_frame_free(stack_frame);
    kernel_vmm_free_space(pt);

    result = fault_caught ? 1 : 0;
    return result;
}
