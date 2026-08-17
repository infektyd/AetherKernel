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
// IRQ ordering stub: forwards to kernel_executor_on_timer_irq (no-op; delay queue removed).
void executor_on_timer_irq(void);

// Swift-owned executor queues (Sources/Application/KernelExecutor.swift).
void kernel_executor_enqueue(void *job);
// Delay/deadline enqueue panic in Swift (proven dead; TimerSleep owns timed wakeups).
void kernel_executor_enqueue_delay_ns(unsigned long long ns, void *job);
void kernel_executor_enqueue_deadline_ns(unsigned long long targetNs,
                                         unsigned long long nowNs,
                                         void *job);
void kernel_executor_on_timer_irq(void);
void kernel_executor_donate_until(int (*condition)(void *), void *context);
__attribute__((noreturn)) void kernel_executor_drain_main(void);
unsigned int kernel_executor_ready_count(void);
unsigned int kernel_executor_delayed_count(void);

// Shared CNTP timer arbiter (Sources/Support/timersleep_hw.c). All CNTP register
// work stays in non-inline C because the inline-asm helpers were previously
// miscompiled when inlined into the Swift IRQ path. Armed clients: SLEEP
// (TimerSleep.swift) and SCHEDULER (kernel_scheduler.c). EXECUTOR slot exists
// but is never armed — executor delay hooks panic instead of scheduling.
#define KERNEL_TIMER_CLIENT_SLEEP    0U
#define KERNEL_TIMER_CLIENT_EXECUTOR 1U
#define KERNEL_TIMER_CLIENT_SCHEDULER 2U
#define KERNEL_TIMER_CLIENT_COUNT    3U

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
unsigned long heap_free_block_count(void);
unsigned long heap_allocated_block_count(void);
unsigned long heap_smallest_free_bytes(void);
unsigned long heap_fragmentation_permil(void);

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
unsigned long heap_pressure_last_free_block_count(void);
unsigned long heap_pressure_last_largest_free_bytes(void);
int heap_fragmentation_selftest(void);
void heap_guard_invalid_free_test(void);
void heap_guard_double_free_test(void);

unsigned int executor_ready_count(void);
unsigned int executor_ready_capacity(void);
unsigned int executor_delayed_count(void);
unsigned int executor_delayed_capacity(void);

// Runtime V28 Swift runtime dependency audit. These constants split the source
// surface Aether owns from the subset that the current linked Swift runtime
// actually pulls into the Mach-O image.
#define KERNEL_RUNTIME_AUDIT_VERSION 28U
#define KERNEL_RUNTIME_OWNED_HOOK_COUNT 10U
#define KERNEL_RUNTIME_LINKED_HOOK_COUNT 2U
#define KERNEL_RUNTIME_HEAP_SHIM_COUNT 5U
#define KERNEL_RUNTIME_LINKED_HEAP_SHIM_COUNT 3U
#define KERNEL_RUNTIME_AUDIT_REQUIRED_SYMBOL_COUNT 5U

unsigned int kernel_runtime_audit_version(void);
unsigned int kernel_runtime_source_hook_count(void);
unsigned int kernel_runtime_linked_hook_count(void);
unsigned int kernel_runtime_heap_shim_count(void);
unsigned int kernel_runtime_linked_heap_shim_count(void);
unsigned int kernel_runtime_required_symbol_count(void);
unsigned int kernel_runtime_audit_selftest(void);

// Runtime V33 atomic and spinlock substrate. These are the first bounded
// cross-core synchronization primitives Aether owns; hot paths stay in C and
// use compiler atomics with explicit acquire/release/seq-cst ordering.
#define KERNEL_ATOMIC_VERSION 33U
#define KERNEL_LOCK_VERSION 33U

typedef struct kernel_spinlock {
    unsigned int state;
    unsigned long acquisitions;
    unsigned long contentions;
} kernel_spinlock_t;

void kernel_atomic_full_barrier(void);
unsigned int kernel_atomic_load_u32(unsigned int *ptr);
void kernel_atomic_store_u32(unsigned int *ptr, unsigned int value);
unsigned int kernel_atomic_fetch_add_u32(unsigned int *ptr, unsigned int value);
unsigned long kernel_atomic_fetch_add_u64(unsigned long *ptr, unsigned long value);
unsigned int kernel_atomic_compare_exchange_u32(unsigned int *ptr, unsigned int expected, unsigned int desired);
int kernel_atomic_selftest(void);
void kernel_spinlock_init(kernel_spinlock_t *lock);
unsigned int kernel_spinlock_try_lock(kernel_spinlock_t *lock);
void kernel_spinlock_lock(kernel_spinlock_t *lock);
void kernel_spinlock_unlock(kernel_spinlock_t *lock);
unsigned long kernel_spinlock_acquisition_count(const kernel_spinlock_t *lock);
unsigned long kernel_spinlock_contention_count(const kernel_spinlock_t *lock);
int kernel_spinlock_selftest(void);

// Runtime V41 secondary scheduler work-stealing protocol.
// Runtime V40 scheduler backpressure protocol.
// Runtime V39 secondary scheduler handoff protocol.
// Runtime V38 secondary scheduler wake protocol.
// Runtime V37 timer-fed secondary C scheduler jobs.
// Runtime V36 timer-fed secondary scheduler workers.
// Runtime V35 secondary-owned scheduler workers.
// Runtime V34 timer-driven SMP scheduler dispatch.
// Runtime V31 preemptive scheduler substrate remains the base timer tick surface:
// actual Swift job
// execution stays on the existing cooperative executor while the periodic CNTP
// IRQ records preemption opportunities. V33 promoted the bounded run queue
// surface to all A72 cores; V34 lets the timer tick route bounded dispatch
// tokens through those per-core queues. V35 proves C-only secondary workers can
// drain their own per-core queues without entering the Swift runtime. V36 lets
// the timer tick feed bounded worker tokens into secondary queues so cores 1-3
// repeatedly drain C-owned scheduler work while core 0 stays the Swift owner.
// V37 turns those fed tokens into typed C-only scheduler jobs with completion
// accounting, still without letting secondary cores enter Swift runtime state.
// V38 parks secondary scheduler loops with WFE and wakes them with bounded SEV
// signals when timer-fed secondary work is enqueued.
// V39 records cross-core scheduler handoff issues and completion
// acknowledgements for those C-only secondary jobs.
// Runtime V44 bounded SMP concurrency soak protocol.
// Runtime V43 secondary scheduler priority/preemption protocol.
// Runtime V42 secondary scheduler load-balancing protocol.
// V40 proves bounded per-core queue backpressure: full queues reject overflow,
// record high-water/overflow telemetry, and drain back to zero. V41 lets idle
// C-only secondary workers steal bounded steal-job tokens from another
// secondary queue, execute them locally, and leave every queue drained. V42
// lets underloaded secondary cores pull bounded balance-job tokens from an
// overloaded peer queue, execute them locally, and record queue fairness. V43
// adds bounded high/low priority lanes on secondary queues and proves
// preemptive yield when high-priority work arrives behind low-priority tokens.
// V44 proves bounded concurrency soak rounds while SMP dispatch and timer-fed
// secondary workers stay active and every per-core queue drains back to zero.
// Package version 46 (proven/selftest gates use < 45U). sched12/schedselftest
// feature token stays 44 (V44 concurrency soak), not this package number.
#define KERNEL_SCHEDULER_VERSION 46U
#define KERNEL_SCHEDULER_CONCURRENCY_SOAK_ROUNDS 3U
#define KERNEL_SCHEDULER_CORE_CAPACITY 4U
#define KERNEL_SCHEDULER_RUNQUEUE_CAPACITY 8U
#define KERNEL_SCHEDULER_DISPATCH_TOKEN_BASE 0x3400U
#define KERNEL_SCHEDULER_WORKER_TOKEN_BASE 0x3500U
#define KERNEL_SCHEDULER_JOB_TOKEN_BASE 0x3700U
#define KERNEL_SCHEDULER_PRESSURE_TOKEN_BASE 0x4000U
#define KERNEL_SCHEDULER_STEAL_TOKEN_BASE 0x5000U
#define KERNEL_SCHEDULER_BALANCE_TOKEN_BASE 0x4200U
#define KERNEL_SCHEDULER_PRIORITY_TOKEN_BASE 0x4300U
#define KERNEL_SCHEDULER_PRIORITY_LANE_LOW 0U
#define KERNEL_SCHEDULER_PRIORITY_LANE_HIGH 1U
#define KERNEL_SCHEDULER_JOB_OP_CHECKSUM 1U

