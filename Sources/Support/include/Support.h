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
unsigned long heap_allocated_bytes(void);
unsigned long heap_high_water_bytes(void);
unsigned long heap_failed_alloc_count(void);
int heap_integrity_check(void);

// Runtime V8 heap guardrails. These are stable machine-checkable reason codes:
// shell output and tests should not depend on allocator internals.
#define HEAP_GUARD_OK             0U
#define HEAP_GUARD_SENTINEL       1U
#define HEAP_GUARD_BLOCK_SIZE     2U
#define HEAP_GUARD_BLOCK_FOOTER   3U
#define HEAP_GUARD_FREE_RANGE     4U
#define HEAP_GUARD_FREE_ALLOCATED 5U
#define HEAP_GUARD_FREE_FOOTER    6U
#define HEAP_GUARD_FREE_DUP       7U
#define HEAP_GUARD_INVALID_FREE   8U
#define HEAP_GUARD_DOUBLE_FREE    9U
#define HEAP_GUARD_BACKPTR        10U

unsigned int heap_guard_last_error(void);
unsigned long heap_invalid_free_count(void);
unsigned long heap_double_free_count(void);
unsigned long heap_corruption_count(void);
int heap_guard_selftest(void);
int heap_pressure_selftest(void);
unsigned long heap_pressure_last_peak_bytes(void);
unsigned long heap_pressure_last_leak_bytes(void);
void heap_guard_invalid_free_test(void);
void heap_guard_double_free_test(void);

unsigned int executor_ready_count(void);
unsigned int executor_ready_capacity(void);
unsigned int executor_delayed_count(void);
unsigned int executor_delayed_capacity(void);

// Runtime V12 kernel object and cooperative task registries. These are fixed
// tables: they give the Swift demo runtime names, counters, and object handles
// without making the Swift heap the source of truth.
#define KERNEL_OBJECT_KIND_TASK    1U
#define KERNEL_OBJECT_KIND_DRIVER  2U
#define KERNEL_OBJECT_KIND_RUNTIME 3U
#define KERNEL_OBJECT_KIND_MAILBOX 4U

#define KERNEL_OBJECT_FLAG_ACTIVE  1U

#define KERNEL_TASK_STATE_IDLE     0U
#define KERNEL_TASK_STATE_RUNNING  1U
#define KERNEL_TASK_STATE_WAITING  2U

void kernel_object_registry_init(void);
unsigned int kernel_object_register(unsigned int kind, unsigned int flags, const unsigned char *name, unsigned int name_len);
unsigned int kernel_object_count(void);
unsigned int kernel_object_capacity(void);
unsigned int kernel_object_active_count(void);
unsigned int kernel_object_kind(unsigned int index);
unsigned int kernel_object_flags(unsigned int index);
unsigned int kernel_object_id(unsigned int index);
unsigned int kernel_object_name_len(unsigned int index);
unsigned int kernel_object_name_byte(unsigned int index, unsigned int offset);
int kernel_object_registry_selftest(void);

void kernel_task_registry_init(void);
unsigned int kernel_task_register(unsigned int task_id, const unsigned char *name, unsigned int name_len, unsigned int period_ms);
void kernel_task_mark_state(unsigned int task_id, unsigned int state);
void kernel_task_record_tick(unsigned int task_id);
unsigned int kernel_task_count(void);
unsigned int kernel_task_capacity(void);
unsigned int kernel_task_object_id(unsigned int task_id);
unsigned int kernel_task_state(unsigned int task_id);
unsigned long kernel_task_tick_count(unsigned int task_id);
unsigned int kernel_task_period_ms(unsigned int task_id);
unsigned int kernel_task_name_len(unsigned int task_id);
unsigned int kernel_task_name_byte(unsigned int task_id, unsigned int offset);
int kernel_task_registry_selftest(void);

// Runtime V13 fixed mailboxes. Each mailbox is a bounded UInt64 FIFO with
// counters and stable error codes; later Swift-facing channels build on this.
#define KERNEL_MAILBOX_ERROR_NONE   0U
#define KERNEL_MAILBOX_ERROR_FULL   1U
#define KERNEL_MAILBOX_ERROR_EMPTY  2U
#define KERNEL_MAILBOX_ERROR_BAD_ID 3U

void kernel_mailbox_registry_init(void);
unsigned int kernel_mailbox_register(unsigned int mailbox_id, const unsigned char *name, unsigned int name_len);
unsigned int kernel_mailbox_count(void);
unsigned int kernel_mailbox_capacity(void);
unsigned int kernel_mailbox_queue_capacity(void);
unsigned int kernel_mailbox_object_id(unsigned int mailbox_id);
unsigned int kernel_mailbox_depth(unsigned int mailbox_id);
unsigned long kernel_mailbox_sent_count(unsigned int mailbox_id);
unsigned long kernel_mailbox_received_count(unsigned int mailbox_id);
unsigned long kernel_mailbox_drop_count(unsigned int mailbox_id);
unsigned int kernel_mailbox_last_error(unsigned int mailbox_id);
int kernel_mailbox_send_u64(unsigned int mailbox_id, unsigned long value);
int kernel_mailbox_recv_u64(unsigned int mailbox_id, unsigned long *out);
void kernel_mailbox_clear(unsigned int mailbox_id);
unsigned int kernel_mailbox_name_len(unsigned int mailbox_id);
unsigned int kernel_mailbox_name_byte(unsigned int mailbox_id, unsigned int offset);
int kernel_mailbox_selftest(void);

