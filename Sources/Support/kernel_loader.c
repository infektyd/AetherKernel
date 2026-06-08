// Runtime V52: User binary loader — flat blob loaded into fresh address space, run at EL0.
// Runtime V53: Multi-process selftest — three independent user processes, each isolated.
#include "Support.h"

extern void user_hello_stub(void);
extern void user_hello_stub_end(void);

static void loader_sync_icache(const void *addr, unsigned long size) {
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

int kernel_loader_selftest(void) {
    static int probed = 0, result = 0;
    if (probed) return result;
    probed = 1;
    // Create a fresh process (address space + ASID).
    unsigned long pid = 0;
    if (!kernel_process_create(&pid)) return 0;

    unsigned long pt   = kernel_process_get_pt(pid);
    unsigned int  asid = kernel_process_get_asid(pid);
    if (!pt || !asid) {
        kernel_process_destroy(pid);
        return 0;
    }

    // Allocate physical frames for code and stack.
    unsigned long code_frame  = kernel_frame_alloc();
    unsigned long stack_frame = kernel_frame_alloc();
    if (!code_frame || !stack_frame) {
        if (code_frame)  kernel_frame_free(code_frame);
        if (stack_frame) kernel_frame_free(stack_frame);
        kernel_process_destroy(pid);
        return 0;
    }

    // Copy the user_hello_stub blob into the code frame.
    unsigned long stub_size = (unsigned long)user_hello_stub_end -
                              (unsigned long)user_hello_stub;
    if (stub_size > 4096) stub_size = 4096;

    unsigned char       *dst = (unsigned char *)code_frame;
    const unsigned char *src = (const unsigned char *)user_hello_stub;
    for (unsigned long i = 0; i < stub_size; i++) dst[i] = src[i];
    loader_sync_icache((const void *)code_frame, stub_size);
    __asm__ volatile("isb" ::: "memory");

    // Map code and stack into the process address space.
    unsigned long user_code_va  = 0x100002000UL;
    unsigned long user_stack_va = 0x100003000UL;
    unsigned long attrs = KERNEL_VMM_ATTR_USER | (1UL << 11);

    if (!kernel_vmm_map_in_table(pt, user_code_va, code_frame, attrs) ||
        !kernel_vmm_map_in_table(pt, user_stack_va, stack_frame, attrs)) {
        kernel_frame_free(code_frame);
        kernel_frame_free(stack_frame);
        kernel_process_destroy(pid);
        return 0;
    }

    // Activate the process address space for copy_from_user in sys_write.
    kernel_uaccess_set_active_pt(pt);

    unsigned long irq_flags = irq_save();
    kernel_vmm_switch_pt_asid(pt, asid);

    // Enter EL0; the stub calls sys_write (SVC #0 with x0=2), which returns
    // to EL1 via _kernel_el1_saved_sp before reaching the spin loop.
    kernel_enter_el0_and_wait(user_code_va, user_stack_va + 4096);

    kernel_vmm_switch_pt_asid(0, 0);
    irq_restore(irq_flags);
    kernel_uaccess_set_active_pt(0);

    // Read dispatch results before tearing down.
    int dispatched = kernel_syscall_last_dispatched_read();
    unsigned long num = kernel_syscall_last_num_read();
    unsigned long ret = kernel_syscall_last_ret_read();

    kernel_frame_free(code_frame);
    kernel_frame_free(stack_frame);
    kernel_process_destroy(pid);

    // Proof: sys_write (num=2) was dispatched and wrote 3 bytes ("Hi\n").
    result = (dispatched && num == KERNEL_SYSCALL_SYS_WRITE && ret == 3) ? 1 : 0;
    return result;
}

// Run a single user hello process; return 1 on success, 0 on failure.
// Isolated: each call creates/destroys its own process + address space.
static int run_user_hello_once(void) {
    unsigned long pid = 0;
    if (!kernel_process_create(&pid)) return 0;

    unsigned long pt   = kernel_process_get_pt(pid);
    unsigned int  asid = kernel_process_get_asid(pid);
    if (!pt || !asid) { kernel_process_destroy(pid); return 0; }

    unsigned long code_frame  = kernel_frame_alloc();
    unsigned long stack_frame = kernel_frame_alloc();
    if (!code_frame || !stack_frame) {
        if (code_frame)  kernel_frame_free(code_frame);
        if (stack_frame) kernel_frame_free(stack_frame);
        kernel_process_destroy(pid);
        return 0;
    }

    unsigned long stub_size = (unsigned long)user_hello_stub_end -
                              (unsigned long)user_hello_stub;
    if (stub_size > 4096) stub_size = 4096;

    unsigned char       *dst = (unsigned char *)code_frame;
    const unsigned char *src = (const unsigned char *)user_hello_stub;
    for (unsigned long i = 0; i < stub_size; i++) dst[i] = src[i];
    loader_sync_icache((const void *)code_frame, stub_size);
    __asm__ volatile("isb" ::: "memory");

    unsigned long user_code_va  = 0x100002000UL;
    unsigned long user_stack_va = 0x100003000UL;
    unsigned long attrs = KERNEL_VMM_ATTR_USER | (1UL << 11);
    if (!kernel_vmm_map_in_table(pt, user_code_va, code_frame, attrs) ||
        !kernel_vmm_map_in_table(pt, user_stack_va, stack_frame, attrs)) {
        kernel_frame_free(code_frame);
        kernel_frame_free(stack_frame);
        kernel_process_destroy(pid);
        return 0;
    }

    kernel_uaccess_set_active_pt(pt);
    unsigned long irq_flags = irq_save();
    kernel_vmm_switch_pt_asid(pt, asid);
    kernel_enter_el0_and_wait(user_code_va, user_stack_va + 4096);
    kernel_vmm_switch_pt_asid(0, 0);
    irq_restore(irq_flags);
    kernel_uaccess_set_active_pt(0);

    int dispatched = kernel_syscall_last_dispatched_read();
    unsigned long num = kernel_syscall_last_num_read();
    unsigned long ret = kernel_syscall_last_ret_read();

    kernel_frame_free(code_frame);
    kernel_frame_free(stack_frame);
    kernel_process_destroy(pid);

    return (dispatched && num == KERNEL_SYSCALL_SYS_WRITE && ret == 3) ? 1 : 0;
}

// Run 3 independent user processes in sequence, each with its own isolated
// address space and ASID.  All 3 must succeed.
int kernel_multiprocess_selftest(void) {
    static int probed = 0, result = 0;
    if (probed) return result;
    probed = 1;
    unsigned int ok = 0;
    for (int i = 0; i < 3; i++) {
        ok += (unsigned int)run_user_hello_once();
    }
    result = (ok == 3) ? 1 : 0;
    return result;
}