void kernel_scheduler_init(void);
void kernel_scheduler_start(unsigned long interval_ticks);
void kernel_scheduler_on_timer_irq(void);
void kernel_scheduler_enable_smp_dispatch(void);
void kernel_scheduler_enable_secondary_workers(void);
void kernel_scheduler_enable_timer_worker_feed(void);
void kernel_scheduler_enable_secondary_job_execution(void);
void kernel_scheduler_enable_secondary_wake_signals(void);
void kernel_scheduler_enable_secondary_handoffs(void);
void kernel_scheduler_enable_secondary_work_stealing(void);
void kernel_scheduler_enable_load_balancing(void);
void kernel_scheduler_enable_priority_lanes(void);
void kernel_scheduler_enable_concurrency_soak(void);
unsigned int kernel_scheduler_active(void);
unsigned int kernel_scheduler_smp_dispatch_enabled(void);
unsigned int kernel_scheduler_secondary_workers_enabled(void);
unsigned int kernel_scheduler_timer_worker_feed_enabled(void);
unsigned int kernel_scheduler_secondary_job_execution_enabled(void);
unsigned int kernel_scheduler_secondary_wake_signals_enabled(void);
unsigned int kernel_scheduler_secondary_handoffs_enabled(void);
unsigned int kernel_scheduler_secondary_work_stealing_enabled(void);
unsigned int kernel_scheduler_load_balancing_enabled(void);
unsigned int kernel_scheduler_priority_lanes_enabled(void);
unsigned int kernel_scheduler_concurrency_soak_enabled(void);
unsigned int kernel_scheduler_core_count(void);
unsigned int kernel_scheduler_runqueue_capacity(void);
unsigned int kernel_scheduler_runqueue_count(unsigned int core_id);
int kernel_scheduler_enqueue(unsigned int core_id, unsigned int token);
int kernel_scheduler_dequeue(unsigned int core_id, unsigned int *out_token);
unsigned int kernel_scheduler_runqueue_head(unsigned int core_id);
unsigned int kernel_scheduler_runqueue_tail(unsigned int core_id);
unsigned long kernel_scheduler_runqueue_high_water(unsigned int core_id);
unsigned long kernel_scheduler_runqueue_overflow_count(unsigned int core_id);
void kernel_scheduler_secondary_worker_tick(unsigned int core_id);
unsigned long kernel_scheduler_dispatch_count(unsigned int core_id);
unsigned long kernel_scheduler_route_count(unsigned int core_id);
unsigned long kernel_scheduler_worker_drain_count(unsigned int core_id);
unsigned long kernel_scheduler_worker_idle_count(unsigned int core_id);
unsigned long kernel_scheduler_worker_feed_count(unsigned int core_id);
unsigned long kernel_scheduler_worker_feed_drop_count(unsigned int core_id);
unsigned long kernel_scheduler_secondary_job_execution_count(unsigned int core_id);
unsigned long kernel_scheduler_secondary_job_completion_count(unsigned int core_id);
unsigned long kernel_scheduler_secondary_job_noop_count(unsigned int core_id);
unsigned long kernel_scheduler_secondary_job_checksum(unsigned int core_id);
unsigned long kernel_scheduler_total_dispatch_count(void);
unsigned long kernel_scheduler_total_route_count(void);
unsigned long kernel_scheduler_total_worker_drain_count(void);
unsigned long kernel_scheduler_total_worker_idle_count(void);
unsigned long kernel_scheduler_total_worker_feed_count(void);
unsigned long kernel_scheduler_total_worker_feed_drop_count(void);
unsigned long kernel_scheduler_runqueue_overflow_total(void);
unsigned long kernel_scheduler_runqueue_high_water_max(void);
unsigned long kernel_scheduler_steal_attempt_count(unsigned int core_id);
unsigned long kernel_scheduler_steal_success_count(unsigned int core_id);
unsigned long kernel_scheduler_steal_source_count(unsigned int core_id);
unsigned long kernel_scheduler_steal_completion_count(unsigned int core_id);
unsigned long kernel_scheduler_steal_total(void);
unsigned long kernel_scheduler_steal_completion_total(void);
unsigned long kernel_scheduler_balance_attempt_count(unsigned int core_id);
unsigned long kernel_scheduler_balance_success_count(unsigned int core_id);
unsigned long kernel_scheduler_balance_source_count(unsigned int core_id);
unsigned long kernel_scheduler_balance_completion_count(unsigned int core_id);
unsigned long kernel_scheduler_balance_total(void);
unsigned long kernel_scheduler_balance_completion_total(void);
unsigned long kernel_scheduler_secondary_queue_min(void);
unsigned long kernel_scheduler_secondary_queue_max(void);
unsigned long kernel_scheduler_secondary_queue_imbalance(void);
int kernel_scheduler_try_balance_work(unsigned int core_id);
int kernel_scheduler_try_preempt_priority_work(unsigned int core_id);
unsigned int kernel_scheduler_secondary_has_runnable_work(unsigned int core_id);
unsigned long kernel_scheduler_secondary_worker_total(void);
unsigned long kernel_scheduler_secondary_worker_min(void);
unsigned long kernel_scheduler_secondary_worker_max(void);
unsigned long kernel_scheduler_secondary_worker_imbalance(void);
unsigned long kernel_scheduler_secondary_worker_feed_total(void);
unsigned long kernel_scheduler_secondary_worker_feed_min(void);
unsigned long kernel_scheduler_secondary_worker_feed_max(void);
unsigned long kernel_scheduler_secondary_worker_feed_imbalance(void);
unsigned long kernel_scheduler_worker_feed_drain_gap(void);
unsigned long kernel_scheduler_secondary_job_total(void);
unsigned long kernel_scheduler_secondary_job_completion_total(void);
unsigned long kernel_scheduler_secondary_job_noop_total(void);
unsigned long kernel_scheduler_secondary_job_checksum_total(void);
unsigned long kernel_scheduler_secondary_job_min(void);
unsigned long kernel_scheduler_secondary_job_max(void);
unsigned long kernel_scheduler_secondary_job_imbalance(void);
unsigned long kernel_scheduler_secondary_job_completion_gap(void);
unsigned long kernel_scheduler_secondary_wake_signal_total(void);
unsigned int kernel_scheduler_secondary_wake_signal_mask(void);
unsigned long kernel_scheduler_secondary_wake_target_total(void);
unsigned long kernel_scheduler_secondary_wake_wait_count(unsigned int core_id);
unsigned long kernel_scheduler_secondary_wake_ack_count(unsigned int core_id);
unsigned long kernel_scheduler_secondary_wake_wait_total(void);
unsigned long kernel_scheduler_secondary_wake_ack_total(void);
unsigned long kernel_scheduler_secondary_wake_gap(void);
unsigned long kernel_scheduler_secondary_wake_imbalance(void);
unsigned long kernel_scheduler_secondary_handoff_issue_count(unsigned int core_id);
unsigned long kernel_scheduler_secondary_handoff_completion_count(unsigned int core_id);
unsigned long kernel_scheduler_secondary_handoff_issue_total(void);
unsigned long kernel_scheduler_secondary_handoff_completion_total(void);
unsigned long kernel_scheduler_secondary_handoff_gap(void);
unsigned long kernel_scheduler_secondary_handoff_imbalance(void);
unsigned long kernel_scheduler_fairness_min(void);
unsigned long kernel_scheduler_fairness_max(void);
unsigned long kernel_scheduler_fairness_imbalance(void);
unsigned long kernel_scheduler_priority_low_count(unsigned int core_id);
unsigned long kernel_scheduler_priority_high_count(unsigned int core_id);
unsigned long kernel_scheduler_priority_preempt_count(unsigned int core_id);
unsigned long kernel_scheduler_priority_yield_count(unsigned int core_id);
unsigned long kernel_scheduler_priority_completion_count(unsigned int core_id);
unsigned long kernel_scheduler_priority_preempt_total(void);
unsigned long kernel_scheduler_priority_yield_total(void);
unsigned long kernel_scheduler_priority_completion_total(void);
unsigned long kernel_scheduler_priority_lane_imbalance(void);
unsigned int kernel_scheduler_last_dispatch_core(void);
unsigned long kernel_scheduler_tick_count(unsigned int core_id);
unsigned long kernel_scheduler_irq_tick_count(unsigned int core_id);
unsigned long kernel_scheduler_preempt_count(unsigned int core_id);
unsigned long kernel_scheduler_enqueue_count(unsigned int core_id);
unsigned long kernel_scheduler_dequeue_count(unsigned int core_id);
unsigned long kernel_scheduler_interval_ticks(void);
int kernel_scheduler_selftest(void);
int kernel_scheduler_runqueue_selftest(void);
int kernel_scheduler_smp_selftest(void);
int kernel_scheduler_secondary_worker_selftest(void);
int kernel_scheduler_timer_worker_feed_selftest(void);
int kernel_scheduler_secondary_job_selftest(void);
int kernel_scheduler_secondary_wake_selftest(void);
int kernel_scheduler_secondary_handoff_selftest(void);
int kernel_scheduler_backpressure_selftest(void);
int kernel_scheduler_work_steal_selftest(void);
int kernel_scheduler_fairness_selftest(void);
int kernel_scheduler_priority_selftest(void);
unsigned long kernel_scheduler_concurrency_soak_round_total(void);
unsigned long kernel_scheduler_concurrency_soak_completion_total(void);
unsigned long kernel_scheduler_concurrency_soak_failure_total(void);
unsigned long kernel_scheduler_concurrency_soak_dispatch_total(void);
unsigned long kernel_scheduler_concurrency_soak_core_completion_total(unsigned int core_id);
int kernel_scheduler_concurrency_soak_selftest(void);
int kernel_scheduler_concurrency_soak_proven(void);
int kernel_scheduler_timer_worker_feed_proven(void);
int kernel_scheduler_secondary_worker_proven(void);
int kernel_scheduler_secondary_job_proven(void);
int kernel_scheduler_secondary_wake_proven(void);
int kernel_scheduler_secondary_handoff_proven(void);
int kernel_scheduler_backpressure_proven(void);
int kernel_scheduler_work_steal_proven(void);
int kernel_scheduler_fairness_proven(void);
int kernel_scheduler_priority_proven(void);
int kernel_scheduler_smp_scheduler_proven(void);
int kernel_scheduler_scheduler_proven(void);
int kernel_scheduler_runqueue_proven(void);