// Runtime V7 memory ownership. V7 keeps the existing heap fixed and introduces
// an explicit low-memory map plus a 4 KiB physical frame allocator above it.
#define KERNEL_PAGE_SIZE   4096UL
#define KERNEL_FRAME_BASE  0x00800000UL
#define KERNEL_FRAME_LIMIT 0x04000000UL

#define KERNEL_MEMORY_REGION_KIND_RESERVED 1U
#define KERNEL_MEMORY_REGION_KIND_HEAP     2U
#define KERNEL_MEMORY_REGION_KIND_FRAMES   3U

#define KERNEL_FRAME_ERROR_NONE        0U
#define KERNEL_FRAME_ERROR_BAD_FREE    1U
#define KERNEL_FRAME_ERROR_DOUBLE_FREE 2U
#define KERNEL_FRAME_ERROR_EXHAUSTED   3U

void kernel_memory_init(void);
unsigned int kernel_memory_region_count(void);
unsigned long kernel_memory_region_start(unsigned int index);
unsigned long kernel_memory_region_end(unsigned int index);
unsigned int kernel_memory_region_kind(unsigned int index);
unsigned int kernel_memory_region_name_len(unsigned int index);
unsigned int kernel_memory_region_name_byte(unsigned int index, unsigned int offset);
unsigned long kernel_memory_reserved_bytes(void);
unsigned int kernel_memory_map_valid(void);
unsigned int kernel_memory_last_error(void);

unsigned long kernel_frame_base(void);
unsigned long kernel_frame_limit(void);
unsigned long kernel_frame_total_count(void);
unsigned long kernel_frame_free_count(void);
unsigned long kernel_frame_used_count(void);
unsigned long kernel_frame_reserved_count(void);
unsigned long kernel_frame_alloc(void);
int kernel_frame_free(unsigned long address);
int kernel_frame_allocator_selftest(void);
unsigned int kernel_frame_last_error(void);
unsigned long kernel_frame_bad_free_count(void);
unsigned long kernel_frame_double_free_count(void);
int kernel_frame_allocator_stress_selftest(void);
int kernel_frame_pressure_selftest(void);
unsigned long kernel_frame_pressure_last_peak_count(void);
unsigned long kernel_frame_pressure_last_leak_count(void);
int kernel_frame_guard_probe_selftest(void);
unsigned int kernel_frame_guard_probe_last_ok(void);

// Runtime V5/V6 diagnostics: fixed-storage counters plus retained panic/fault
// records across watchdog reset.
#define KERNEL_RETAINED_RECORD_ADDR 0x003ff000UL
#define KERNEL_RETAINED_KIND_NONE   0U
#define KERNEL_RETAINED_KIND_PANIC  1U
#define KERNEL_RETAINED_KIND_FAULT  2U

void kernel_irq_record(unsigned int intid);
unsigned long kernel_irq_total_count(void);
unsigned long kernel_irq_cntp_count(void);
unsigned long kernel_irq_uart0_count(void);
unsigned long kernel_irq_spurious_count(void);
unsigned long kernel_irq_unknown_count(void);
unsigned int kernel_timer_active_count(void);
unsigned long kernel_timer_deadline_ticks(unsigned int client);
void kernel_record_fault(unsigned long esr, unsigned long elr, unsigned long far);
unsigned int kernel_fault_seen(void);
unsigned long kernel_fault_esr(void);
unsigned long kernel_fault_elr(void);
unsigned long kernel_fault_far(void);
unsigned int kernel_panic_seen(void);
unsigned int kernel_retained_valid(void);
unsigned int kernel_retained_kind(void);
unsigned long kernel_retained_sequence(void);
unsigned long kernel_retained_esr(void);
unsigned long kernel_retained_elr(void);
unsigned long kernel_retained_far(void);
unsigned int kernel_retained_reason_len(void);
unsigned int kernel_retained_reason_byte(unsigned int index);
void kernel_retained_clear(void);
void kernel_retained_write_panic(const char *reason);
void kernel_retained_write_fault(unsigned long esr, unsigned long elr, unsigned long far);
void kernel_panic(const char *reason);
void kernel_panic_with_far(const char *reason, unsigned long far);
void kernel_panic_with_detail(const char *reason, unsigned long esr, unsigned long elr, unsigned long far);
void kernel_panic_test(void);
void kernel_trigger_sync_fault(void);

// Tiny fixed shell line buffer used by UARTShell.swift. The buffer capacity is
// defined in Swift's command parser contract; C owns the mutable byte storage so
// the hot path does not allocate Swift arrays.
void uart_shell_buffer_clear(void);
unsigned int uart_shell_buffer_count(void);
int uart_shell_buffer_append(unsigned int byte);
unsigned int uart_shell_buffer_get(unsigned int index);

// IRQ-backed PL011 UART RX byte ring (Sources/Support/uart_rx_irq.c).
// The IRQ service path uses fixed storage only: no allocation, no Swift calls.
void uart_rx_irq_init(void);
void uart_rx_irq_enable(void);
void uart_rx_irq_service(void);
int uart_rx_ring_read_byte(unsigned int *out);
unsigned int uart_rx_ring_count(void);
unsigned int uart_rx_ring_capacity(void);
unsigned int uart_rx_overflow_count(void);

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
