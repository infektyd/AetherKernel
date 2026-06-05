//===----------------------------------------------------------------------===//
// AetherKernel — low-level support shim.
//
// MMIO must go through `volatile` so the optimizer never elides or hoists a
// peripheral read/write (e.g. a polled "wait until TX FIFO has space" loop).
// Swift has no volatile, so these C accessors are the guaranteed-correct path.
//===----------------------------------------------------------------------===//
#pragma once

static inline __attribute__((always_inline)) void nop(void) {
    __asm__ volatile("nop");
}

static inline __attribute__((always_inline)) unsigned int mmio_read32(unsigned long addr) {
    return *(volatile unsigned int *)addr;
}

static inline __attribute__((always_inline)) void mmio_write32(unsigned long addr, unsigned int value) {
    *(volatile unsigned int *)addr = value;
}

// Read CurrentEL (bits [3:2] hold the exception level). Used to prove the
// EL2->EL1 drop actually happened.
static inline __attribute__((always_inline)) unsigned long read_currentel(void) {
    unsigned long v;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(v));
    return v;
}

//===----------------------------------------------------------------------===//
// ARM generic timer (EL1 physical timer). EL1 access is enabled by boot.S
// (CNTHCTL_EL2 = EL1PCTEN|EL1PCEN) with CNTVOFF_EL2 = 0.
//
//   CNTFRQ_EL0   : tick frequency in Hz (firmware-programmed; ~54 MHz on Pi 4)
//   CNTPCT_EL0   : current 64-bit physical count (monotonic up-counter)
//   CNTP_TVAL_EL0: write N -> fire after N ticks (CompareValue = CNTPCT + N)
//   CNTP_CTL_EL0 : bit0 ENABLE, bit1 IMASK (1=mask IRQ), bit2 ISTATUS (RO, set
//                  when the condition is met — independent of IMASK, so it is
//                  pollable without an interrupt controller).
//===----------------------------------------------------------------------===//
static inline __attribute__((always_inline)) unsigned long read_cntfrq(void) {
    unsigned long v;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v));
    return v;
}

static inline __attribute__((always_inline)) unsigned long read_cntpct(void) {
    unsigned long v;
    // isb so the read isn't speculated before prior instructions (ARM ARM).
    // NOTE: keep the isb and the mrs as SEPARATE asm statements. Combined as one
    // template ("isb; mrs %0, cntpct_el0"), the optimizer dropped the mrs (kept
    // only the isb), leaving the output uninitialized — verified in disassembly.
    __asm__ volatile("isb" ::: "memory");
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(v) :: "memory");
    return v;
}

static inline __attribute__((always_inline)) void write_cntp_tval(unsigned long v) {
    __asm__ volatile("msr cntp_tval_el0, %0" :: "r"(v));
}

static inline __attribute__((always_inline)) void write_cntp_ctl(unsigned long v) {
    __asm__ volatile("msr cntp_ctl_el0, %0" :: "r"(v));
}

static inline __attribute__((always_inline)) unsigned long read_cntp_ctl(void) {
    unsigned long v;
    __asm__ volatile("mrs %0, cntp_ctl_el0" : "=r"(v));
    return v;
}

// Unmask IRQs at the PE (boot.S left DAIF masked). DAIFClr bit1 = I.
static inline __attribute__((always_inline)) void irq_enable(void)  { __asm__ volatile("msr daifclr, #2" ::: "memory"); }
static inline __attribute__((always_inline)) void irq_disable(void) { __asm__ volatile("msr daifset, #2" ::: "memory"); }
// Idle until an interrupt arrives.
static inline __attribute__((always_inline)) void wait_for_interrupt(void) { __asm__ volatile("wfi"); }

// DAIF critical-section helpers
static inline __attribute__((always_inline)) unsigned long irq_save(void) {
    unsigned long f;
    __asm__ volatile("mrs %0, daif; msr daifset, #2" : "=r"(f) :: "memory");
    return f;
}
static inline __attribute__((always_inline)) void irq_restore(unsigned long f) {
    __asm__ volatile("msr daif, %0" :: "r"(f) : "memory");
}

// Memory allocator functions exposed to Swift/application
void *malloc(unsigned long size);
void free(void *ptr);
int posix_memalign(void **memptr, unsigned long alignment, unsigned long size);
void *calloc(unsigned long nmemb, unsigned long size);
void *realloc(void *ptr, unsigned long size);

// MS4 cooperative executor (Sources/Support/executor.c).
// swift_task_asyncMainDrainQueue is the runtime's public drain trampoline (defined
// in libswift_Concurrency.a; it forwards to our swift_task_asyncMainDrainQueueImpl).
// It is void(void) so the swiftcall/cdecl ABI difference is immaterial here.
void swift_task_asyncMainDrainQueue(void);
// Executor delayed jobs share CNTP through the Runtime V2 timer arbiter.
void executor_on_timer_irq(void);

// Shared CNTP timer arbiter (Sources/Support/timersleep_hw.c). All CNTP register
// work stays in non-inline C because the inline-asm helpers were previously
// miscompiled when inlined into the Swift IRQ path. Runtime V2 has two clients:
// Swift continuation sleeps and the Swift executor's delayed jobs.
#define KERNEL_TIMER_CLIENT_SLEEP    0U
#define KERNEL_TIMER_CLIENT_EXECUTOR 1U
#define KERNEL_TIMER_CLIENT_COUNT    2U

unsigned long kernel_timer_now(void);
void kernel_timer_set_deadline(unsigned int client, unsigned long deadlineTicks);
void kernel_timer_clear_deadline(unsigned int client);
void kernel_timer_rearm(void);
unsigned int kernel_timer_active_mask(void);

// Runtime V3 status surfaces.
unsigned long heap_total_bytes(void);
unsigned long heap_free_bytes(void);
unsigned long heap_largest_free_bytes(void);
unsigned long heap_malloc_count(void);
unsigned long heap_free_count(void);
unsigned long heap_realloc_count(void);
unsigned long heap_calloc_count(void);

unsigned int executor_ready_count(void);
unsigned int executor_ready_capacity(void);
unsigned int executor_delayed_count(void);
unsigned int executor_delayed_capacity(void);

// Tiny fixed shell line buffer used by UARTShell.swift. The buffer capacity is
// defined in Swift's command parser contract; C owns the mutable byte storage so
// the hot path does not allocate Swift arrays.
void uart_shell_buffer_clear(void);
unsigned int uart_shell_buffer_count(void);
int uart_shell_buffer_append(unsigned int byte);
unsigned int uart_shell_buffer_get(unsigned int index);

// MMU setup (Sources/Support/mmu.c). Called from boot.S after the EL1 drop and
// before _main: identity-maps RAM as Normal Inner-Shareable cacheable (peripherals
// as Device) so the concurrency runtime's ldxr/stxr atomics have an exclusive monitor.
void mmu_enable(void);

// BCM2711 watchdog / PM reset (Sources/Support/watchdog.c). reset_now reboots the
// board immediately; arm/pet give a hang-detector (auto-reboot if not re-armed);
// disable cancels a pending reset.
void watchdog_reset_now(void);
void watchdog_arm_seconds(unsigned int seconds);
void watchdog_pet_seconds(unsigned int seconds);
void watchdog_disable(void);