// Runtime V32 SMP secondary-core bring-up substrate. Secondary cores enter a
// fixed C-only accounting loop with private stacks; they do not touch Swift
// runtime state. Later slices can attach scheduling/queues to these records.
#define KERNEL_SMP_VERSION 32U
#define KERNEL_SMP_CORE_CAPACITY 4U
#define KERNEL_SMP_SECONDARY_MASK 0xeU
#define KERNEL_SMP_STACK_BYTES 4096U

void kernel_smp_init(void);
void kernel_smp_note_primary(unsigned long mpidr);
void kernel_smp_secondary_entry(unsigned int core_id, unsigned long mpidr);
unsigned int kernel_smp_core_capacity(void);
unsigned int kernel_smp_online_count(void);
unsigned int kernel_smp_online_mask(void);
unsigned int kernel_smp_primary_core_id(void);
unsigned int kernel_smp_core_online(unsigned int core_id);
unsigned long kernel_smp_core_mpidr(unsigned int core_id);
unsigned long kernel_smp_core_entry_count(unsigned int core_id);
unsigned long kernel_smp_core_heartbeat(unsigned int core_id);
unsigned int kernel_smp_release_map(void);
void kernel_smp_signal_scheduler_work(unsigned int target_mask);
void kernel_smp_secondary_wait_for_work(unsigned int core_id);
unsigned long kernel_smp_scheduler_signal_count(void);
unsigned int kernel_smp_scheduler_signal_mask(void);
unsigned long kernel_smp_scheduler_signal_target_total(void);
unsigned long kernel_smp_core_scheduler_wait_count(unsigned int core_id);
unsigned long kernel_smp_core_scheduler_wake_count(unsigned int core_id);
int kernel_smp_scheduler_wake_selftest(void);
int kernel_smp_selftest(void);
int kernel_smp_proven(void);

// Runtime V12 kernel object and cooperative task registries. These are fixed
// tables: they give the Swift demo runtime names, counters, and object handles
// without making the Swift heap the source of truth.
#define KERNEL_OBJECT_KIND_TASK    1U
#define KERNEL_OBJECT_KIND_DRIVER  2U
#define KERNEL_OBJECT_KIND_RUNTIME 3U
#define KERNEL_OBJECT_KIND_MAILBOX 4U

#define KERNEL_OBJECT_FLAG_ACTIVE  1U

#define KERNEL_OBJECT_CAP_INSPECT   1U
#define KERNEL_OBJECT_CAP_CONTROL   2U
#define KERNEL_OBJECT_CAP_SEND      4U
#define KERNEL_OBJECT_CAP_RECEIVE   8U
#define KERNEL_OBJECT_CAP_SUPERVISE 16U

#define KERNEL_OBJECT_LOOKUP_OK         0U
#define KERNEL_OBJECT_LOOKUP_BAD_HANDLE 1U
#define KERNEL_OBJECT_LOOKUP_STALE      2U
#define KERNEL_OBJECT_LOOKUP_CAP_DENIED 3U

#define KERNEL_OBJECT_HANDLE_INVALID 0UL

#define KERNEL_TASK_STATE_IDLE     0U
#define KERNEL_TASK_STATE_RUNNING  1U
#define KERNEL_TASK_STATE_WAITING  2U
#define KERNEL_TASK_ROOT_PARENT    0xffffffffU

void kernel_object_registry_init(void);
unsigned int kernel_object_register(unsigned int kind, unsigned int flags, const unsigned char *name, unsigned int name_len);
unsigned int kernel_object_count(void);
unsigned int kernel_object_capacity(void);
unsigned int kernel_object_active_count(void);
unsigned int kernel_object_kind(unsigned int index);
unsigned int kernel_object_flags(unsigned int index);
unsigned int kernel_object_id(unsigned int index);
unsigned int kernel_object_caps(unsigned int index);
unsigned int kernel_object_generation(unsigned int index);
unsigned int kernel_object_name_len(unsigned int index);
unsigned int kernel_object_name_byte(unsigned int index, unsigned int offset);
unsigned long kernel_object_make_handle(unsigned int index, unsigned int caps);
unsigned int kernel_object_handle_index(unsigned long handle);
unsigned int kernel_object_handle_generation(unsigned long handle);
unsigned int kernel_object_handle_caps(unsigned long handle);
unsigned int kernel_object_lookup_id(unsigned long handle, unsigned int required_caps);
unsigned int kernel_object_unregister_handle(unsigned long handle);
unsigned int kernel_object_handle_last_error(void);
int kernel_object_registry_selftest(void);
int kernel_object_handle_selftest(void);
int kernel_object_capcheck_selftest(void);

// Runtime V24 fixed driver registry. This is a bounded metadata layer for the
// kernel's current MMIO/IRQ drivers; it registers driver objects and exposes
// stats without changing the drivers' actual hot paths.
#define KERNEL_DRIVER_ID_UART0    0U
#define KERNEL_DRIVER_ID_CNTP     1U
#define KERNEL_DRIVER_ID_GIC      2U
#define KERNEL_DRIVER_ID_WATCHDOG 3U

#define KERNEL_DRIVER_STATE_READY 1U

void kernel_driver_registry_init(void);
unsigned int kernel_driver_count(void);
unsigned int kernel_driver_capacity(void);
unsigned int kernel_driver_object_id(unsigned int driver_id);
unsigned long kernel_driver_handle(unsigned int driver_id);
unsigned int kernel_driver_name_len(unsigned int driver_id);
unsigned int kernel_driver_name_byte(unsigned int driver_id, unsigned int offset);
unsigned int kernel_driver_state(unsigned int driver_id);
unsigned int kernel_driver_intid(unsigned int driver_id);
unsigned long kernel_driver_base(unsigned int driver_id);
unsigned int kernel_driver_caps(unsigned int driver_id);
unsigned long kernel_driver_irq_count(unsigned int driver_id);
unsigned long kernel_driver_error_count(unsigned int driver_id);
unsigned long kernel_driver_operation_count(unsigned int driver_id);
int kernel_driver_registry_selftest(void);

// Runtime V16 fixed kernel event log. The log is a bounded ring of recent
// machine-checkable events for humans and host agents; overflow keeps the newest
// records and increments lost_count.
#define KERNEL_EVENT_KIND_BOOT       1U
#define KERNEL_EVENT_KIND_TASK       2U
#define KERNEL_EVENT_KIND_TIMER      3U
#define KERNEL_EVENT_KIND_MAILBOX    4U
#define KERNEL_EVENT_KIND_SUPERVISOR 5U
#define KERNEL_EVENT_KIND_SHELL      6U
#define KERNEL_EVENT_KIND_HANDLE     7U
#define KERNEL_EVENT_KIND_SELFTEST   8U

void kernel_event_log_init(void);
void kernel_event_emit(unsigned int kind, unsigned long arg0, unsigned long arg1, unsigned long arg2);
unsigned int kernel_event_capacity(void);
unsigned int kernel_event_count(void);
unsigned long kernel_event_lost_count(void);
unsigned long kernel_event_sequence(void);
unsigned int kernel_event_kind(unsigned int index);
unsigned long kernel_event_ticks(unsigned int index);
unsigned long kernel_event_seq(unsigned int index);
unsigned long kernel_event_arg0(unsigned int index);
unsigned long kernel_event_arg1(unsigned int index);
unsigned long kernel_event_arg2(unsigned int index);
int kernel_event_log_selftest(void);

void kernel_task_registry_init(void);
unsigned int kernel_task_register(unsigned int task_id, const unsigned char *name, unsigned int name_len, unsigned int period_ms);
unsigned int kernel_task_register_with_parent(unsigned int task_id, const unsigned char *name, unsigned int name_len, unsigned int period_ms, unsigned int parent_task_id);
void kernel_task_set_parent(unsigned int task_id, unsigned int parent_task_id);
void kernel_task_mark_state(unsigned int task_id, unsigned int state);
void kernel_task_record_tick(unsigned int task_id);
void kernel_task_record_spawn(unsigned int task_id, unsigned int parent_task_id);
void kernel_task_record_completion(unsigned int task_id);
unsigned int kernel_task_count(void);
unsigned int kernel_task_capacity(void);
unsigned int kernel_task_object_id(unsigned int task_id);
unsigned int kernel_task_parent_id(unsigned int task_id);
unsigned long kernel_task_handle(unsigned int task_id);
unsigned int kernel_task_state(unsigned int task_id);
unsigned long kernel_task_tick_count(unsigned int task_id);
unsigned long kernel_task_spawn_count(unsigned int task_id);
unsigned long kernel_task_completion_count(unsigned int task_id);
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

// Runtime V14 deterministic cooperative supervisor. The supervisor watches
// named task IDs from the V12 task registry; policy=observe records misses,
// policy=panic fails loudly if a supervised task misses its heartbeat.
#define KERNEL_SUPERVISOR_POLICY_OBSERVE 1U
#define KERNEL_SUPERVISOR_POLICY_PANIC   2U

#define KERNEL_SUPERVISOR_STATE_HEALTHY  1U
#define KERNEL_SUPERVISOR_STATE_MISSED   2U

