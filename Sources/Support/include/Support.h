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

// Runtime V35 secondary-owned scheduler workers.
// Runtime V34 timer-driven SMP scheduler dispatch.
// Runtime V31 preemptive scheduler substrate remains the base timer tick surface:
// actual Swift job
// execution stays on the existing cooperative executor while the periodic CNTP
// IRQ records preemption opportunities. V33 promoted the bounded run queue
// surface to all A72 cores; V34 lets the timer tick route bounded dispatch
// tokens through those per-core queues. V35 proves C-only secondary workers can
// drain their own per-core queues without entering the Swift runtime.
#define KERNEL_SCHEDULER_VERSION 35U
#define KERNEL_SCHEDULER_CORE_CAPACITY 4U
#define KERNEL_SCHEDULER_RUNQUEUE_CAPACITY 8U
#define KERNEL_SCHEDULER_DISPATCH_TOKEN_BASE 0x3400U
#define KERNEL_SCHEDULER_WORKER_TOKEN_BASE 0x3500U

void kernel_scheduler_init(void);
void kernel_scheduler_start(unsigned long interval_ticks);
void kernel_scheduler_on_timer_irq(void);
void kernel_scheduler_enable_smp_dispatch(void);
void kernel_scheduler_enable_secondary_workers(void);
unsigned int kernel_scheduler_active(void);
unsigned int kernel_scheduler_smp_dispatch_enabled(void);
unsigned int kernel_scheduler_secondary_workers_enabled(void);
unsigned int kernel_scheduler_core_count(void);
unsigned int kernel_scheduler_runqueue_capacity(void);
unsigned int kernel_scheduler_runqueue_count(unsigned int core_id);
int kernel_scheduler_enqueue(unsigned int core_id, unsigned int token);
int kernel_scheduler_dequeue(unsigned int core_id, unsigned int *out_token);
unsigned int kernel_scheduler_runqueue_head(unsigned int core_id);
unsigned int kernel_scheduler_runqueue_tail(unsigned int core_id);
void kernel_scheduler_secondary_worker_tick(unsigned int core_id);
unsigned long kernel_scheduler_dispatch_count(unsigned int core_id);
unsigned long kernel_scheduler_route_count(unsigned int core_id);
unsigned long kernel_scheduler_worker_drain_count(unsigned int core_id);
unsigned long kernel_scheduler_worker_idle_count(unsigned int core_id);
unsigned long kernel_scheduler_total_dispatch_count(void);
unsigned long kernel_scheduler_total_route_count(void);
unsigned long kernel_scheduler_total_worker_drain_count(void);
unsigned long kernel_scheduler_total_worker_idle_count(void);
unsigned long kernel_scheduler_secondary_worker_total(void);
unsigned long kernel_scheduler_secondary_worker_min(void);
unsigned long kernel_scheduler_secondary_worker_max(void);
unsigned long kernel_scheduler_secondary_worker_imbalance(void);
unsigned long kernel_scheduler_fairness_min(void);
unsigned long kernel_scheduler_fairness_max(void);
unsigned long kernel_scheduler_fairness_imbalance(void);
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
int kernel_smp_selftest(void);

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
