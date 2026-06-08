// Runtime V48/V52: versioned syscall ABI via SVC from EL0.
#include "Support.h"

#define KERNEL_SYSCALL_ABI_VERSION 48
#define KERNEL_SYSCALL_TABLE_SIZE 5

#define KERNEL_SYSCALL_SYS_EXIT 0
#define KERNEL_SYSCALL_SYS_PING 1
// KERNEL_SYSCALL_SYS_WRITE = 2 is defined in Support.h

#define UART0_DR 0xFE201000UL
#define UART0_FR 0xFE201018UL

typedef unsigned long (*kernel_syscall_fn_t)(user_context_t *ctx);

static void syscall_sync_code_range(const void *addr, unsigned long size) {
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

static unsigned long kernel_syscall_sys_exit(user_context_t *ctx) {
    (void)ctx;
    return 0;
}

static unsigned long kernel_syscall_sys_ping(user_context_t *ctx) {
    unsigned long arg1 = ctx->regs[1];
    return (arg1 << 16) | 0x2026UL;
}

// sys_write(2): write up to 256 bytes from user buffer to UART.
static unsigned long kernel_syscall_sys_write(user_context_t *ctx) {
    unsigned long buf_va = ctx->regs[1];
    unsigned long len    = ctx->regs[2];
    if (len > 256) len = 256;
    unsigned char kbuf[256];
    long n = kernel_copy_from_user(kbuf, buf_va, len);
    if (n <= 0) return (unsigned long)-14; // EFAULT
    for (long i = 0; i < n; i++) {
        while (mmio_read32(UART0_FR) & (1U << 5)) {}
        mmio_write32(UART0_DR, (unsigned int)kbuf[i]);
    }
    return (unsigned long)n;
}

static kernel_syscall_fn_t kernel_syscall_table[KERNEL_SYSCALL_TABLE_SIZE] = {
    kernel_syscall_sys_exit,
    kernel_syscall_sys_ping,
    kernel_syscall_sys_write,
    0,
    0,
};

static volatile unsigned long kernel_syscall_last_num = 0;
static volatile unsigned long kernel_syscall_last_ret = 0;
static volatile int kernel_syscall_last_dispatched = 0;

unsigned int kernel_syscall_abi_version(void) {
    return KERNEL_SYSCALL_ABI_VERSION;
}

unsigned int kernel_syscall_table_size(void) {
    return KERNEL_SYSCALL_TABLE_SIZE;
}

int kernel_syscall_table_valid(void) {
    return kernel_syscall_table[KERNEL_SYSCALL_SYS_EXIT] != 0 &&
           kernel_syscall_table[KERNEL_SYSCALL_SYS_PING] != 0;
}

unsigned long kernel_syscall_last_num_read(void) {
    return kernel_syscall_last_num;
}

unsigned long kernel_syscall_last_ret_read(void) {
    return kernel_syscall_last_ret;
}

int kernel_syscall_last_dispatched_read(void) {
    return kernel_syscall_last_dispatched;
}

void kernel_syscall_handle_svc(user_context_t *ctx) {
    unsigned long num = ctx->regs[0];
    unsigned long ret = (unsigned long)-38; // ENOSYS

    kernel_syscall_last_num = num;
    kernel_syscall_last_dispatched = 0;

    if (num < KERNEL_SYSCALL_TABLE_SIZE && kernel_syscall_table[num] != 0) {
        ret = kernel_syscall_table[num](ctx);
        kernel_syscall_last_dispatched = 1;
    }

    kernel_syscall_last_ret = ret;
    ctx->regs[0] = ret;
    ctx->elr_el1 += 4;
}

int kernel_syscall_selftest(void) {
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

    unsigned long stub_size = (unsigned long)syscall_test_stub_end - (unsigned long)syscall_test_stub;
    if (stub_size > 4096) stub_size = 4096;

    unsigned char *dst = (unsigned char *)code_frame;
    const unsigned char *src = (const unsigned char *)syscall_test_stub;
    for (unsigned long i = 0; i < stub_size; i++) {
        dst[i] = src[i];
    }
    syscall_sync_code_range((const void *)code_frame, stub_size);
    __asm__ volatile("isb" ::: "memory");

    unsigned long user_code_va = 0x100002000UL;
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
    kernel_vmm_switch_pt_asid(pt, 4);

    kernel_syscall_last_num = 0;
    kernel_syscall_last_ret = 0;
    kernel_syscall_last_dispatched = 0;

    kernel_enter_el0_and_wait(user_code_va, user_stack_va + 4096);

    kernel_vmm_switch_pt_asid(0, 0);
    irq_restore(irq_flags);

    kernel_frame_free(code_frame);
    kernel_frame_free(stack_frame);
    kernel_vmm_free_space(pt);

    unsigned long expected = (0x48UL << 16) | 0x2026UL;
    result = (kernel_syscall_last_dispatched &&
              kernel_syscall_last_num == KERNEL_SYSCALL_SYS_PING &&
              kernel_syscall_last_ret == expected &&
              kernel_syscall_table_valid()) ? 1 : 0;
    return result;
}