void kernel_supervisor_init(void);
unsigned int kernel_supervisor_register_task(unsigned int task_id, unsigned int deadline_ms, unsigned int policy);
void kernel_supervisor_heartbeat(unsigned int task_id);
void kernel_supervisor_check(void);
unsigned int kernel_supervisor_count(void);
unsigned int kernel_supervisor_capacity(void);
unsigned int kernel_supervisor_unhealthy_count(void);
unsigned long kernel_supervisor_total_missed_count(void);
unsigned long kernel_supervisor_now_ms(void);
unsigned int kernel_supervisor_task_id(unsigned int index);
unsigned int kernel_supervisor_deadline_ms(unsigned int index);
unsigned long kernel_supervisor_last_heartbeat_ms(unsigned int index);
unsigned long kernel_supervisor_missed_count(unsigned int index);
unsigned int kernel_supervisor_state(unsigned int index);
unsigned int kernel_supervisor_policy(unsigned int index);
int kernel_supervisor_selftest(void);

// Runtime V18 cooperative cancellation tokens. Tokens are fixed C records with
// generation-tagged IDs; Swift tasks poll/request/complete cooperatively.
#define KERNEL_CANCEL_TOKEN_CAPACITY 16U

#define KERNEL_CANCEL_STATE_FREE      0U
#define KERNEL_CANCEL_STATE_ACTIVE    1U
#define KERNEL_CANCEL_STATE_CANCELLED 2U
#define KERNEL_CANCEL_STATE_COMPLETED 3U

#define KERNEL_CANCEL_ERROR_NONE      0U
#define KERNEL_CANCEL_ERROR_CAPACITY  1U
#define KERNEL_CANCEL_ERROR_BAD_TOKEN 2U

void kernel_cancel_init(void);
unsigned int kernel_cancel_token_capacity(void);
unsigned int kernel_cancel_token_count(void);
unsigned long kernel_cancel_requested_count(void);
unsigned long kernel_cancel_completed_count(void);
unsigned int kernel_cancel_last_error(void);
unsigned int kernel_cancel_create(unsigned int owner_task_id, unsigned int *token_out);
unsigned int kernel_cancel_request(unsigned int token);
unsigned int kernel_cancel_is_requested(unsigned int token);
unsigned int kernel_cancel_complete(unsigned int token);
unsigned int kernel_cancel_state(unsigned int token);
unsigned int kernel_cancel_owner_task(unsigned int token);
int kernel_cancel_selftest(void);

// Runtime V22 fixed guarded typed pools. These pools are C-owned bounded slabs
// with guard words, generation counters, and stable error/counter surfaces.
#define KERNEL_POOL_SELFTEST_ID 0U

#define KERNEL_POOL_ERROR_NONE        0U
#define KERNEL_POOL_ERROR_FULL        1U
#define KERNEL_POOL_ERROR_BAD_POOL    2U
#define KERNEL_POOL_ERROR_BAD_FREE    3U
#define KERNEL_POOL_ERROR_DOUBLE_FREE 4U
#define KERNEL_POOL_ERROR_GUARD       5U

void kernel_pool_init(void);
unsigned int kernel_pool_count(void);
unsigned int kernel_pool_capacity(void);
unsigned int kernel_pool_name_len(unsigned int pool_id);
unsigned int kernel_pool_name_byte(unsigned int pool_id, unsigned int offset);
unsigned int kernel_pool_slot_size(unsigned int pool_id);
unsigned int kernel_pool_slot_capacity(unsigned int pool_id);
unsigned int kernel_pool_used(unsigned int pool_id);
unsigned int kernel_pool_high_water(unsigned int pool_id);
unsigned int kernel_pool_generation(unsigned int pool_id);
unsigned int kernel_pool_total_slot_count(void);
unsigned int kernel_pool_used_slot_count(void);
unsigned int kernel_pool_high_water_slot_count(void);
unsigned long kernel_pool_alloc_count(unsigned int pool_id);
unsigned long kernel_pool_free_count(unsigned int pool_id);
unsigned long kernel_pool_failed_alloc_count(unsigned int pool_id);
unsigned long kernel_pool_bad_free_count(unsigned int pool_id);
unsigned long kernel_pool_double_free_count(unsigned int pool_id);
unsigned long kernel_pool_failed_alloc_total(void);
unsigned long kernel_pool_bad_free_total(void);
unsigned long kernel_pool_double_free_total(void);
unsigned int kernel_pool_last_error(unsigned int pool_id);
void *kernel_pool_alloc(unsigned int pool_id);
int kernel_pool_free(unsigned int pool_id, void *ptr);
int kernel_pool_selftest(void);
int kernel_pool_pressure_selftest(void);

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

#define KERNEL_RETAINED_CATEGORY_NONE     0U
#define KERNEL_RETAINED_CATEGORY_COMMAND  1U
#define KERNEL_RETAINED_CATEGORY_FAULT    2U
#define KERNEL_RETAINED_CATEGORY_HEAP     3U
#define KERNEL_RETAINED_CATEGORY_MEMORY   4U
#define KERNEL_RETAINED_CATEGORY_REGISTRY 5U
#define KERNEL_RETAINED_CATEGORY_INTERNAL 6U

#define KERNEL_RETAINED_REASON_UNKNOWN                      0U
#define KERNEL_RETAINED_REASON_PANIC_TEST                   1U
#define KERNEL_RETAINED_REASON_SYNC_FAULT                   2U
#define KERNEL_RETAINED_REASON_HEAP_INVALID_FREE            3U
#define KERNEL_RETAINED_REASON_HEAP_DOUBLE_FREE             4U
#define KERNEL_RETAINED_REASON_MEMORY_MAP_OVERLAP           5U
#define KERNEL_RETAINED_REASON_KERNEL_OBJECT_REGISTRY_FULL  6U
#define KERNEL_RETAINED_REASON_KERNEL_TASK_REGISTRY_BAD_ID  7U

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
unsigned int kernel_retained_category(void);
unsigned int kernel_retained_reason_id(void);
unsigned long kernel_retained_sequence(void);
unsigned long kernel_retained_esr(void);
unsigned long kernel_retained_elr(void);
unsigned long kernel_retained_far(void);
unsigned int kernel_retained_reason_len(void);
unsigned int kernel_retained_reason_byte(unsigned int index);
void kernel_retained_clear(void);
void kernel_retained_write_panic(const char *reason);
void kernel_retained_write_fault(unsigned long esr, unsigned long elr, unsigned long far);
unsigned int kernel_panic_reason_id_for(const char *reason);
unsigned int kernel_panic_category_for_reason_id(unsigned int reason_id);
void kernel_panic(const char *reason);
void kernel_panic_with_far(const char *reason, unsigned long far);
void kernel_panic_with_detail(const char *reason, unsigned long esr, unsigned long elr, unsigned long far);
void kernel_panic_with_taxonomy(const char *reason, unsigned int category, unsigned int reason_id, unsigned long esr, unsigned long elr, unsigned long far);
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
#define KERNEL_MMU_REGION_KIND_FAULT  0U
#define KERNEL_MMU_REGION_KIND_NORMAL 1U
#define KERNEL_MMU_REGION_KIND_DEVICE 2U

void mmu_enable(void);
unsigned int kernel_mmu_l1_entry_count(void);
unsigned long kernel_mmu_block_size(void);
unsigned int kernel_mmu_region_count(void);
unsigned long kernel_mmu_region_va_base(unsigned int index);
unsigned long kernel_mmu_region_pa_base(unsigned int index);
unsigned long kernel_mmu_region_size(unsigned int index);
unsigned int kernel_mmu_region_kind(unsigned int index);
unsigned long kernel_mmu_tcr_value(void);
unsigned long kernel_mmu_mair_value(void);
int kernel_mmu_selftest(void);

// Runtime V45: dynamic page-table management (adds to static identity L1 blocks).
// Page tables (L2/L3) allocated from frame allocator. Install helpers perform
// explicit break-before-make + TLB maintenance (tlbi + dsb sy + isb) so that
// remaps are safe while the kernel continues running on the live EL1 stage-1 tables.
// High VA (L1 entries >=4, currently fault) used for initial test mappings to
// avoid touching the 0-4 GiB identity blocks. Map/unmap of 4 KiB pages.
unsigned long kernel_vmm_alloc_pt(void);
int kernel_vmm_free_pt(unsigned long pa);
int kernel_vmm_pt_alloc_selftest(void);
int kernel_vmm_map_4k(unsigned long va, unsigned long pa, unsigned long attrs);
int kernel_vmm_map_4k_nc(unsigned long va, unsigned long pa);
int kernel_vmm_unmap_4k(unsigned long va);
int kernel_vmm_vmm_selftest(void);  // basic table + simple high-VA map test (expanded in v45-3)

// Runtime V46: kernel/user address-space split and isolated page tables.
#define KERNEL_VMM_ATTR_USER (1UL << 6)
void kernel_vmm_init_space(unsigned long l1_pa);
unsigned long kernel_vmm_lookup_in_table(unsigned long l1_pa, unsigned long va, unsigned long *attrs_out);
int kernel_vmm_map_in_table(unsigned long l1_pa, unsigned long va, unsigned long pa, unsigned long attrs);
int kernel_vmm_unmap_in_table(unsigned long l1_pa, unsigned long va);
void kernel_vmm_free_space(unsigned long l1_pa);
void kernel_vmm_switch_pt(unsigned long l1_pa);
void kernel_vmm_switch_pt_asid(unsigned long l1_pa, unsigned int asid);
int kernel_vmm_asplit_selftest(void);

// Runtime V47: EL0 entry/exit and context save/restore.
typedef struct {
    unsigned long regs[31]; // x0 - x30
    unsigned long sp_el0;   // Stack pointer for EL0
    unsigned long elr_el1;  // ELR_EL1 (PC)
    unsigned long spsr_el1; // SPSR_EL1
} user_context_t;

void kernel_enter_el0_and_wait(unsigned long entry_pc, unsigned long user_sp);
void kernel_el0_sync_handler(user_context_t *ctx);
int kernel_vmm_el0_selftest(void);

/* Runtime V45 boot snapshot — write-once at boot; cert/shell readers (no event ring scan). */
int kernel_vmm_boot_snapshot_seal(int pt_ok, int vmm_ok, int asplit_ok, int el0_ok);
int kernel_vmm_boot_pt_proven(void);
int kernel_vmm_boot_vmm_proven(void);
int kernel_vmm_boot_asplit_proven(void);
int kernel_vmm_boot_el0_proven(void);

extern void el0_test_stub(void);
extern void el0_test_stub_end(void);

// Runtime V48: syscall ABI via SVC (svc immediate = KERNEL_SVC_SYSCALL_IMM).
#define KERNEL_SVC_SYSCALL_IMM 1
#define KERNEL_SYSCALL_SYS_WRITE 2
unsigned int kernel_syscall_abi_version(void);
unsigned int kernel_syscall_table_size(void);
int kernel_syscall_table_valid(void);
void kernel_syscall_handle_svc(user_context_t *ctx);
int kernel_syscall_selftest(void);
unsigned long kernel_syscall_last_num_read(void);
unsigned long kernel_syscall_last_ret_read(void);
int kernel_syscall_last_dispatched_read(void);
extern void syscall_test_stub(void);
extern void syscall_test_stub_end(void);

// Runtime V49: fault-safe user memory copies (page-table probe, no kernel panic).
void kernel_uaccess_set_active_pt(unsigned long l1_pa);
unsigned long kernel_uaccess_active_pt_read(void);
long kernel_copy_from_user(void *kdst, unsigned long usrc, unsigned long len);
long kernel_copy_to_user(unsigned long udst, const void *ksrc, unsigned long len);
int kernel_uaccess_selftest(void);

// Runtime V50: EPIC A capstone — EL0 syscall round-trip + user fault containment.
extern void usermode_fault_stub(void);
extern void usermode_fault_stub_end(void);
int kernel_el0_fault_contained_read(void);
int kernel_usermode_selftest(void);

// Runtime V51: Process abstraction — address space + lifecycle state.
int kernel_process_create(unsigned long *pid_out);
int kernel_process_destroy(unsigned long pid);
int kernel_process_count(void);
int kernel_process_capacity(void);
int kernel_process_selftest(void);
unsigned long kernel_process_get_pt(unsigned long pid);
unsigned int kernel_process_get_asid(unsigned long pid);

// Runtime V52: User binary loader — flat blob into fresh address space, run at EL0.
extern void user_hello_stub(void);
extern void user_hello_stub_end(void);
int kernel_loader_selftest(void);

// Runtime V53: Multi-process selftest — three isolated user processes via per-core EL0.
int kernel_multiprocess_selftest(void);

// Runtime V54: BCM2711 EMMC2/SDHCI register probe.
#define EMMC2_BASE 0xFE340000UL
int kernel_sdhci_probe_selftest(void);
unsigned long kernel_sdhci_probe_cap0(void);
unsigned long kernel_sdhci_probe_host_version(void);

// Runtime V55: SD card identification (CMD0/CMD8/ACMD41/CMD2/CMD3).
int kernel_sdhci_card_init(void);
int kernel_sdhci_card_init_selftest(void);
unsigned long kernel_sdhci_card_rca(void);
unsigned long kernel_sdhci_card_ocr(void);

// Runtime V56: Single block read via CMD17; MBR 0x55AA verification.
int kernel_sdhci_block_read(void);
int kernel_sdhci_block_read_selftest(void);
unsigned long kernel_sdhci_mbr_magic(void);

// Runtime V57: FAT32 file read (config.txt); byte count + 32-bit byte-sum checksum.
int kernel_sdhci_fat32_read(void);
int kernel_sdhci_fat32_selftest(void);
unsigned long kernel_sdhci_fat32_bytes(void);
unsigned long kernel_sdhci_fat32_checksum(void);
unsigned long kernel_sdhci_fat32_step(void);
int           kernel_sdhci_fat32_listdir(unsigned int *files_out, unsigned int *config_out);
unsigned int  kernel_sdhci_fat32_list_other(void);
int           kernel_sdhci_fat32_read_second(unsigned int *name_out,
                                             unsigned int *bytes_out,
                                             unsigned int *sum_out);
int           kernel_sdhci_fat32_list_overlays(unsigned int *files_out,
                                               unsigned int *name_out);
int           kernel_sdhci_fat32_read_overlay(unsigned int *name_out,
                                              unsigned int *bytes_out,
                                              unsigned int *sum_out);
int           kernel_sdhci_fat32_read_issue(unsigned int *name_out,
                                            unsigned int *bytes_out,
                                            unsigned int *sum_out);
int           kernel_sdhci_fat32_write_free(unsigned int *clus_out,
                                            unsigned int *match_out);
int           kernel_sdhci_fat32_create_scratch(unsigned int *name_out,
                                                unsigned int *created_out,
                                                unsigned int *match_out);
int           kernel_sdhci_fat32_read_scratch(unsigned int *name_out,
                                              unsigned int *present_out,
                                              unsigned int *match_out);
int           kernel_sdhci_card_status(unsigned int *state_out,
                                       unsigned int *ready_out,
                                       unsigned int *rca_out);
int           kernel_sdhci_card_scr(unsigned int *spec_out, unsigned int *bus_out);
int           kernel_sdhci_card_sd_status(unsigned int *type_out,
                                          unsigned int *class_out);
int           kernel_sdhci_card_bus_width(unsigned int *bits_out,
                                          unsigned int *host_out,
                                          unsigned int *mbr_out);
int           kernel_sdhci_card_multiblock(unsigned int *blocks_out,
                                           unsigned int *mbr_out);
int           kernel_sdhci_card_switch(unsigned int *mode_out,
                                       unsigned int *grp1_out);
int           kernel_sdhci_card_multiwrite(unsigned int *blocks_out,
                                           unsigned int *match_out,
                                           unsigned int *clus_out);
int           kernel_sdhci_card_blockcount(unsigned int *count_out,
                                           unsigned int *mbr_out);
int           kernel_sdhci_fat32_fsinfo(unsigned int *lead_out,
                                        unsigned int *struct_out,
                                        unsigned int *free_out);
int           kernel_sdhci_fat32_backup(unsigned int *match_out,
                                        unsigned int *sec_out);
int           kernel_sdhci_fat32_mirror(unsigned int *match_out,
                                        unsigned int *fats_out);
int           kernel_sdhci_fat32_unlink_scratch(unsigned int *deleted_out,
                                                unsigned int *present_out);

// Runtime V58: VideoCore ARM->GPU property mailbox probe (firmware revision).
int kernel_vc_mbox_probe(void);
unsigned long kernel_vc_mbox_fw_rev(void);
int kernel_vc_mbox_selftest(void);

// Runtime V59: VideoCore framebuffer allocation (width, height, depth, pitch, addr).
int kernel_vc_mbox_fb_alloc(void);
unsigned long kernel_vc_mbox_fb_width(void);
unsigned long kernel_vc_mbox_fb_height(void);
unsigned long kernel_vc_mbox_fb_depth(void);
unsigned long kernel_vc_mbox_fb_pitch(void);
unsigned long kernel_vc_mbox_fb_addr(void);
unsigned long kernel_vc_mbox_fb_size(void);
int kernel_vc_mbox_fb_selftest(void);
// Diagnostic state from most recent fb_alloc call.
int           kernel_vc_mbox_fb_last_call_ok(void);
unsigned long kernel_vc_mbox_fb_last_resp(void);
unsigned long kernel_vc_mbox_fb_raw_addr(void);
unsigned long kernel_vc_mbox_fb_raw_pitch(void);
unsigned long kernel_vc_mbox_fb_query_w(void);
unsigned long kernel_vc_mbox_fb_query_h(void);
int           kernel_vc_mbox_notify_xhci_reset(void);
int           kernel_vc_mbox_xhci_reset_ok(void);
unsigned int  kernel_vc_mbox_xhci_reset_payload(void);
int           kernel_vc_mbox_set_power_state(unsigned int device_id, unsigned int state);
int           kernel_vc_mbox_pwr_state_result(void);
unsigned int  kernel_vc_mbox_pwr_state_response(void);
int           kernel_vc_mbox_set_pcie_reset(unsigned int reset_id, unsigned int state);
int           kernel_vc_mbox_pcie_reset_result(void);
unsigned int  kernel_vc_mbox_pcie_reset_response(void);
// V69: GET_BOARD_MAC_ADDRESS (0x00010003). Read-only. Returns 1 and writes
// *out when firmware returns a non-zero 48-bit MAC. Boot/shell only.
int           kernel_vc_mbox_board_mac(unsigned long *out);
// V70: GET_BOARD_SERIAL (0x00010004). Read-only. Returns 1 and writes
// *out when firmware returns a non-zero 64-bit serial. Boot/shell only.
int           kernel_vc_mbox_board_serial(unsigned long *out);
// V96: GET_TEMPERATURE (0x00030006). Millidegrees. Boot/shell only.
int           kernel_vc_mbox_get_temp(unsigned int *out);
// V97: GET_CLOCK_RATE (0x00030002). Hz. Boot/shell only.
int           kernel_vc_mbox_get_clock_rate(unsigned int clock_id, unsigned int *out);
// V99: GET_VOLTAGE (0x00030003). Microvolts. Boot/shell only.
int           kernel_vc_mbox_get_voltage(unsigned int volt_id, unsigned int *out);

// V96: mailbox temperature after GENET. Fail-closed range. No EL0.
int           kernel_mboxt_selftest(void);
int           kernel_mboxt_ok(void);
unsigned int  kernel_mboxt_temp(void);

// V97: mailbox ARM clock rate after GENET. Fail-closed range. No EL0.
int           kernel_mboxc_selftest(void);
int           kernel_mboxc_ok(void);
unsigned int  kernel_mboxc_clk(void);
unsigned int  kernel_mboxc_hz(void);

// V99: mailbox core voltage after GENET. Fail-closed range. No EL0.
int           kernel_mboxv_selftest(void);
int           kernel_mboxv_ok(void);
unsigned int  kernel_mboxv_id(void);
unsigned int  kernel_mboxv_uv(void);

// V100: BCM2711 RNG200 word after GENET. Fail-closed fifo ready. No EL0.
int           kernel_rng_selftest(void);
int           kernel_rng_ok(void);
int           kernel_rng_ready(void);
unsigned int  kernel_rng_data(void);

// V101: BCM2711 DMA engine memcpy after GENET. Fail-closed src==dst. No EL0.
int           kernel_dma2_selftest(void);
int           kernel_dma2_ok(void);
int           kernel_dma2_match(void);
unsigned int  kernel_dma2_chan(void);
unsigned int  kernel_dma2_bytes(void);

// V102: SDHCI CMD24 write of a free FAT32 cluster after GENET. Fail-closed match. No EL0.
int           kernel_sdwr_selftest(void);
int           kernel_sdwr_ok(void);
int           kernel_sdwr_match(void);
unsigned int  kernel_sdwr_clus(void);
unsigned int  kernel_sdwr_bytes(void);

// V103: FAT32 create/link of AETHER.TMP after GENET. Fail-closed if name is foreign. No EL0.
int           kernel_sdmk_selftest(void);
int           kernel_sdmk_ok(void);
int           kernel_sdmk_match(void);
int           kernel_sdmk_created(void);
unsigned int  kernel_sdmk_name(void);

// V104: re-read AETHER.TMP by name after GENET. Read-only. Fail-closed if missing/foreign. No EL0.
int           kernel_sdrd_selftest(void);
int           kernel_sdrd_ok(void);
int           kernel_sdrd_match(void);
int           kernel_sdrd_present(void);
unsigned int  kernel_sdrd_name(void);

// V105: SDHCI CMD13 SEND_STATUS after GENET. Fail-closed TRAN+READY_FOR_DATA. No EL0.
int           kernel_sdst_selftest(void);
int           kernel_sdst_ok(void);
int           kernel_sdst_ready(void);
unsigned int  kernel_sdst_state(void);
unsigned int  kernel_sdst_rca(void);

// V106: SDHCI ACMD51 SEND_SCR after GENET. Fail-closed structure/spec/4-bit. No EL0.
int           kernel_sdscr_selftest(void);
int           kernel_sdscr_ok(void);
unsigned int  kernel_sdscr_spec(void);
unsigned int  kernel_sdscr_bus(void);

// V107: SDHCI ACMD13 SD_STATUS after GENET. Fail-closed SD/SDHC type. No EL0.
int           kernel_sdss_selftest(void);
int           kernel_sdss_ok(void);
unsigned int  kernel_sdss_type(void);
unsigned int  kernel_sdss_class(void);

// V108: SDHCI ACMD6 SET_BUS_WIDTH after GENET. Fail-closed 4-bit + MBR. No EL0.
int           kernel_sdbus_selftest(void);
int           kernel_sdbus_ok(void);
unsigned int  kernel_sdbus_bits(void);
unsigned int  kernel_sdbus_host(void);
unsigned int  kernel_sdbus_mbr(void);

// V109: SDHCI CMD18 multi-block read after GENET. Fail-closed 2 blocks + MBR. No EL0.
int           kernel_sdmb_selftest(void);
int           kernel_sdmb_ok(void);
unsigned int  kernel_sdmb_blocks(void);
unsigned int  kernel_sdmb_mbr(void);

// V110: SDHCI CMD6 SWITCH_FUNC check after GENET. Fail-closed group-1 default. No EL0.
int           kernel_sdsw_selftest(void);
int           kernel_sdsw_ok(void);
unsigned int  kernel_sdsw_mode(void);
unsigned int  kernel_sdsw_grp1(void);

// V111: SDHCI CMD25 multi-block write after GENET. Fail-closed 2-block readback. No EL0.
int           kernel_sdmw_selftest(void);
int           kernel_sdmw_ok(void);
unsigned int  kernel_sdmw_match(void);
unsigned int  kernel_sdmw_blocks(void);
unsigned int  kernel_sdmw_clus(void);

// V112: SDHCI CMD23 SET_BLOCK_COUNT after GENET. Fail-closed 2 blocks + MBR. No EL0.
int           kernel_sdbc_selftest(void);
int           kernel_sdbc_ok(void);
unsigned int  kernel_sdbc_count(void);
unsigned int  kernel_sdbc_mbr(void);

// V113: FAT32 FSInfo sector after GENET. Fail-closed lead+struct. No EL0.
int           kernel_sdfi_selftest(void);
int           kernel_sdfi_ok(void);
unsigned int  kernel_sdfi_lead(void);
unsigned int  kernel_sdfi_struct(void);
unsigned int  kernel_sdfi_free(void);

// V114: FAT32 backup boot sector after GENET. Fail-closed BPB match. No EL0.
int           kernel_sdfb_selftest(void);
int           kernel_sdfb_ok(void);
unsigned int  kernel_sdfb_match(void);
unsigned int  kernel_sdfb_sec(void);

// V115: FAT32 FAT-mirror compare after GENET. Fail-closed fats>=2. No EL0.
int           kernel_sdfm_selftest(void);
int           kernel_sdfm_ok(void);
unsigned int  kernel_sdfm_match(void);
unsigned int  kernel_sdfm_fats(void);

// V116: FAT32 scratch unlink after GENET. Fail-closed deleted+absent. No EL0.
int           kernel_sdrm_selftest(void);
int           kernel_sdrm_ok(void);
unsigned int  kernel_sdrm_deleted(void);
unsigned int  kernel_sdrm_present(void);

// Runtime V60: 8x8 text console blit (Sources/Support/kernel_vc_console.c).
// selftest blits "AetherKernel v60" and verifies readback; ok() returns the result.
int          kernel_vc_console_selftest(void);
int          kernel_vc_console_ok(void);
unsigned int kernel_vc_console_rows(void);
unsigned int kernel_vc_console_cols(void);
unsigned int kernel_vc_console_glyphs(void);
unsigned long kernel_vc_console_counter(void);
unsigned long kernel_vc_console_mirror_count(void);
void         kernel_vc_console_blit_counter(unsigned long value);
void         kernel_vc_console_tick(unsigned long scheduler_tick);
void         kernel_vc_console_note_uart(unsigned int byte);
int          kernel_vc_console_paint_if_needed(void);

// Runtime V61: BCM2711 PCIe RC bring-up (Sources/Support/kernel_pcie.c).
// selftest de-asserts PERST#, polls PHYLINKUP|DL_ACTIVE, sets outbound win0.
int          kernel_pcie_selftest(void);
int          kernel_pcie_ok(void);
unsigned int kernel_pcie_speed(void);
unsigned int kernel_pcie_width(void);
// Live outbound-window register readbacks (diagnostic).
unsigned int kernel_pcie_win0_lo(void);
unsigned int kernel_pcie_win0_hi(void);
unsigned int kernel_pcie_win0_bl(void);
unsigned int kernel_pcie_win0_bhi(void);
unsigned int kernel_pcie_win0_lhi(void);
unsigned int kernel_pcie_misc_ctrl(void);
unsigned int kernel_pcie_status(void);

// Runtime V62: VL805 USB 3.0 controller discovery (Sources/Support/kernel_pcie.c).
// selftest reads VID/DID at bus 1:0:0, probes BAR0 size, assigns BAR0=PCIe 0xF8000000.
int          kernel_vl805_selftest(void);
int          kernel_vl805_ok(void);
unsigned int kernel_vl805_vendor(void);
unsigned int kernel_vl805_device(void);
unsigned int kernel_vl805_raw_viddid(void);
unsigned int kernel_vl805_hw_rev(void);
unsigned int kernel_vl805_pcie_status(void); // PCIE_STATUS captured during EXT_CFG probe
unsigned int kernel_vl805_rgr1(void);        // RGR1_SW_INIT_1 (bit0=PERST#, bit1=BRIDGE_SW_INIT)
unsigned int kernel_vl805_busnr(void);       // DBI bridge bus numbers (SecBus in byte [15:8])

// Live config-space and MMIO diagnostic readbacks for VL805 (after selftest).
unsigned int kernel_vl805_bar0_lo(void);
unsigned int kernel_vl805_bar0_hi(void);
unsigned int kernel_vl805_cmd_reg(void);
unsigned int kernel_vl805_mmio_raw0(void);
unsigned int kernel_vl805_mmio_raw4(void);
// Diagnostic: BAR0 value captured BEFORE our probe/assignment (Pi firmware state).
unsigned int kernel_vl805_bar0_lo_pi(void);
// Diagnostic: PM power state captured during selftest (0=D0, 3=D3hot).
unsigned int kernel_vl805_pm_state(void);
// Diagnostic: result of RPI_FIRMWARE_NOTIFY_XHCI_RESET mailbox call (0=ok, -1=not run, <0=error).
int          kernel_vl805_vc_xhci_reset(void);
// Diagnostic: vc_buf[5] response payload after NOTIFY_XHCI_RESET (0=VC success, non-zero=VC error).
unsigned int kernel_vl805_vc_xhci_payload(void);
// Diagnostic: VL805 config 0x50 BEFORE NOTIFY_XHCI_RESET (0=ROM state; non-0=firmware or static cap data).
unsigned int kernel_vl805_fw_ver_pre(void);
// Diagnostic: VL805 config 0x50 AFTER NOTIFY_XHCI_RESET (firmware version if loaded).
unsigned int kernel_vl805_rom_status(void);
// Diagnostic: ms polled before mmio_raw0 became valid (0xFFFF=never valid in 5s).
unsigned int kernel_vl805_mmio_poll_ms(void);
// Diagnostic: MMIO[0] read BEFORE calling NOTIFY_XHCI_RESET (tests if Pi firmware left VL805 up).
unsigned int kernel_vl805_mmio_early(void);
// Diagnostic: HARD_DEBUG register captured before and after NOTIFY_XHCI_RESET.
// CLKREQ_DBG_EN (bit 0) gates the endpoint ref-clock — if set after NOTIFY, MMIO times out.
unsigned int kernel_vl805_hard_debug_pre(void);
unsigned int kernel_vl805_hard_debug_post(void);
// PCIe DevSts captured after first dead MMIO read: bit3(URD)=UR, else completion timeout.
unsigned int kernel_vl805_dev_sts(void);
// RC Primary PCI Status captured after first dead MMIO read: bit13=Received Master Abort (UR or CTO).
unsigned int kernel_vl805_rc_psts(void);
// PCIe extended capability header at 0x100: bits[15:0]=cap ID (0x0001=AER); 0=no extended caps.
unsigned int kernel_vl805_ext_cap0(void);
// RC Bridge Secondary Status captured after first dead MMIO read: bit13=Received Master Abort.
unsigned int kernel_vl805_rc_2sts(void);
// AER Uncorrectable Error Status full 32 bits: bit14=CTO, bit20=UR received from downstream.
unsigned int kernel_vl805_aer_sts(void);
// AER Uncorrectable Error Mask full 32 bits: bit14=CTO masked, bit20=UR masked.
unsigned int kernel_vl805_aer_msk(void);
// 54MHz ticks elapsed for Phase 1 MMIO read: <10=AXI intercept, ~30-100=UR, ~2.7M=CTO(50ms).
unsigned int kernel_vl805_mmio_early_ticks(void);
// Diagnostic: WIN0_LO captured before our call to pcie_set_outbound_win0().
unsigned int kernel_pcie_win0_lo_pre(void);
unsigned int kernel_pcie_win0_bl_pre(void);
unsigned int kernel_pcie_mmio_pre_reset(void);
unsigned int kernel_pcie_bar0_pre_reset(void);
unsigned int kernel_pcie_cm_pcie_pre(void);
unsigned int kernel_pcie_cm_pcie_post(void);
unsigned int kernel_pcie_cm_pcie_at_l0(void);
int          kernel_pcie_path_inherited(void);
unsigned int kernel_pcie_link_inherit_ms(void);
unsigned int kernel_pcie_mmio_imm_l0(void);
unsigned int kernel_pcie_mmio_imm_l0_ticks(void);
unsigned int kernel_pcie_mmio_pre_perst(void);
unsigned int kernel_pcie_mmio_pre_perst_ticks(void);
unsigned int kernel_pcie_mmio_post_perst(void);
unsigned int kernel_pcie_mmio_post_perst_ticks(void);
unsigned int kernel_pcie_mmio_at_l0(void);
unsigned int kernel_pcie_mmio_at_l0_ticks(void);
unsigned int kernel_pcie_mmio_post_link(void);
unsigned int kernel_pcie_mmio_post_link_ticks(void);
unsigned int kernel_pcie_rgr1_pi(void);
unsigned int kernel_pcie_rc_cmd(void);
unsigned int kernel_pcie_priv1_lnkcap_pre(void);
unsigned int kernel_pcie_priv1_lnkcap_post(void);
unsigned int kernel_pcie_lnkctl2_pre(void);
unsigned int kernel_pcie_lnkctl2_post(void);
unsigned int kernel_pcie_win0_bl_at_l0(void);
unsigned int kernel_pcie_win0_bhi_at_l0(void);
unsigned int kernel_pcie_win0_lhi_at_l0(void);
unsigned int kernel_pcie_lnkctl(void);
unsigned int kernel_pcie_hard_debug_post(void);
unsigned int kernel_pcie_misc_ctrl_post(void);

// Runtime V63: xHCI capability register probe (Sources/Support/kernel_xhci.c).
// V64: xHCI controller init — DCBAA + rings + USBCMD.RUN + port-connect detect.
// V65: USB device enumeration — port reset, ENABLE_SLOT, ADDRESS_DEVICE, GET_DESCRIPTOR.
// V66: HID boot-protocol keyboard — interrupt-IN poll + keypress decode.
int          kernel_xhci_selftest(void);
int          kernel_xhci_ok(void);
unsigned int kernel_xhci_hciversion(void);
unsigned int kernel_xhci_ports(void);
unsigned int kernel_xhci_slots(void);
unsigned int kernel_xhci_scratch(void);

int          kernel_xhci_run_selftest(void);
int          kernel_xhci_run_ok(void);
unsigned int kernel_xhci_ports_connected(void);

int          kernel_usb_enum_selftest(void);
int          kernel_usb_enum_ok(void);
unsigned int kernel_usb_enum_vendor(void);
unsigned int kernel_usb_enum_product(void);
unsigned int kernel_usb_enum_class(void);
unsigned int kernel_usb_enum_addr(void);
unsigned int kernel_usb_enum_stage(void);
unsigned int kernel_usb_enum_portsc(void);
unsigned int kernel_usb_enum_portscR(void);
unsigned int kernel_usb_enum_slot_raw(void);
unsigned int kernel_usb_enum_ad_raw(void);

int          kernel_kbd_selftest(void);
int          kernel_kbd_ok(void);
unsigned int kernel_kbd_keycode(void);
unsigned int kernel_kbd_char(void);

// Hub downstream walk (V66). ok=1 means GET_HUB_DESCRIPTOR + GET_PORT_STATUS
// on every downstream port completed. connected/hid stay 0 when the bench
// has no device; that is honest, not a failed walk.
int          kernel_hubwalk_ok(void);
unsigned int kernel_hubwalk_ports(void);
unsigned int kernel_hubwalk_connected(void);
unsigned int kernel_hubwalk_hid(void);

// V67: GENET SYS_REV + bounded MDIO/link. Boot-time only. ok=1 means
// SYS_REV_CTRL looked like a live block; mdio/link may stay 0.
int          kernel_genet_selftest(void);
int          kernel_genet_ok(void);
unsigned int kernel_genet_rev(void);
unsigned int kernel_genet_mdio(void);
unsigned int kernel_genet_link(void);

// V68: UMAC station MAC + leftover RX_EN + MIB. No DMA, no CMD_RX_EN write.
int           kernel_genet2_selftest(void);
int           kernel_genet2_ok(void);
unsigned long kernel_genet2_mac(void);
unsigned int  kernel_genet2_rx(void);
unsigned int  kernel_genet2_frames(void);
unsigned int  kernel_genet2_bytes(void);

// V69: firmware station MAC via mailbox. No UMAC write, no DMA, no RX enable.
int           kernel_genet3_selftest(void);
int           kernel_genet3_ok(void);
unsigned long kernel_genet3_mac(void);
unsigned int  kernel_genet3_mbox(void);
unsigned long kernel_genet3_umac(void);

// V70: firmware board serial via mailbox. No UMAC write, no DMA, no RX enable.
int           kernel_genet4_selftest(void);
int           kernel_genet4_ok(void);
unsigned long kernel_genet4_serial(void);
unsigned int  kernel_genet4_mbox(void);
unsigned long kernel_genet4_mac(void);

// V71: BCM2711 GPIO register probe. Read-only. No PUP/GPFSEL write.
int           kernel_gpio_selftest(void);
int           kernel_gpio_ok(void);
unsigned int  kernel_gpio_fsel(void);
unsigned int  kernel_gpio_pup(void);
unsigned int  kernel_gpio_uart(void);

// V91: GPIO42 output + GPLEV readback after GENET. Restore FSEL. No EL0.
int           kernel_gpio2_selftest(void);
int           kernel_gpio2_ok(void);
unsigned int  kernel_gpio2_pin(void);
unsigned int  kernel_gpio2_set(void);
unsigned int  kernel_gpio2_clr(void);

// V94: GPIO26 PUP_PDN write+readback after GENET. REG1 only. No EL0.
int           kernel_gpio3_selftest(void);
int           kernel_gpio3_ok(void);
unsigned int  kernel_gpio3_pin(void);
unsigned int  kernel_gpio3_up(void);
unsigned int  kernel_gpio3_dn(void);

// System DMA NC page. Returns ARM PA (not PCIe phys+0x400000000).
int           kernel_dma_alloc_nc(unsigned long *pa_out, void **nc_out);
int           kernel_dma_nc_from_pa(unsigned long pa, void **nc_out);

// V72: leftover-RX stop + own NC RX ring. UART token only.
int           kernel_genet5_selftest(void);
int           kernel_genet5_ok(void);
unsigned int  kernel_genet5_stop(void);
unsigned int  kernel_genet5_ring(void);
unsigned int  kernel_genet5_rx(void);
unsigned int  kernel_genet5_frames(void);

// V73: mailbox MAC into UMAC + own TX ring + one ARP. UART token only.
int           kernel_genet6_selftest(void);
int           kernel_genet6_ok(void);
unsigned int  kernel_genet6_mac(void);
unsigned int  kernel_genet6_tx(void);
unsigned int  kernel_genet6_frames(void);

// V74: Linux ring-16 geometry + leftover RBUF reset. tx=1 only if CONS moved.
int           kernel_genet7_selftest(void);
int           kernel_genet7_ok(void);
unsigned int  kernel_genet7_ring(void);
unsigned int  kernel_genet7_tx(void);
unsigned int  kernel_genet7_cons(void);
unsigned int  kernel_genet7_prod(void);
unsigned int  kernel_genet7_frames(void);

// V75: v4/v5 TDMA PROD at 0x0C. tx=1 if CONS moved or PROD latched.
int           kernel_genet8_selftest(void);
int           kernel_genet8_ok(void);
unsigned int  kernel_genet8_prod(void);
unsigned int  kernel_genet8_cons(void);
unsigned int  kernel_genet8_tx(void);
unsigned int  kernel_genet8_frames(void);

// V76: parse one RX ARP/ICMP request and reply. kind 0=none 1=arp 2=icmp.
int           kernel_genet9_selftest(void);
int           kernel_genet9_ok(void);
unsigned int  kernel_genet9_rx(void);
unsigned int  kernel_genet9_tx(void);
unsigned int  kernel_genet9_kind(void);

// V77: bounded multi-BD poll. Park after. replies= latched TX replies this window.
int           kernel_genet10_selftest(void);
int           kernel_genet10_poll(void);
int           kernel_genet10_ok(void);
unsigned int  kernel_genet10_rx(void);
unsigned int  kernel_genet10_tx(void);
unsigned int  kernel_genet10_replies(void);
unsigned int  kernel_genet10_kind(void);

// V78: bounded UDP echo (port 7). Same unpark/poll/park as genet10. kind 3=udp.
int           kernel_genet11_selftest(void);
int           kernel_genet11_poll(void);
int           kernel_genet11_ok(void);
unsigned int  kernel_genet11_rx(void);
unsigned int  kernel_genet11_tx(void);
unsigned int  kernel_genet11_replies(void);
unsigned int  kernel_genet11_kind(void);

// V79: bounded TCP echo (port 7). Same unpark/poll/park. kind 4=tcp after payload.
int           kernel_genet12_selftest(void);
int           kernel_genet12_poll(void);
int           kernel_genet12_ok(void);
unsigned int  kernel_genet12_rx(void);
unsigned int  kernel_genet12_tx(void);
unsigned int  kernel_genet12_replies(void);
unsigned int  kernel_genet12_kind(void);

// V117: originate ARP + ICMP echo to 10.42.0.1. Bounded park. No EL0.
int           kernel_genet13_selftest(void);
int           kernel_genet13_ok(void);
unsigned int  kernel_genet13_arp(void);
unsigned int  kernel_genet13_echo(void);
unsigned long kernel_genet13_peer_mac(void);

// V118: originate UDP echo to 10.42.0.1:41240. Bounded park. No EL0.
int           kernel_genet14_selftest(void);
int           kernel_genet14_ok(void);
unsigned int  kernel_genet14_udp(void);
unsigned int  kernel_genet14_echo(void);

// V119: originate TCP echo to 10.42.0.1:41241. Bounded park. No EL0.
int           kernel_genet15_selftest(void);
int           kernel_genet15_ok(void);
unsigned int  kernel_genet15_tcp(void);
unsigned int  kernel_genet15_echo(void);

// V80: BSC1 + SPI0 register probe. Read-only. No boot event emit.
int           kernel_i2c_selftest(void);
int           kernel_i2c_ok(void);
unsigned int  kernel_i2c_bsc(void);
unsigned int  kernel_i2c_div(void);
unsigned int  kernel_i2c_spi(void);

// V81: PWM0 + PWM1 register probe. Read-only. No boot event emit.
int           kernel_pwm_selftest(void);
int           kernel_pwm_ok(void);
unsigned int  kernel_pwm_ctl(void);
unsigned int  kernel_pwm_sta(void);
unsigned int  kernel_pwm_pwm1(void);

// V93: PWM clock enable + CTL poke after GENET. No pin-mux. No EL0.
int           kernel_pwm2_selftest(void);
int           kernel_pwm2_ok(void);
unsigned int  kernel_pwm2_clk(void);
unsigned int  kernel_pwm2_en(void);

// V82: one bounded BSC1 write to a vacant address. Honest nack=1. No boot emit.
int           kernel_i2c2_selftest(void);
int           kernel_i2c2_ok(void);
unsigned int  kernel_i2c2_nack(void);
unsigned int  kernel_i2c2_addr(void);
unsigned int  kernel_i2c2_sta(void);

// V83: one bounded SPI0 byte. DONE is success. loop=1 only on RX==TX. No boot emit.
int           kernel_spi2_selftest(void);
int           kernel_spi2_ok(void);
unsigned int  kernel_spi2_done(void);
unsigned int  kernel_spi2_loop(void);
unsigned int  kernel_spi2_rx(void);

// V84: system timer CLO/CHI + four compare slots. Read-only. No boot emit.
int           kernel_stimer_selftest(void);
int           kernel_stimer_ok(void);
unsigned int  kernel_stimer_clo(void);
unsigned int  kernel_stimer_chi(void);
unsigned int  kernel_stimer_chans(void);

// V92: system timer C1 match after GENET. No C0/C2 writes. No EL0.
int           kernel_stimer2_selftest(void);
int           kernel_stimer2_ok(void);
unsigned int  kernel_stimer2_chan(void);
unsigned int  kernel_stimer2_match(void);

// V95: system timer C3 match after GENET. No C0/C1/C2 writes. No EL0.
int           kernel_stimer3_selftest(void);
int           kernel_stimer3_ok(void);
unsigned int  kernel_stimer3_chan(void);
unsigned int  kernel_stimer3_match(void);

// V85: reload config.txt after GENET. match=1 vs V57. No EL0. No boot emit.
int           kernel_sdload_selftest(void);
int           kernel_sdload_ok(void);
unsigned int  kernel_sdload_match(void);
unsigned int  kernel_sdload_bytes(void);
unsigned int  kernel_sdload_sum(void);

// V86: list FAT32 root after GENET. files>=2 + CONFIG.TXT. No EL0. No boot emit.
int           kernel_sdls_selftest(void);
int           kernel_sdls_ok(void);
unsigned int  kernel_sdls_files(void);
unsigned int  kernel_sdls_config(void);
unsigned int  kernel_sdls_other(void);

// V87: load a second FAT32 root file after GENET. Not CONFIG.TXT. No EL0. No boot emit.
int           kernel_sdfile_selftest(void);
int           kernel_sdfile_ok(void);
unsigned int  kernel_sdfile_name(void);
unsigned int  kernel_sdfile_bytes(void);
unsigned int  kernel_sdfile_sum(void);

// V88: walk FAT32 overlays/ after GENET. files>=1 + first 8.3 name. No EL0. No boot emit.
int           kernel_sdovl_selftest(void);
int           kernel_sdovl_ok(void);
unsigned int  kernel_sdovl_files(void);
unsigned int  kernel_sdovl_name(void);

// V89: load one overlays/ file after GENET. Size cap 65536. No EL0. No boot emit.
int           kernel_sdovf_selftest(void);
int           kernel_sdovf_ok(void);
unsigned int  kernel_sdovf_name(void);
unsigned int  kernel_sdovf_bytes(void);
unsigned int  kernel_sdovf_sum(void);

// V90: load FAT32 root issue.txt by name after GENET. Fail-closed if missing.
int           kernel_sdiss_selftest(void);
int           kernel_sdiss_ok(void);
int           kernel_sdiss_present(void);
unsigned int  kernel_sdiss_name(void);
unsigned int  kernel_sdiss_bytes(void);
unsigned int  kernel_sdiss_sum(void);

// BCM2711 watchdog / PM reset (Sources/Support/watchdog.c). reset_now reboots the
// board immediately; arm/pet give a hang-detector (auto-reboot if not re-armed);
// disable cancels a pending reset.
void watchdog_reset_now(void);
void watchdog_arm_seconds(unsigned int seconds);
void watchdog_pet_seconds(unsigned int seconds);
void watchdog_disable(void);
unsigned long watchdog_reset_count(void);
unsigned long watchdog_arm_count(void);
unsigned long watchdog_pet_count(void);
unsigned long watchdog_disable_count(void);
unsigned int  watchdog_remaining_ticks(void);
int           watchdog_full_reset_armed(void);

// V98: PM watchdog remaining-tick readback after GENET. Arm, read, disable.
// Never reset_now. Fail-closed remaining range. No EL0.
int           kernel_wdog2_selftest(void);
int           kernel_wdog2_ok(void);
int           kernel_wdog2_armed(void);
int           kernel_wdog2_off(void);
unsigned int  kernel_wdog2_remain(void);
