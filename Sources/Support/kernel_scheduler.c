#include "Support.h"

//===----------------------------------------------------------------------===//
// Runtime V43 secondary scheduler priority/preemption protocol.
// Runtime V42 secondary scheduler load-balancing protocol.
// Runtime V41 secondary scheduler work-stealing protocol.
// Runtime V40 scheduler backpressure protocol.
// Runtime V39 secondary scheduler handoff protocol.
// Runtime V38 secondary scheduler wake protocol.
// Runtime V37 timer-fed secondary C scheduler jobs.
// Runtime V36 timer-fed secondary scheduler workers.
// Runtime V35 secondary-owned scheduler workers.
// Runtime V34 timer-driven SMP scheduler dispatch.
//
// This is intentionally still a substrate, not cross-core Swift task dispatch.
// It owns a periodic CNTP timer client, records IRQ preemption opportunities on
// core 0, routes bounded dispatch tokens through each online A72 core queue, and
// lets C-only secondary workers drain V35/V36/V37 worker/job tokens from their
// own queues. V38 wakes those parked secondary workers with SEV when timer-fed
// work is available, while the workers park with WFE between scheduler ticks.
// V39 records bounded issue/completion handoff acknowledgements for those
// C-only secondary jobs. V40 proves the bounded per-core queues fail closed:
// saturating a queue records high-water/overflow telemetry, rejects overfill,
// and drains back to zero without corrupting queued tokens. V41 lets idle
// C-only secondary workers steal bounded steal-job tokens from another
// secondary queue, execute them locally, and drain every queue back to zero.
//===----------------------------------------------------------------------===//

typedef struct scheduler_core {
    kernel_spinlock_t lock;
    unsigned long ticks;
    unsigned long irq_ticks;
    unsigned long preemptions;
    unsigned long routes;
    unsigned long dispatches;
    unsigned long worker_drains;
    unsigned long worker_idles;
    unsigned long worker_feeds;
    unsigned long worker_feed_drops;
    unsigned long job_executions;
    unsigned long job_completions;
    unsigned long job_noops;
    unsigned long job_checksum;
    unsigned long handoff_issues;
    unsigned long handoff_completions;
    unsigned long runqueue_high_water;
    unsigned long runqueue_overflows;
    unsigned long steal_attempts;
    unsigned long steal_successes;
    unsigned long steal_source_count;
    unsigned long steal_completions;
    unsigned long balance_attempts;
    unsigned long balance_successes;
    unsigned long balance_source_count;
    unsigned long balance_completions;
    unsigned long priority_low_count;
    unsigned long priority_high_count;
    unsigned long priority_preempt_count;
    unsigned long priority_yield_count;
    unsigned long priority_completions;
    unsigned long enqueues;
    unsigned long dequeues;
    unsigned int queue[KERNEL_SCHEDULER_RUNQUEUE_CAPACITY];
    unsigned int head;
    unsigned int tail;
    unsigned int count;
} scheduler_core;

static scheduler_core cores[KERNEL_SCHEDULER_CORE_CAPACITY];
static unsigned int initialized;
static unsigned int active;
static unsigned int smp_dispatch_enabled;
static unsigned int secondary_workers_enabled;
static unsigned int timer_worker_feed_enabled;
static unsigned int secondary_job_execution_enabled;
static unsigned int secondary_wake_signals_enabled;
static unsigned int secondary_handoffs_enabled;
static unsigned int secondary_work_stealing_enabled;
static unsigned int load_balancing_enabled;
static unsigned int priority_lanes_enabled;
static unsigned int last_dispatch_core;
static unsigned long interval_ticks_value;

static int valid_core(unsigned int core_id) {
    return core_id < KERNEL_SCHEDULER_CORE_CAPACITY;
}

static void lock_core(unsigned int core_id, unsigned long *flags) {
    *flags = irq_save();
    kernel_spinlock_lock(&cores[core_id].lock);
}

static void unlock_core(unsigned int core_id, unsigned long flags) {
    kernel_spinlock_unlock(&cores[core_id].lock);
    irq_restore(flags);
}

static void clear_core_unsafe(scheduler_core *core) {
    core->ticks = 0;
    core->irq_ticks = 0;
    core->preemptions = 0;
    core->routes = 0;
    core->dispatches = 0;
    core->worker_drains = 0;
    core->worker_idles = 0;
    core->worker_feeds = 0;
    core->worker_feed_drops = 0;
    core->job_executions = 0;
    core->job_completions = 0;
    core->job_noops = 0;
    core->job_checksum = 0;
    core->handoff_issues = 0;
    core->handoff_completions = 0;
    core->runqueue_high_water = 0;
    core->runqueue_overflows = 0;
    core->steal_attempts = 0;
    core->steal_successes = 0;
    core->steal_source_count = 0;
    core->steal_completions = 0;
    core->balance_attempts = 0;
    core->balance_successes = 0;
    core->balance_source_count = 0;
    core->balance_completions = 0;
    core->priority_low_count = 0;
    core->priority_high_count = 0;
    core->priority_preempt_count = 0;
    core->priority_yield_count = 0;
    core->priority_completions = 0;
    core->enqueues = 0;
    core->dequeues = 0;
    core->head = 0;
    core->tail = 0;
    core->count = 0;
    for (unsigned int i = 0; i < KERNEL_SCHEDULER_RUNQUEUE_CAPACITY; i++) {
        core->queue[i] = 0;
    }
}

static int queue_push_unsafe(scheduler_core *core, unsigned int token) {
    if (core->count >= KERNEL_SCHEDULER_RUNQUEUE_CAPACITY) {
        core->runqueue_overflows++;
        return 0;
    }
    core->queue[core->tail] = token;
    core->tail = (core->tail + 1U) % KERNEL_SCHEDULER_RUNQUEUE_CAPACITY;
    core->count++;
    if (core->count > core->runqueue_high_water) {
        core->runqueue_high_water = core->count;
    }
    core->enqueues++;
    return 1;
}

static int is_scheduler_priority_token(unsigned int token) {
    unsigned int tag = token & 0xff00U;
    return tag == KERNEL_SCHEDULER_PRIORITY_TOKEN_BASE ||
        tag == (KERNEL_SCHEDULER_PRIORITY_TOKEN_BASE | 0x1000U);
}

static unsigned int scheduler_priority_lane(unsigned int token) {
    return (token >> 12) & 0x1U;
}

static int queue_pop_unsafe(scheduler_core *core, unsigned int *out) {
    if (core->count == 0) {
        return 0;
    }
    if (out != 0) {
        *out = core->queue[core->head];
    }
    core->queue[core->head] = 0;
    core->head = (core->head + 1U) % KERNEL_SCHEDULER_RUNQUEUE_CAPACITY;
    core->count--;
    core->dequeues++;
    return 1;
}

static int queue_find_priority_high_offset_unsafe(const scheduler_core *core, unsigned int *out_offset) {
    for (unsigned int offset = 0; offset < core->count; offset++) {
        unsigned int index = (core->head + offset) % KERNEL_SCHEDULER_RUNQUEUE_CAPACITY;
        unsigned int token = core->queue[index];
        if (is_scheduler_priority_token(token) &&
            scheduler_priority_lane(token) == KERNEL_SCHEDULER_PRIORITY_LANE_HIGH) {
            *out_offset = offset;
            return 1;
        }
    }
    return 0;
}

static int queue_remove_offset_unsafe(scheduler_core *core, unsigned int offset, unsigned int *out_token) {
    if (offset >= core->count) {
        return 0;
    }
    unsigned int remove_index = (core->head + offset) % KERNEL_SCHEDULER_RUNQUEUE_CAPACITY;
    if (out_token != 0) {
        *out_token = core->queue[remove_index];
    }
    for (unsigned int shift = offset; shift + 1U < core->count; shift++) {
        unsigned int src = (core->head + shift + 1U) % KERNEL_SCHEDULER_RUNQUEUE_CAPACITY;
        unsigned int dst = (core->head + shift) % KERNEL_SCHEDULER_RUNQUEUE_CAPACITY;
        core->queue[dst] = core->queue[src];
    }
    unsigned int tail_index = (core->head + core->count - 1U) % KERNEL_SCHEDULER_RUNQUEUE_CAPACITY;
    core->queue[tail_index] = 0;
    core->count--;
    if (core->count == 0) {
        core->head = 0;
        core->tail = 0;
    } else {
        core->tail = tail_index;
    }
    core->dequeues++;
    return 1;
}

void kernel_scheduler_init(void) {
    unsigned long flags = irq_save();
    for (unsigned int i = 0; i < KERNEL_SCHEDULER_CORE_CAPACITY; i++) {
        kernel_spinlock_init(&cores[i].lock);
        clear_core_unsafe(&cores[i]);
    }
    initialized = 1;
    active = 0;
    smp_dispatch_enabled = 0;
    secondary_workers_enabled = 0;
    timer_worker_feed_enabled = 0;
    secondary_job_execution_enabled = 0;
    secondary_wake_signals_enabled = 0;
    secondary_handoffs_enabled = 0;
    secondary_work_stealing_enabled = 0;
    load_balancing_enabled = 0;
    priority_lanes_enabled = 0;
    last_dispatch_core = 0;
    interval_ticks_value = 0;
    irq_restore(flags);
}

void kernel_scheduler_start(unsigned long interval_ticks) {
    if (!initialized) {
        kernel_scheduler_init();
    }
    if (interval_ticks == 0) {
        interval_ticks = 1;
    }

    unsigned long flags = irq_save();
    interval_ticks_value = interval_ticks;
    active = 1;
    irq_restore(flags);

    kernel_timer_set_deadline(KERNEL_TIMER_CLIENT_SCHEDULER,
                              kernel_timer_now() + interval_ticks);
}

void kernel_scheduler_enable_smp_dispatch(void) {
    if (!initialized) {
        kernel_scheduler_init();
    }
    unsigned long flags = irq_save();
    smp_dispatch_enabled = 1;
    irq_restore(flags);
}

void kernel_scheduler_enable_secondary_workers(void) {
    if (!initialized) {
        kernel_scheduler_init();
    }
    unsigned long flags = irq_save();
    secondary_workers_enabled = 1;
    irq_restore(flags);
}

void kernel_scheduler_enable_timer_worker_feed(void) {
    if (!initialized) {
        kernel_scheduler_init();
    }
    unsigned long flags = irq_save();
    timer_worker_feed_enabled = 1;
    irq_restore(flags);
}

void kernel_scheduler_enable_secondary_job_execution(void) {
    if (!initialized) {
        kernel_scheduler_init();
    }
    unsigned long flags = irq_save();
    secondary_job_execution_enabled = 1;
    irq_restore(flags);
}

void kernel_scheduler_enable_secondary_wake_signals(void) {
    if (!initialized) {
        kernel_scheduler_init();
    }
    unsigned long flags = irq_save();
    secondary_wake_signals_enabled = 1;
    irq_restore(flags);
}

void kernel_scheduler_enable_secondary_handoffs(void) {
    if (!initialized) {
        kernel_scheduler_init();
    }
    unsigned long flags = irq_save();
    secondary_handoffs_enabled = 1;
    irq_restore(flags);
}

void kernel_scheduler_enable_secondary_work_stealing(void) {
    if (!initialized) {
        kernel_scheduler_init();
    }
    unsigned long flags = irq_save();
    secondary_work_stealing_enabled = 1;
    irq_restore(flags);
}

void kernel_scheduler_enable_load_balancing(void) {
    if (!initialized) {
        kernel_scheduler_init();
    }
    unsigned long flags = irq_save();
    load_balancing_enabled = 1;
    irq_restore(flags);
}

void kernel_scheduler_enable_priority_lanes(void) {
    if (!initialized) {
        kernel_scheduler_init();
    }
    unsigned long flags = irq_save();
    priority_lanes_enabled = 1;
    irq_restore(flags);
}

static unsigned int scheduler_job_token_for_core(unsigned int core_id) {
    return KERNEL_SCHEDULER_JOB_TOKEN_BASE |
        (KERNEL_SCHEDULER_JOB_OP_CHECKSUM << 4) |
        (core_id & 0x0fU);
}

static unsigned int scheduler_steal_token_for_source(unsigned int source_core, unsigned int sequence) {
    return KERNEL_SCHEDULER_STEAL_TOKEN_BASE |
        ((sequence & 0xffU) << 4) |
        (source_core & 0x0fU);
}

static unsigned int scheduler_balance_token_for_source(unsigned int source_core, unsigned int sequence) {
    return KERNEL_SCHEDULER_BALANCE_TOKEN_BASE |
        ((sequence & 0xffU) << 4) |
        (source_core & 0x0fU);
}

static unsigned int scheduler_priority_token_for_core(unsigned int core_id, unsigned int lane, unsigned int sequence) {
    return KERNEL_SCHEDULER_PRIORITY_TOKEN_BASE |
        ((lane & 0x1U) << 12) |
        ((sequence & 0xffU) << 4) |
        (core_id & 0x0fU);
}

static unsigned int worker_token_for_core(unsigned int core_id) {
    if (secondary_job_execution_enabled) {
        return scheduler_job_token_for_core(core_id);
    }
    return KERNEL_SCHEDULER_WORKER_TOKEN_BASE | (core_id & 0x0fU);
}

static int is_scheduler_job_token(unsigned int token) {
    return (token & 0xff00U) == KERNEL_SCHEDULER_JOB_TOKEN_BASE;
}

static int is_scheduler_steal_token(unsigned int token) {
    return (token & 0xff00U) == KERNEL_SCHEDULER_STEAL_TOKEN_BASE;
}

static int is_scheduler_balance_token(unsigned int token) {
    return (token & 0xff00U) == KERNEL_SCHEDULER_BALANCE_TOKEN_BASE;
}

static int is_worker_token(unsigned int token) {
    return ((token & 0xfff0U) == KERNEL_SCHEDULER_WORKER_TOKEN_BASE) ||
        is_scheduler_job_token(token);
}

static void signal_secondary_work_for_core(unsigned int core_id) {
    if (!secondary_wake_signals_enabled || !valid_core(core_id) || core_id == 0) {
        return;
    }
    kernel_smp_signal_scheduler_work(1U << core_id);
}

static void record_secondary_handoff_issue(unsigned int core_id, unsigned int token) {
    if (!secondary_handoffs_enabled ||
        !valid_core(core_id) ||
        core_id == 0 ||
        !is_scheduler_job_token(token)) {
        return;
    }
    unsigned long flags = 0;
    lock_core(core_id, &flags);
    cores[core_id].handoff_issues++;
    unlock_core(core_id, flags);
}

static void record_secondary_handoff_completion(unsigned int core_id) {
    if (!secondary_handoffs_enabled || !valid_core(core_id) || core_id == 0) {
        return;
    }
    cores[core_id].handoff_completions++;
}

static void execute_scheduler_job_for_core(unsigned int core_id, unsigned int token) {
    if (!valid_core(core_id) || core_id == 0) {
        return;
    }

    unsigned int token_core = token & 0x0fU;
    unsigned int op = (token >> 4) & 0x0fU;
    unsigned long flags = 0;
    lock_core(core_id, &flags);
    cores[core_id].job_executions++;

    if (!secondary_job_execution_enabled ||
        !is_scheduler_job_token(token) ||
        token_core != core_id ||
        op != KERNEL_SCHEDULER_JOB_OP_CHECKSUM) {
        cores[core_id].job_noops++;
        unlock_core(core_id, flags);
        return;
    }

    unsigned long sequence = cores[core_id].job_executions;
    unsigned long mix = ((unsigned long)token << 16) ^
        (sequence * 131UL) ^
        ((unsigned long)core_id * 0x9e37UL);
    if (mix == 0) {
        mix = (unsigned long)core_id + 1UL;
    }
    cores[core_id].job_checksum += mix;
    if (cores[core_id].job_checksum == 0) {
        cores[core_id].job_checksum = mix | 1UL;
    }
    cores[core_id].job_completions++;
    record_secondary_handoff_completion(core_id);
    unlock_core(core_id, flags);
}

static void execute_stolen_scheduler_job_for_core(unsigned int core_id, unsigned int token, unsigned int source_core) {
    if (!valid_core(core_id) || core_id == 0 || !valid_core(source_core) || source_core == 0) {
        return;
    }

    unsigned long flags = 0;
    lock_core(core_id, &flags);
    cores[core_id].job_executions++;
    unsigned long sequence = cores[core_id].job_executions;
    unsigned long mix = ((unsigned long)token << 16) ^
        (sequence * 257UL) ^
        ((unsigned long)core_id * 0x9e37UL) ^
        ((unsigned long)source_core * 0x51edUL);
    if (mix == 0) {
        mix = (unsigned long)core_id + (unsigned long)source_core + 1UL;
    }
    cores[core_id].job_checksum += mix;
    if (cores[core_id].job_checksum == 0) {
        cores[core_id].job_checksum = mix | 1UL;
    }
    cores[core_id].job_completions++;
    cores[core_id].steal_completions++;
    unlock_core(core_id, flags);
}

static void execute_balanced_scheduler_job_for_core(unsigned int core_id, unsigned int token, unsigned int source_core) {
    if (!valid_core(core_id) || core_id == 0 || !valid_core(source_core) || source_core == 0) {
        return;
    }

    unsigned long flags = 0;
    lock_core(core_id, &flags);
    cores[core_id].job_executions++;
    unsigned long sequence = cores[core_id].job_executions;
    unsigned long mix = ((unsigned long)token << 16) ^
        (sequence * 389UL) ^
        ((unsigned long)core_id * 0x9e37UL) ^
        ((unsigned long)source_core * 0x85ebUL);
    if (mix == 0) {
        mix = (unsigned long)core_id + (unsigned long)source_core + 3UL;
    }
    cores[core_id].job_checksum += mix;
    if (cores[core_id].job_checksum == 0) {
        cores[core_id].job_checksum = mix | 1UL;
    }
    cores[core_id].job_completions++;
    cores[core_id].balance_completions++;
    unlock_core(core_id, flags);
}

static void execute_priority_job_for_core(unsigned int core_id, unsigned int token) {
    if (!valid_core(core_id) || core_id == 0 || !is_scheduler_priority_token(token)) {
        return;
    }

    unsigned long flags = 0;
    lock_core(core_id, &flags);
    cores[core_id].job_executions++;
    unsigned long sequence = cores[core_id].job_executions;
    unsigned long lane = scheduler_priority_lane(token);
    unsigned long mix = ((unsigned long)token << 16) ^
        (sequence * 521UL) ^
        ((unsigned long)core_id * 0x9e37UL) ^
        (lane * 0x27d4eb2dUL);
    if (mix == 0) {
        mix = (unsigned long)core_id + lane + 5UL;
    }
    cores[core_id].job_checksum += mix;
    if (cores[core_id].job_checksum == 0) {
        cores[core_id].job_checksum = mix | 1UL;
    }
    cores[core_id].job_completions++;
    cores[core_id].priority_completions++;
    unlock_core(core_id, flags);
}

int kernel_scheduler_try_balance_work(unsigned int core_id) {
    if (!load_balancing_enabled ||
        !valid_core(core_id) ||
        core_id == 0 ||
        !kernel_smp_core_online(core_id)) {
        return 0;
    }

    unsigned long flags = 0;
    lock_core(core_id, &flags);
    unsigned int dest_count = cores[core_id].count;
    cores[core_id].balance_attempts++;
    unlock_core(core_id, flags);

    if (dest_count >= KERNEL_SCHEDULER_RUNQUEUE_CAPACITY) {
        return 0;
    }

    for (unsigned int offset = 1; offset < KERNEL_SCHEDULER_CORE_CAPACITY; offset++) {
        unsigned int source_core = ((core_id + offset - 1U) % (KERNEL_SCHEDULER_CORE_CAPACITY - 1U)) + 1U;
        if (source_core == core_id || !kernel_smp_core_online(source_core)) {
            continue;
        }

        lock_core(source_core, &flags);
        if (cores[source_core].count < 2U ||
            !is_scheduler_balance_token(cores[source_core].queue[cores[source_core].head])) {
            unlock_core(source_core, flags);
            continue;
        }
        unsigned int token = 0;
        (void)queue_pop_unsafe(&cores[source_core], &token);
        cores[source_core].balance_source_count++;
        unlock_core(source_core, flags);

        lock_core(core_id, &flags);
        cores[core_id].balance_successes++;
        unlock_core(core_id, flags);

        execute_balanced_scheduler_job_for_core(core_id, token, source_core);
        return 1;
    }

    return 0;
}

int kernel_scheduler_try_preempt_priority_work(unsigned int core_id) {
    if (!priority_lanes_enabled ||
        !valid_core(core_id) ||
        core_id == 0 ||
        !kernel_smp_core_online(core_id)) {
        return 0;
    }

    unsigned long flags = 0;
    lock_core(core_id, &flags);
    if (cores[core_id].count == 0) {
        unlock_core(core_id, flags);
        return 0;
    }

    unsigned int head_token = cores[core_id].queue[cores[core_id].head];
    int head_is_low = is_scheduler_priority_token(head_token) &&
        scheduler_priority_lane(head_token) == KERNEL_SCHEDULER_PRIORITY_LANE_LOW;
    unsigned int high_offset = 0;
    int has_high_behind = head_is_low &&
        queue_find_priority_high_offset_unsafe(&cores[core_id], &high_offset) &&
        high_offset > 0U;

    if (has_high_behind) {
        unsigned int token = 0;
        cores[core_id].priority_yield_count++;
        cores[core_id].priority_preempt_count++;
        (void)queue_remove_offset_unsafe(&cores[core_id], high_offset, &token);
        unlock_core(core_id, flags);
        execute_priority_job_for_core(core_id, token);
        return 1;
    }

    if (is_scheduler_priority_token(head_token) &&
        scheduler_priority_lane(head_token) == KERNEL_SCHEDULER_PRIORITY_LANE_HIGH) {
        unsigned int token = 0;
        cores[core_id].priority_preempt_count++;
        (void)queue_pop_unsafe(&cores[core_id], &token);
        unlock_core(core_id, flags);
        execute_priority_job_for_core(core_id, token);
        return 1;
    }

    if (head_is_low) {
        unsigned int token = 0;
        (void)queue_pop_unsafe(&cores[core_id], &token);
        unlock_core(core_id, flags);
        execute_priority_job_for_core(core_id, token);
        return 1;
    }

    unlock_core(core_id, flags);
    return 0;
}

static int kernel_scheduler_try_steal_work(unsigned int core_id) {
    if (!secondary_work_stealing_enabled ||
        !valid_core(core_id) ||
        core_id == 0 ||
        !kernel_smp_core_online(core_id)) {
        return 0;
    }

    unsigned long flags = 0;
    lock_core(core_id, &flags);
    cores[core_id].steal_attempts++;
    unlock_core(core_id, flags);

    for (unsigned int offset = 1; offset < KERNEL_SCHEDULER_CORE_CAPACITY; offset++) {
        unsigned int source_core = ((core_id + offset - 1U) % (KERNEL_SCHEDULER_CORE_CAPACITY - 1U)) + 1U;
        if (source_core == core_id || !kernel_smp_core_online(source_core)) {
            continue;
        }

        unsigned int token = 0;
        lock_core(source_core, &flags);
        if (cores[source_core].count == 0 ||
            !is_scheduler_steal_token(cores[source_core].queue[cores[source_core].head])) {
            unlock_core(source_core, flags);
            continue;
        }
        (void)queue_pop_unsafe(&cores[source_core], &token);
        cores[source_core].steal_source_count++;
        unlock_core(source_core, flags);

        lock_core(core_id, &flags);
        cores[core_id].steal_successes++;
        unlock_core(core_id, flags);

        execute_stolen_scheduler_job_for_core(core_id, token, source_core);
        return 1;
    }

    return 0;
}

static int enqueue_worker_probe_for_core(unsigned int core_id) {
    if (!valid_core(core_id) || core_id == 0 || !kernel_smp_core_online(core_id)) {
        return 0;
    }
    if (kernel_scheduler_runqueue_count(core_id) != 0) {
        return 0;
    }
    unsigned int token = worker_token_for_core(core_id);
    int ok = kernel_scheduler_enqueue(core_id, token);
    if (ok) {
        record_secondary_handoff_issue(core_id, token);
        signal_secondary_work_for_core(core_id);
    }
    return ok;
}

static void set_timer_worker_feed_enabled(unsigned int value) {
    unsigned long flags = irq_save();
    timer_worker_feed_enabled = value ? 1U : 0U;
    irq_restore(flags);
}

static void set_smp_dispatch_enabled(unsigned int value) {
    unsigned long flags = irq_save();
    smp_dispatch_enabled = value ? 1U : 0U;
    irq_restore(flags);
}

static int timer_worker_feed_is_enabled(void) {
    unsigned long flags = irq_save();
    unsigned int enabled = timer_worker_feed_enabled;
    irq_restore(flags);
    return enabled != 0;
}

static int secondary_queues_empty(void) {
    for (unsigned int core_id = 1; core_id < KERNEL_SCHEDULER_CORE_CAPACITY; core_id++) {
        if (kernel_scheduler_runqueue_count(core_id) != 0) {
            return 0;
        }
    }
    return 1;
}

static void wait_for_secondary_queues_empty(void) {
    for (unsigned int spin = 0; spin < 200000U; spin++) {
        if (secondary_queues_empty()) {
            return;
        }
        if ((spin & 0x3ffU) == 0U) {
            kernel_smp_signal_scheduler_work(KERNEL_SMP_SECONDARY_MASK);
        }
        __asm__ volatile("nop" ::: "memory");
    }
}

static void route_dispatch_for_core(unsigned int core_id, unsigned long tick) {
    if (!valid_core(core_id) || !kernel_smp_core_online(core_id)) {
        return;
    }

    unsigned int token = KERNEL_SCHEDULER_DISPATCH_TOKEN_BASE |
        ((unsigned int)(tick & 0xffU) << 8) |
        (core_id & 0xffU);
    if (!kernel_scheduler_enqueue(core_id, token)) {
        return;
    }

    unsigned int dispatched = 0;
    unsigned long flags = 0;
    lock_core(core_id, &flags);
    cores[core_id].routes++;
    unlock_core(core_id, flags);

    if (kernel_scheduler_dequeue(core_id, &dispatched) && dispatched == token) {
        lock_core(core_id, &flags);
        cores[core_id].dispatches++;
        last_dispatch_core = core_id;
        unlock_core(core_id, flags);
    }
}

static void route_dispatch_for_online_cores(unsigned long tick) {
    if (!smp_dispatch_enabled) {
        return;
    }
    for (unsigned int core_id = 0; core_id < KERNEL_SCHEDULER_CORE_CAPACITY; core_id++) {
        route_dispatch_for_core(core_id, tick);
    }
}

static int route_worker_feed_for_core(unsigned int core_id) {
    if (!valid_core(core_id) || core_id == 0 || !kernel_smp_core_online(core_id)) {
        return 0;
    }

    unsigned long flags = 0;
    lock_core(core_id, &flags);
    if (cores[core_id].count != 0) {
        cores[core_id].worker_feed_drops++;
        unlock_core(core_id, flags);
        return 0;
    }
    unlock_core(core_id, flags);

    unsigned int token = worker_token_for_core(core_id);
    if (kernel_scheduler_enqueue(core_id, token)) {
        lock_core(core_id, &flags);
        cores[core_id].worker_feeds++;
        unlock_core(core_id, flags);
        record_secondary_handoff_issue(core_id, token);
        signal_secondary_work_for_core(core_id);
        return 1;
    }

    lock_core(core_id, &flags);
    cores[core_id].worker_feed_drops++;
    unlock_core(core_id, flags);
    return 0;
}

static void route_worker_feed_for_online_secondary_cores(void) {
    if (!timer_worker_feed_is_enabled()) {
        return;
    }
    for (unsigned int core_id = 1; core_id < KERNEL_SCHEDULER_CORE_CAPACITY; core_id++) {
        (void)route_worker_feed_for_core(core_id);
    }
}

void kernel_scheduler_on_timer_irq(void) {
    if (!initialized || !active) {
        return;
    }

    unsigned long now = kernel_timer_now();
    unsigned long deadline = kernel_timer_deadline_ticks(KERNEL_TIMER_CLIENT_SCHEDULER);
    unsigned long interval = interval_ticks_value;
    if (deadline == 0 || now < deadline) {
        return;
    }
    if (interval == 0) {
        interval = 1;
    }

    kernel_spinlock_lock(&cores[0].lock);
    cores[0].ticks++;
    cores[0].irq_ticks++;
    cores[0].preemptions++;
    unsigned long tick = cores[0].ticks;
    kernel_spinlock_unlock(&cores[0].lock);

    route_dispatch_for_online_cores(tick);
    route_worker_feed_for_online_secondary_cores();
    kernel_timer_set_deadline(KERNEL_TIMER_CLIENT_SCHEDULER, now + interval);
}

unsigned int kernel_scheduler_active(void) {
    unsigned long flags = irq_save();
    unsigned int value = active;
    irq_restore(flags);
    return value;
}

unsigned int kernel_scheduler_smp_dispatch_enabled(void) {
    unsigned long flags = irq_save();
    unsigned int value = smp_dispatch_enabled;
    irq_restore(flags);
    return value;
}

unsigned int kernel_scheduler_secondary_workers_enabled(void) {
    unsigned long flags = irq_save();
    unsigned int value = secondary_workers_enabled;
    irq_restore(flags);
    return value;
}

unsigned int kernel_scheduler_timer_worker_feed_enabled(void) {
    unsigned long flags = irq_save();
    unsigned int value = timer_worker_feed_enabled;
    irq_restore(flags);
    return value;
}

unsigned int kernel_scheduler_secondary_job_execution_enabled(void) {
    unsigned long flags = irq_save();
    unsigned int value = secondary_job_execution_enabled;
    irq_restore(flags);
    return value;
}

unsigned int kernel_scheduler_secondary_wake_signals_enabled(void) {
    unsigned long flags = irq_save();
    unsigned int value = secondary_wake_signals_enabled;
    irq_restore(flags);
    return value;
}

unsigned int kernel_scheduler_secondary_handoffs_enabled(void) {
    unsigned long flags = irq_save();
    unsigned int value = secondary_handoffs_enabled;
    irq_restore(flags);
    return value;
}

unsigned int kernel_scheduler_secondary_work_stealing_enabled(void) {
    unsigned long flags = irq_save();
    unsigned int value = secondary_work_stealing_enabled;
    irq_restore(flags);
    return value;
}

unsigned int kernel_scheduler_load_balancing_enabled(void) {
    unsigned long flags = irq_save();
    unsigned int value = load_balancing_enabled;
    irq_restore(flags);
    return value;
}

unsigned int kernel_scheduler_priority_lanes_enabled(void) {
    unsigned long flags = irq_save();
    unsigned int value = priority_lanes_enabled;
    irq_restore(flags);
    return value;
}

unsigned int kernel_scheduler_core_count(void) {
    return KERNEL_SCHEDULER_CORE_CAPACITY;
}

unsigned int kernel_scheduler_runqueue_capacity(void) {
    return KERNEL_SCHEDULER_RUNQUEUE_CAPACITY;
}

unsigned int kernel_scheduler_runqueue_count(unsigned int core_id) {
    if (!valid_core(core_id)) {
        return 0;
    }
    unsigned long flags = 0;
    lock_core(core_id, &flags);
    unsigned int count = cores[core_id].count;
    unlock_core(core_id, flags);
    return count;
}

unsigned int kernel_scheduler_secondary_has_runnable_work(unsigned int core_id) {
    if (!valid_core(core_id) || core_id == 0) {
        return 0;
    }
    unsigned long flags = 0;
    lock_core(core_id, &flags);
    unsigned int head_token = cores[core_id].queue[cores[core_id].head];
    unsigned int runnable = cores[core_id].count != 0 &&
        (is_worker_token(head_token) ||
         is_scheduler_steal_token(head_token) ||
         is_scheduler_balance_token(head_token) ||
         (priority_lanes_enabled && is_scheduler_priority_token(head_token))) ? 1U : 0U;
    unlock_core(core_id, flags);
    return runnable;
}

int kernel_scheduler_enqueue(unsigned int core_id, unsigned int token) {
    if (!valid_core(core_id) || token == 0) {
        return 0;
    }
    unsigned long flags = 0;
    lock_core(core_id, &flags);
    int ok = queue_push_unsafe(&cores[core_id], token);
    if (ok && priority_lanes_enabled && is_scheduler_priority_token(token)) {
        if (scheduler_priority_lane(token) == KERNEL_SCHEDULER_PRIORITY_LANE_HIGH) {
            cores[core_id].priority_high_count++;
        } else {
            cores[core_id].priority_low_count++;
        }
    }
    unlock_core(core_id, flags);
    return ok;
}

int kernel_scheduler_dequeue(unsigned int core_id, unsigned int *out_token) {
    if (!valid_core(core_id)) {
        return 0;
    }
    unsigned long flags = 0;
    lock_core(core_id, &flags);
    int ok = queue_pop_unsafe(&cores[core_id], out_token);
    unlock_core(core_id, flags);
    return ok;
}

unsigned int kernel_scheduler_runqueue_head(unsigned int core_id) {
    if (!valid_core(core_id)) {
        return 0;
    }
    unsigned long flags = 0;
    lock_core(core_id, &flags);
    unsigned int head = cores[core_id].head;
    unlock_core(core_id, flags);
    return head;
}

unsigned int kernel_scheduler_runqueue_tail(unsigned int core_id) {
    if (!valid_core(core_id)) {
        return 0;
    }
    unsigned long flags = 0;
    lock_core(core_id, &flags);
    unsigned int tail = cores[core_id].tail;
    unlock_core(core_id, flags);
    return tail;
}

unsigned long kernel_scheduler_runqueue_high_water(unsigned int core_id) {
    if (!valid_core(core_id)) {
        return 0;
    }
    unsigned long flags = 0;
    lock_core(core_id, &flags);
    unsigned long value = cores[core_id].runqueue_high_water;
    unlock_core(core_id, flags);
    return value;
}

unsigned long kernel_scheduler_runqueue_overflow_count(unsigned int core_id) {
    if (!valid_core(core_id)) {
        return 0;
    }
    unsigned long flags = 0;
    lock_core(core_id, &flags);
    unsigned long value = cores[core_id].runqueue_overflows;
    unlock_core(core_id, flags);
    return value;
}

void kernel_scheduler_secondary_worker_tick(unsigned int core_id) {
    if (!secondary_workers_enabled || !valid_core(core_id) || core_id == 0) {
        return;
    }

    unsigned long flags = 0;
    lock_core(core_id, &flags);
    if (cores[core_id].count == 0) {
        unlock_core(core_id, flags);
        if (kernel_scheduler_try_balance_work(core_id)) {
            lock_core(core_id, &flags);
            cores[core_id].worker_drains++;
            unlock_core(core_id, flags);
            return;
        }
        if (kernel_scheduler_try_steal_work(core_id)) {
            lock_core(core_id, &flags);
            cores[core_id].worker_drains++;
            unlock_core(core_id, flags);
            return;
        }
        lock_core(core_id, &flags);
        cores[core_id].worker_idles++;
        unlock_core(core_id, flags);
        return;
    }

    unsigned int expected = cores[core_id].queue[cores[core_id].head];
    if (!is_worker_token(expected)) {
        cores[core_id].worker_idles++;
        unlock_core(core_id, flags);
        return;
    }
    unlock_core(core_id, flags);

    unsigned int token = 0;
    if (kernel_scheduler_dequeue(core_id, &token) && token == expected) {
        if (is_scheduler_job_token(token)) {
            execute_scheduler_job_for_core(core_id, token);
        }
        lock_core(core_id, &flags);
        cores[core_id].worker_drains++;
        unlock_core(core_id, flags);
    } else {
        lock_core(core_id, &flags);
        cores[core_id].worker_idles++;
        unlock_core(core_id, flags);
    }
}

unsigned long kernel_scheduler_dispatch_count(unsigned int core_id) {
    if (!valid_core(core_id)) {
        return 0;
    }
    unsigned long flags = 0;
    lock_core(core_id, &flags);
    unsigned long count = cores[core_id].dispatches;
    unlock_core(core_id, flags);
    return count;
}

unsigned long kernel_scheduler_route_count(unsigned int core_id) {
    if (!valid_core(core_id)) {
        return 0;
    }
    unsigned long flags = 0;
    lock_core(core_id, &flags);
    unsigned long count = cores[core_id].routes;
    unlock_core(core_id, flags);
    return count;
}

unsigned long kernel_scheduler_worker_drain_count(unsigned int core_id) {
    if (!valid_core(core_id)) {
        return 0;
    }
    unsigned long flags = 0;
    lock_core(core_id, &flags);
    unsigned long count = cores[core_id].worker_drains;
    unlock_core(core_id, flags);
    return count;
}

unsigned long kernel_scheduler_worker_idle_count(unsigned int core_id) {
    if (!valid_core(core_id)) {
        return 0;
    }
    unsigned long flags = 0;
    lock_core(core_id, &flags);
    unsigned long count = cores[core_id].worker_idles;
    unlock_core(core_id, flags);
    return count;
}

unsigned long kernel_scheduler_worker_feed_count(unsigned int core_id) {
    if (!valid_core(core_id)) {
        return 0;
    }
    unsigned long flags = 0;
    lock_core(core_id, &flags);
    unsigned long count = cores[core_id].worker_feeds;
    unlock_core(core_id, flags);
    return count;
}

unsigned long kernel_scheduler_worker_feed_drop_count(unsigned int core_id) {
    if (!valid_core(core_id)) {
        return 0;
    }
    unsigned long flags = 0;
    lock_core(core_id, &flags);
    unsigned long count = cores[core_id].worker_feed_drops;
    unlock_core(core_id, flags);
    return count;
}

unsigned long kernel_scheduler_secondary_job_execution_count(unsigned int core_id) {
    if (!valid_core(core_id)) {
        return 0;
    }
    unsigned long flags = 0;
    lock_core(core_id, &flags);
    unsigned long count = cores[core_id].job_executions;
    unlock_core(core_id, flags);
    return count;
}

unsigned long kernel_scheduler_secondary_job_completion_count(unsigned int core_id) {
    if (!valid_core(core_id)) {
        return 0;
    }
    unsigned long flags = 0;
    lock_core(core_id, &flags);
    unsigned long count = cores[core_id].job_completions;
    unlock_core(core_id, flags);
    return count;
}

unsigned long kernel_scheduler_secondary_job_noop_count(unsigned int core_id) {
    if (!valid_core(core_id)) {
        return 0;
    }
    unsigned long flags = 0;
    lock_core(core_id, &flags);
    unsigned long count = cores[core_id].job_noops;
    unlock_core(core_id, flags);
    return count;
}

unsigned long kernel_scheduler_secondary_job_checksum(unsigned int core_id) {
    if (!valid_core(core_id)) {
        return 0;
    }
    unsigned long flags = 0;
    lock_core(core_id, &flags);
    unsigned long value = cores[core_id].job_checksum;
    unlock_core(core_id, flags);
    return value;
}

unsigned long kernel_scheduler_total_dispatch_count(void) {
    unsigned long total = 0;
    for (unsigned int i = 0; i < KERNEL_SCHEDULER_CORE_CAPACITY; i++) {
        total += kernel_scheduler_dispatch_count(i);
    }
    return total;
}

unsigned long kernel_scheduler_total_route_count(void) {
    unsigned long total = 0;
    for (unsigned int i = 0; i < KERNEL_SCHEDULER_CORE_CAPACITY; i++) {
        total += kernel_scheduler_route_count(i);
    }
    return total;
}

unsigned long kernel_scheduler_total_worker_drain_count(void) {
    unsigned long total = 0;
    for (unsigned int i = 0; i < KERNEL_SCHEDULER_CORE_CAPACITY; i++) {
        total += kernel_scheduler_worker_drain_count(i);
    }
    return total;
}

unsigned long kernel_scheduler_total_worker_idle_count(void) {
    unsigned long total = 0;
    for (unsigned int i = 0; i < KERNEL_SCHEDULER_CORE_CAPACITY; i++) {
        total += kernel_scheduler_worker_idle_count(i);
    }
    return total;
}

unsigned long kernel_scheduler_total_worker_feed_count(void) {
    unsigned long total = 0;
    for (unsigned int i = 0; i < KERNEL_SCHEDULER_CORE_CAPACITY; i++) {
        total += kernel_scheduler_worker_feed_count(i);
    }
    return total;
}

unsigned long kernel_scheduler_total_worker_feed_drop_count(void) {
    unsigned long total = 0;
    for (unsigned int i = 0; i < KERNEL_SCHEDULER_CORE_CAPACITY; i++) {
        total += kernel_scheduler_worker_feed_drop_count(i);
    }
    return total;
}

unsigned long kernel_scheduler_runqueue_overflow_total(void) {
    unsigned long total = 0;
    for (unsigned int i = 0; i < KERNEL_SCHEDULER_CORE_CAPACITY; i++) {
        total += kernel_scheduler_runqueue_overflow_count(i);
    }
    return total;
}

unsigned long kernel_scheduler_runqueue_high_water_max(void) {
    unsigned long max = 0;
    for (unsigned int i = 0; i < KERNEL_SCHEDULER_CORE_CAPACITY; i++) {
        unsigned long high = kernel_scheduler_runqueue_high_water(i);
        if (high > max) {
            max = high;
        }
    }
    return max;
}

unsigned long kernel_scheduler_steal_attempt_count(unsigned int core_id) {
    if (!valid_core(core_id)) {
        return 0;
    }
    unsigned long flags = 0;
    lock_core(core_id, &flags);
    unsigned long value = cores[core_id].steal_attempts;
    unlock_core(core_id, flags);
    return value;
}

unsigned long kernel_scheduler_steal_success_count(unsigned int core_id) {
    if (!valid_core(core_id)) {
        return 0;
    }
    unsigned long flags = 0;
    lock_core(core_id, &flags);
    unsigned long value = cores[core_id].steal_successes;
    unlock_core(core_id, flags);
    return value;
}

unsigned long kernel_scheduler_steal_source_count(unsigned int core_id) {
    if (!valid_core(core_id)) {
        return 0;
    }
    unsigned long flags = 0;
    lock_core(core_id, &flags);
    unsigned long value = cores[core_id].steal_source_count;
    unlock_core(core_id, flags);
    return value;
}

unsigned long kernel_scheduler_steal_completion_count(unsigned int core_id) {
    if (!valid_core(core_id)) {
        return 0;
    }
    unsigned long flags = 0;
    lock_core(core_id, &flags);
    unsigned long value = cores[core_id].steal_completions;
    unlock_core(core_id, flags);
    return value;
}

unsigned long kernel_scheduler_steal_total(void) {
    unsigned long total = 0;
    for (unsigned int i = 1; i < KERNEL_SCHEDULER_CORE_CAPACITY; i++) {
        total += kernel_scheduler_steal_success_count(i);
    }
    return total;
}

unsigned long kernel_scheduler_steal_completion_total(void) {
    unsigned long total = 0;
    for (unsigned int i = 1; i < KERNEL_SCHEDULER_CORE_CAPACITY; i++) {
        total += kernel_scheduler_steal_completion_count(i);
    }
    return total;
}

unsigned long kernel_scheduler_balance_attempt_count(unsigned int core_id) {
    if (!valid_core(core_id)) {
        return 0;
    }
    unsigned long flags = 0;
    lock_core(core_id, &flags);
    unsigned long value = cores[core_id].balance_attempts;
    unlock_core(core_id, flags);
    return value;
}

unsigned long kernel_scheduler_balance_success_count(unsigned int core_id) {
    if (!valid_core(core_id)) {
        return 0;
    }
    unsigned long flags = 0;
    lock_core(core_id, &flags);
    unsigned long value = cores[core_id].balance_successes;
    unlock_core(core_id, flags);
    return value;
}

unsigned long kernel_scheduler_balance_source_count(unsigned int core_id) {
    if (!valid_core(core_id)) {
        return 0;
    }
    unsigned long flags = 0;
    lock_core(core_id, &flags);
    unsigned long value = cores[core_id].balance_source_count;
    unlock_core(core_id, flags);
    return value;
}

unsigned long kernel_scheduler_balance_completion_count(unsigned int core_id) {
    if (!valid_core(core_id)) {
        return 0;
    }
    unsigned long flags = 0;
    lock_core(core_id, &flags);
    unsigned long value = cores[core_id].balance_completions;
    unlock_core(core_id, flags);
    return value;
}

unsigned long kernel_scheduler_balance_total(void) {
    unsigned long total = 0;
    for (unsigned int i = 1; i < KERNEL_SCHEDULER_CORE_CAPACITY; i++) {
        total += kernel_scheduler_balance_success_count(i);
    }
    return total;
}

unsigned long kernel_scheduler_balance_completion_total(void) {
    unsigned long total = 0;
    for (unsigned int i = 1; i < KERNEL_SCHEDULER_CORE_CAPACITY; i++) {
        total += kernel_scheduler_balance_completion_count(i);
    }
    return total;
}

unsigned long kernel_scheduler_secondary_queue_min(void) {
    unsigned long min = 0;
    unsigned int seen = 0;
    for (unsigned int i = 1; i < KERNEL_SCHEDULER_CORE_CAPACITY; i++) {
        if (!kernel_smp_core_online(i)) {
            continue;
        }
        unsigned long count = kernel_scheduler_runqueue_count(i);
        if (!seen || count < min) {
            min = count;
        }
        seen = 1;
    }
    return seen ? min : 0;
}

unsigned long kernel_scheduler_secondary_queue_max(void) {
    unsigned long max = 0;
    for (unsigned int i = 1; i < KERNEL_SCHEDULER_CORE_CAPACITY; i++) {
        if (!kernel_smp_core_online(i)) {
            continue;
        }
        unsigned long count = kernel_scheduler_runqueue_count(i);
        if (count > max) {
            max = count;
        }
    }
    return max;
}

unsigned long kernel_scheduler_secondary_queue_imbalance(void) {
    unsigned long min = kernel_scheduler_secondary_queue_min();
    unsigned long max = kernel_scheduler_secondary_queue_max();
    return max >= min ? max - min : 0;
}

unsigned long kernel_scheduler_secondary_worker_total(void) {
    unsigned long total = 0;
    for (unsigned int i = 1; i < KERNEL_SCHEDULER_CORE_CAPACITY; i++) {
        total += kernel_scheduler_worker_drain_count(i);
    }
    return total;
}

unsigned long kernel_scheduler_secondary_worker_min(void) {
    unsigned long min = 0;
    unsigned int seen = 0;
    for (unsigned int i = 1; i < KERNEL_SCHEDULER_CORE_CAPACITY; i++) {
        if (!kernel_smp_core_online(i)) {
            continue;
        }
        unsigned long count = kernel_scheduler_worker_drain_count(i);
        if (!seen || count < min) {
            min = count;
        }
        seen = 1;
    }
    return seen ? min : 0;
}

unsigned long kernel_scheduler_secondary_worker_max(void) {
    unsigned long max = 0;
    for (unsigned int i = 1; i < KERNEL_SCHEDULER_CORE_CAPACITY; i++) {
        if (!kernel_smp_core_online(i)) {
            continue;
        }
        unsigned long count = kernel_scheduler_worker_drain_count(i);
        if (count > max) {
            max = count;
        }
    }
    return max;
}

unsigned long kernel_scheduler_secondary_worker_imbalance(void) {
    unsigned long min = kernel_scheduler_secondary_worker_min();
    unsigned long max = kernel_scheduler_secondary_worker_max();
    return max >= min ? max - min : 0;
}

unsigned long kernel_scheduler_secondary_worker_feed_total(void) {
    unsigned long total = 0;
    for (unsigned int i = 1; i < KERNEL_SCHEDULER_CORE_CAPACITY; i++) {
        total += kernel_scheduler_worker_feed_count(i);
    }
    return total;
}

unsigned long kernel_scheduler_secondary_worker_feed_min(void) {
    unsigned long min = 0;
    unsigned int seen = 0;
    for (unsigned int i = 1; i < KERNEL_SCHEDULER_CORE_CAPACITY; i++) {
        if (!kernel_smp_core_online(i)) {
            continue;
        }
        unsigned long count = kernel_scheduler_worker_feed_count(i);
        if (!seen || count < min) {
            min = count;
        }
        seen = 1;
    }
    return seen ? min : 0;
}

unsigned long kernel_scheduler_secondary_worker_feed_max(void) {
    unsigned long max = 0;
    for (unsigned int i = 1; i < KERNEL_SCHEDULER_CORE_CAPACITY; i++) {
        if (!kernel_smp_core_online(i)) {
            continue;
        }
        unsigned long count = kernel_scheduler_worker_feed_count(i);
        if (count > max) {
            max = count;
        }
    }
    return max;
}

unsigned long kernel_scheduler_secondary_worker_feed_imbalance(void) {
    unsigned long min = kernel_scheduler_secondary_worker_feed_min();
    unsigned long max = kernel_scheduler_secondary_worker_feed_max();
    return max >= min ? max - min : 0;
}

unsigned long kernel_scheduler_worker_feed_drain_gap(void) {
    unsigned long feeds = kernel_scheduler_secondary_worker_feed_total();
    unsigned long drains = kernel_scheduler_secondary_worker_total();
    return feeds >= drains ? feeds - drains : 0;
}

unsigned long kernel_scheduler_secondary_job_total(void) {
    unsigned long total = 0;
    for (unsigned int i = 1; i < KERNEL_SCHEDULER_CORE_CAPACITY; i++) {
        total += kernel_scheduler_secondary_job_execution_count(i);
    }
    return total;
}

unsigned long kernel_scheduler_secondary_job_completion_total(void) {
    unsigned long total = 0;
    for (unsigned int i = 1; i < KERNEL_SCHEDULER_CORE_CAPACITY; i++) {
        total += kernel_scheduler_secondary_job_completion_count(i);
    }
    return total;
}

unsigned long kernel_scheduler_secondary_job_noop_total(void) {
    unsigned long total = 0;
    for (unsigned int i = 1; i < KERNEL_SCHEDULER_CORE_CAPACITY; i++) {
        total += kernel_scheduler_secondary_job_noop_count(i);
    }
    return total;
}

unsigned long kernel_scheduler_secondary_job_checksum_total(void) {
    unsigned long total = 0;
    for (unsigned int i = 1; i < KERNEL_SCHEDULER_CORE_CAPACITY; i++) {
        total += kernel_scheduler_secondary_job_checksum(i);
    }
    return total;
}

unsigned long kernel_scheduler_secondary_job_min(void) {
    unsigned long min = 0;
    unsigned int seen = 0;
    for (unsigned int i = 1; i < KERNEL_SCHEDULER_CORE_CAPACITY; i++) {
        if (!kernel_smp_core_online(i)) {
            continue;
        }
        unsigned long count = kernel_scheduler_secondary_job_execution_count(i);
        if (!seen || count < min) {
            min = count;
        }
        seen = 1;
    }
    return seen ? min : 0;
}

unsigned long kernel_scheduler_secondary_job_max(void) {
    unsigned long max = 0;
    for (unsigned int i = 1; i < KERNEL_SCHEDULER_CORE_CAPACITY; i++) {
        if (!kernel_smp_core_online(i)) {
            continue;
        }
        unsigned long count = kernel_scheduler_secondary_job_execution_count(i);
        if (count > max) {
            max = count;
        }
    }
    return max;
}

unsigned long kernel_scheduler_secondary_job_imbalance(void) {
    unsigned long min = kernel_scheduler_secondary_job_min();
    unsigned long max = kernel_scheduler_secondary_job_max();
    return max >= min ? max - min : 0;
}

unsigned long kernel_scheduler_secondary_job_completion_gap(void) {
    unsigned long executions = kernel_scheduler_secondary_job_total();
    unsigned long completions = kernel_scheduler_secondary_job_completion_total();
    return executions >= completions ? executions - completions : 0;
}

unsigned long kernel_scheduler_secondary_wake_signal_total(void) {
    return kernel_smp_scheduler_signal_count();
}

unsigned int kernel_scheduler_secondary_wake_signal_mask(void) {
    return kernel_smp_scheduler_signal_mask();
}

unsigned long kernel_scheduler_secondary_wake_target_total(void) {
    return kernel_smp_scheduler_signal_target_total();
}

unsigned long kernel_scheduler_secondary_wake_wait_count(unsigned int core_id) {
    return kernel_smp_core_scheduler_wait_count(core_id);
}

unsigned long kernel_scheduler_secondary_wake_ack_count(unsigned int core_id) {
    return kernel_smp_core_scheduler_wake_count(core_id);
}

unsigned long kernel_scheduler_secondary_wake_wait_total(void) {
    unsigned long total = 0;
    for (unsigned int i = 1; i < KERNEL_SCHEDULER_CORE_CAPACITY; i++) {
        total += kernel_scheduler_secondary_wake_wait_count(i);
    }
    return total;
}

unsigned long kernel_scheduler_secondary_wake_ack_total(void) {
    unsigned long total = 0;
    for (unsigned int i = 1; i < KERNEL_SCHEDULER_CORE_CAPACITY; i++) {
        total += kernel_scheduler_secondary_wake_ack_count(i);
    }
    return total;
}

unsigned long kernel_scheduler_secondary_wake_gap(void) {
    unsigned long waits = kernel_scheduler_secondary_wake_wait_total();
    unsigned long wakes = kernel_scheduler_secondary_wake_ack_total();
    return waits >= wakes ? waits - wakes : 0;
}

unsigned long kernel_scheduler_secondary_wake_imbalance(void) {
    unsigned long min = 0;
    unsigned long max = 0;
    unsigned int seen = 0;
    for (unsigned int i = 1; i < KERNEL_SCHEDULER_CORE_CAPACITY; i++) {
        if (!kernel_smp_core_online(i)) {
            continue;
        }
        unsigned long count = kernel_scheduler_secondary_wake_ack_count(i);
        if (!seen || count < min) {
            min = count;
        }
        if (count > max) {
            max = count;
        }
        seen = 1;
    }
    return seen && max >= min ? max - min : 0;
}

unsigned long kernel_scheduler_secondary_handoff_issue_count(unsigned int core_id) {
    if (!valid_core(core_id)) {
        return 0;
    }
    unsigned long flags = 0;
    lock_core(core_id, &flags);
    unsigned long count = cores[core_id].handoff_issues;
    unlock_core(core_id, flags);
    return count;
}

unsigned long kernel_scheduler_secondary_handoff_completion_count(unsigned int core_id) {
    if (!valid_core(core_id)) {
        return 0;
    }
    unsigned long flags = 0;
    lock_core(core_id, &flags);
    unsigned long count = cores[core_id].handoff_completions;
    unlock_core(core_id, flags);
    return count;
}

unsigned long kernel_scheduler_secondary_handoff_issue_total(void) {
    unsigned long total = 0;
    for (unsigned int i = 1; i < KERNEL_SCHEDULER_CORE_CAPACITY; i++) {
        total += kernel_scheduler_secondary_handoff_issue_count(i);
    }
    return total;
}

unsigned long kernel_scheduler_secondary_handoff_completion_total(void) {
    unsigned long total = 0;
    for (unsigned int i = 1; i < KERNEL_SCHEDULER_CORE_CAPACITY; i++) {
        total += kernel_scheduler_secondary_handoff_completion_count(i);
    }
    return total;
}

unsigned long kernel_scheduler_secondary_handoff_gap(void) {
    unsigned long issued = kernel_scheduler_secondary_handoff_issue_total();
    unsigned long completed = kernel_scheduler_secondary_handoff_completion_total();
    return issued >= completed ? issued - completed : 0;
}

unsigned long kernel_scheduler_secondary_handoff_imbalance(void) {
    unsigned long min = 0;
    unsigned long max = 0;
    unsigned int seen = 0;
    for (unsigned int i = 1; i < KERNEL_SCHEDULER_CORE_CAPACITY; i++) {
        if (!kernel_smp_core_online(i)) {
            continue;
        }
        unsigned long count = kernel_scheduler_secondary_handoff_completion_count(i);
        if (!seen || count < min) {
            min = count;
        }
        if (count > max) {
            max = count;
        }
        seen = 1;
    }
    return seen && max >= min ? max - min : 0;
}

unsigned long kernel_scheduler_fairness_min(void) {
    unsigned long min = 0;
    unsigned int seen = 0;
    for (unsigned int i = 0; i < KERNEL_SCHEDULER_CORE_CAPACITY; i++) {
        if (!kernel_smp_core_online(i)) {
            continue;
        }
        unsigned long count = kernel_scheduler_dispatch_count(i);
        if (!seen || count < min) {
            min = count;
        }
        seen = 1;
    }
    return seen ? min : 0;
}

unsigned long kernel_scheduler_fairness_max(void) {
    unsigned long max = 0;
    for (unsigned int i = 0; i < KERNEL_SCHEDULER_CORE_CAPACITY; i++) {
        if (!kernel_smp_core_online(i)) {
            continue;
        }
        unsigned long count = kernel_scheduler_dispatch_count(i);
        if (count > max) {
            max = count;
        }
    }
    return max;
}

unsigned long kernel_scheduler_fairness_imbalance(void) {
    unsigned long min = kernel_scheduler_fairness_min();
    unsigned long max = kernel_scheduler_fairness_max();
    return max >= min ? max - min : 0;
}

unsigned long kernel_scheduler_priority_low_count(unsigned int core_id) {
    if (!valid_core(core_id)) {
        return 0;
    }
    unsigned long flags = 0;
    lock_core(core_id, &flags);
    unsigned long value = cores[core_id].priority_low_count;
    unlock_core(core_id, flags);
    return value;
}

unsigned long kernel_scheduler_priority_high_count(unsigned int core_id) {
    if (!valid_core(core_id)) {
        return 0;
    }
    unsigned long flags = 0;
    lock_core(core_id, &flags);
    unsigned long value = cores[core_id].priority_high_count;
    unlock_core(core_id, flags);
    return value;
}

unsigned long kernel_scheduler_priority_preempt_count(unsigned int core_id) {
    if (!valid_core(core_id)) {
        return 0;
    }
    unsigned long flags = 0;
    lock_core(core_id, &flags);
    unsigned long value = cores[core_id].priority_preempt_count;
    unlock_core(core_id, flags);
    return value;
}

unsigned long kernel_scheduler_priority_yield_count(unsigned int core_id) {
    if (!valid_core(core_id)) {
        return 0;
    }
    unsigned long flags = 0;
    lock_core(core_id, &flags);
    unsigned long value = cores[core_id].priority_yield_count;
    unlock_core(core_id, flags);
    return value;
}

unsigned long kernel_scheduler_priority_completion_count(unsigned int core_id) {
    if (!valid_core(core_id)) {
        return 0;
    }
    unsigned long flags = 0;
    lock_core(core_id, &flags);
    unsigned long value = cores[core_id].priority_completions;
    unlock_core(core_id, flags);
    return value;
}

unsigned long kernel_scheduler_priority_preempt_total(void) {
    unsigned long total = 0;
    for (unsigned int core_id = 1; core_id < KERNEL_SCHEDULER_CORE_CAPACITY; core_id++) {
        total += kernel_scheduler_priority_preempt_count(core_id);
    }
    return total;
}

unsigned long kernel_scheduler_priority_yield_total(void) {
    unsigned long total = 0;
    for (unsigned int core_id = 1; core_id < KERNEL_SCHEDULER_CORE_CAPACITY; core_id++) {
        total += kernel_scheduler_priority_yield_count(core_id);
    }
    return total;
}

unsigned long kernel_scheduler_priority_completion_total(void) {
    unsigned long total = 0;
    for (unsigned int core_id = 1; core_id < KERNEL_SCHEDULER_CORE_CAPACITY; core_id++) {
        total += kernel_scheduler_priority_completion_count(core_id);
    }
    return total;
}

unsigned long kernel_scheduler_priority_lane_imbalance(void) {
    unsigned long min = 0;
    unsigned long max = 0;
    unsigned int seen = 0;
    for (unsigned int core_id = 1; core_id < KERNEL_SCHEDULER_CORE_CAPACITY; core_id++) {
        unsigned long value = kernel_scheduler_priority_completion_count(core_id);
        if (seen == 0) {
            min = value;
            max = value;
            seen = 1;
        } else {
            if (value < min) {
                min = value;
            }
            if (value > max) {
                max = value;
            }
        }
    }
    return max >= min ? max - min : 0;
}

unsigned int kernel_scheduler_last_dispatch_core(void) {
    unsigned long flags = irq_save();
    unsigned int value = last_dispatch_core;
    irq_restore(flags);
    return value;
}

unsigned long kernel_scheduler_tick_count(unsigned int core_id) {
    if (!valid_core(core_id)) {
        return 0;
    }
    unsigned long flags = 0;
    lock_core(core_id, &flags);
    unsigned long count = cores[core_id].ticks;
    unlock_core(core_id, flags);
    return count;
}

unsigned long kernel_scheduler_irq_tick_count(unsigned int core_id) {
    if (!valid_core(core_id)) {
        return 0;
    }
    unsigned long flags = 0;
    lock_core(core_id, &flags);
    unsigned long count = cores[core_id].irq_ticks;
    unlock_core(core_id, flags);
    return count;
}

unsigned long kernel_scheduler_preempt_count(unsigned int core_id) {
    if (!valid_core(core_id)) {
        return 0;
    }
    unsigned long flags = 0;
    lock_core(core_id, &flags);
    unsigned long count = cores[core_id].preemptions;
    unlock_core(core_id, flags);
    return count;
}

unsigned long kernel_scheduler_enqueue_count(unsigned int core_id) {
    if (!valid_core(core_id)) {
        return 0;
    }
    unsigned long flags = 0;
    lock_core(core_id, &flags);
    unsigned long count = cores[core_id].enqueues;
    unlock_core(core_id, flags);
    return count;
}

unsigned long kernel_scheduler_dequeue_count(unsigned int core_id) {
    if (!valid_core(core_id)) {
        return 0;
    }
    unsigned long flags = 0;
    lock_core(core_id, &flags);
    unsigned long count = cores[core_id].dequeues;
    unlock_core(core_id, flags);
    return count;
}

unsigned long kernel_scheduler_interval_ticks(void) {
    unsigned long flags = irq_save();
    unsigned long value = interval_ticks_value;
    irq_restore(flags);
    return value;
}

int kernel_scheduler_timer_worker_feed_proven(void) {
    if (KERNEL_SCHEDULER_VERSION != 43U ||
        !kernel_scheduler_active() ||
        !kernel_scheduler_secondary_workers_enabled() ||
        !kernel_scheduler_timer_worker_feed_enabled() ||
        kernel_smp_online_count() != 4U ||
        kernel_smp_online_mask() != 0xfU) {
        return 0;
    }

    unsigned long feed_min = kernel_scheduler_secondary_worker_feed_min();
    unsigned long feed_max = kernel_scheduler_secondary_worker_feed_max();
    unsigned long drain_min = kernel_scheduler_secondary_worker_min();
    unsigned long drain_max = kernel_scheduler_secondary_worker_max();
    return feed_min > 0 &&
        feed_max >= feed_min &&
        drain_min > 0 &&
        drain_max >= drain_min &&
        kernel_scheduler_worker_feed_count(0) == 0 &&
        kernel_scheduler_worker_drain_count(0) == 0 &&
        kernel_scheduler_secondary_worker_feed_total() >= 3UL &&
        kernel_scheduler_secondary_worker_total() >= 3UL &&
        kernel_scheduler_secondary_worker_feed_imbalance() <= KERNEL_SCHEDULER_CORE_CAPACITY &&
        kernel_scheduler_secondary_worker_imbalance() <= KERNEL_SCHEDULER_CORE_CAPACITY &&
        kernel_scheduler_worker_feed_drain_gap() <= KERNEL_SCHEDULER_CORE_CAPACITY ? 1 : 0;
}

int kernel_scheduler_secondary_worker_proven(void) {
    if (KERNEL_SCHEDULER_VERSION != 43U ||
        !kernel_scheduler_active() ||
        !kernel_scheduler_secondary_workers_enabled() ||
        kernel_smp_online_count() != 4U ||
        kernel_smp_online_mask() != 0xfU) {
        return 0;
    }

    return kernel_scheduler_secondary_worker_total() >= 3UL &&
        kernel_scheduler_secondary_worker_min() > 0UL &&
        kernel_scheduler_secondary_worker_imbalance() <= KERNEL_SCHEDULER_CORE_CAPACITY ? 1 : 0;
}

int kernel_scheduler_secondary_job_proven(void) {
    if (KERNEL_SCHEDULER_VERSION != 43U ||
        !kernel_scheduler_active() ||
        !kernel_scheduler_secondary_workers_enabled() ||
        !kernel_scheduler_timer_worker_feed_enabled() ||
        !kernel_scheduler_secondary_job_execution_enabled() ||
        kernel_smp_online_count() != 4U ||
        kernel_smp_online_mask() != 0xfU) {
        return 0;
    }

    unsigned long min = kernel_scheduler_secondary_job_min();
    unsigned long max = kernel_scheduler_secondary_job_max();
    return min > 0 &&
        max >= min &&
        kernel_scheduler_secondary_job_execution_count(0) == 0 &&
        kernel_scheduler_secondary_job_completion_count(0) == 0 &&
        kernel_scheduler_secondary_job_total() >= 3UL &&
        kernel_scheduler_secondary_job_completion_total() >= 3UL &&
        kernel_scheduler_secondary_job_checksum_total() > 0 &&
        kernel_scheduler_secondary_job_imbalance() <= KERNEL_SCHEDULER_CORE_CAPACITY &&
        kernel_scheduler_secondary_job_completion_gap() == 0 ? 1 : 0;
}

int kernel_scheduler_secondary_wake_proven(void) {
    if (KERNEL_SCHEDULER_VERSION != 43U ||
        !kernel_scheduler_active() ||
        !kernel_scheduler_secondary_workers_enabled() ||
        !kernel_scheduler_timer_worker_feed_enabled() ||
        !kernel_scheduler_secondary_job_execution_enabled() ||
        !kernel_scheduler_secondary_wake_signals_enabled() ||
        kernel_smp_online_count() != 4U ||
        kernel_smp_online_mask() != 0xfU) {
        return 0;
    }

    return kernel_scheduler_secondary_wake_signal_total() > 0 &&
        kernel_scheduler_secondary_wake_signal_mask() == KERNEL_SMP_SECONDARY_MASK &&
        kernel_scheduler_secondary_wake_target_total() >= 3UL &&
        kernel_scheduler_secondary_wake_wait_count(0) == 0 &&
        kernel_scheduler_secondary_wake_ack_count(0) == 0 &&
        kernel_scheduler_secondary_wake_wait_total() >= 3UL &&
        kernel_scheduler_secondary_wake_ack_total() >= 3UL &&
        kernel_scheduler_secondary_wake_gap() <= KERNEL_SCHEDULER_CORE_CAPACITY ? 1 : 0;
}

int kernel_scheduler_secondary_handoff_proven(void) {
    if (KERNEL_SCHEDULER_VERSION != 43U ||
        !kernel_scheduler_active() ||
        !kernel_scheduler_secondary_workers_enabled() ||
        !kernel_scheduler_timer_worker_feed_enabled() ||
        !kernel_scheduler_secondary_job_execution_enabled() ||
        !kernel_scheduler_secondary_wake_signals_enabled() ||
        !kernel_scheduler_secondary_handoffs_enabled() ||
        kernel_smp_online_count() != 4U ||
        kernel_smp_online_mask() != 0xfU) {
        return 0;
    }

    return kernel_scheduler_secondary_handoff_issue_count(0) == 0 &&
        kernel_scheduler_secondary_handoff_completion_count(0) == 0 &&
        kernel_scheduler_secondary_handoff_issue_total() >= 3UL &&
        kernel_scheduler_secondary_handoff_completion_total() >= 3UL &&
        kernel_scheduler_secondary_handoff_gap() <= KERNEL_SCHEDULER_CORE_CAPACITY &&
        kernel_scheduler_secondary_handoff_imbalance() <= KERNEL_SCHEDULER_CORE_CAPACITY ? 1 : 0;
}

int kernel_scheduler_backpressure_proven(void) {
    if (KERNEL_SCHEDULER_VERSION != 43U ||
        kernel_scheduler_core_count() != KERNEL_SCHEDULER_CORE_CAPACITY ||
        kernel_scheduler_runqueue_capacity() != KERNEL_SCHEDULER_RUNQUEUE_CAPACITY) {
        return 0;
    }

    return kernel_scheduler_runqueue_overflow_total() >= KERNEL_SCHEDULER_CORE_CAPACITY &&
        kernel_scheduler_runqueue_high_water_max() >= KERNEL_SCHEDULER_RUNQUEUE_CAPACITY ? 1 : 0;
}

int kernel_scheduler_work_steal_proven(void) {
    if (KERNEL_SCHEDULER_VERSION != 43U ||
        kernel_scheduler_core_count() != KERNEL_SCHEDULER_CORE_CAPACITY ||
        kernel_scheduler_runqueue_capacity() != KERNEL_SCHEDULER_RUNQUEUE_CAPACITY ||
        !kernel_smp_core_online(1) ||
        !kernel_smp_core_online(2) ||
        !kernel_smp_core_online(3)) {
        return 0;
    }

    return kernel_scheduler_steal_total() >= 2U &&
        kernel_scheduler_steal_completion_total() >= 2U ? 1 : 0;
}

int kernel_scheduler_fairness_proven(void) {
    if (KERNEL_SCHEDULER_VERSION != 43U ||
        kernel_scheduler_core_count() != KERNEL_SCHEDULER_CORE_CAPACITY ||
        kernel_scheduler_runqueue_capacity() != KERNEL_SCHEDULER_RUNQUEUE_CAPACITY ||
        !kernel_smp_core_online(1) ||
        !kernel_smp_core_online(2) ||
        !kernel_smp_core_online(3)) {
        return 0;
    }

    return kernel_scheduler_balance_total() >= 2U &&
        kernel_scheduler_balance_completion_total() >= 2U &&
        kernel_scheduler_secondary_queue_imbalance() <= 1UL ? 1 : 0;
}

int kernel_scheduler_priority_proven(void) {
    if (KERNEL_SCHEDULER_VERSION != 43U ||
        kernel_scheduler_core_count() != KERNEL_SCHEDULER_CORE_CAPACITY ||
        kernel_scheduler_runqueue_capacity() != KERNEL_SCHEDULER_RUNQUEUE_CAPACITY ||
        !priority_lanes_enabled ||
        !kernel_smp_core_online(1)) {
        return 0;
    }

    return kernel_scheduler_priority_preempt_total() >= 2U &&
        kernel_scheduler_priority_yield_total() >= 2U &&
        kernel_scheduler_priority_completion_total() >= 4U ? 1 : 0;
}

int kernel_scheduler_smp_scheduler_proven(void) {
    if (KERNEL_SCHEDULER_VERSION != 43U ||
        !kernel_scheduler_active() ||
        !kernel_scheduler_smp_dispatch_enabled() ||
        kernel_scheduler_core_count() != 4U ||
        kernel_smp_online_count() != 4U) {
        return 0;
    }

    unsigned long min = kernel_scheduler_fairness_min();
    unsigned long max = kernel_scheduler_fairness_max();
    return min > 0 &&
        max >= min &&
        kernel_scheduler_fairness_imbalance() <= 1UL &&
        kernel_scheduler_total_route_count() >= 4UL &&
        kernel_scheduler_total_dispatch_count() >= 4UL ? 1 : 0;
}

int kernel_scheduler_scheduler_proven(void) {
    if (KERNEL_SCHEDULER_VERSION != 43U ||
        kernel_scheduler_core_count() != KERNEL_SCHEDULER_CORE_CAPACITY ||
        kernel_scheduler_runqueue_capacity() != KERNEL_SCHEDULER_RUNQUEUE_CAPACITY ||
        kernel_scheduler_active() == 0 ||
        kernel_scheduler_interval_ticks() == 0) {
        return 0;
    }

    return 1;
}

int kernel_scheduler_runqueue_proven(void) {
    if (KERNEL_SCHEDULER_VERSION != 43U ||
        kernel_scheduler_core_count() != KERNEL_SCHEDULER_CORE_CAPACITY ||
        kernel_scheduler_runqueue_capacity() != KERNEL_SCHEDULER_RUNQUEUE_CAPACITY) {
        return 0;
    }

    unsigned int total = 0;
    for (unsigned int core_id = 0; core_id < KERNEL_SCHEDULER_CORE_CAPACITY; core_id++) {
        total += kernel_scheduler_runqueue_count(core_id);
    }

    return kernel_scheduler_steal_total() >= 2U &&
        kernel_scheduler_runqueue_high_water_max() >= KERNEL_SCHEDULER_RUNQUEUE_CAPACITY &&
        total == 0U ? 1 : 0;
}

int kernel_scheduler_selftest(void) {
    if (!initialized) {
        kernel_scheduler_init();
    }
    if (KERNEL_SCHEDULER_VERSION != 43U) {
        return 0;
    }
    if (kernel_scheduler_core_count() != KERNEL_SCHEDULER_CORE_CAPACITY) {
        return 0;
    }
    if (kernel_scheduler_runqueue_capacity() != KERNEL_SCHEDULER_RUNQUEUE_CAPACITY) {
        return 0;
    }
    if (kernel_scheduler_active() == 0 || kernel_scheduler_interval_ticks() == 0) {
        return 0;
    }

    return kernel_scheduler_runqueue_selftest();
}

int kernel_scheduler_runqueue_selftest(void) {
    if (!initialized) {
        kernel_scheduler_init();
    }
    if (KERNEL_SCHEDULER_VERSION != 43U) {
        return 0;
    }
    if (kernel_scheduler_core_count() != 4U) {
        return 0;
    }
    if (kernel_scheduler_runqueue_capacity() != KERNEL_SCHEDULER_RUNQUEUE_CAPACITY) {
        return 0;
    }
    if (kernel_scheduler_steal_total() >= 2U &&
        kernel_scheduler_runqueue_high_water_max() >= KERNEL_SCHEDULER_RUNQUEUE_CAPACITY) {
        return 1;
    }

    unsigned int saved_feed = kernel_scheduler_timer_worker_feed_enabled();
    set_timer_worker_feed_enabled(0);
    wait_for_secondary_queues_empty();

    int ok = 1;
    for (unsigned int core_id = 0; core_id < KERNEL_SCHEDULER_CORE_CAPACITY; core_id++) {
        unsigned int value = 0;
        unsigned int token = 0x33U + core_id;
        unsigned int before = kernel_scheduler_runqueue_count(core_id);
        if (before != 0) {
            ok = 0;
            break;
        }
        if (!kernel_scheduler_enqueue(core_id, token)) {
            ok = 0;
            break;
        }
        if (kernel_scheduler_runqueue_count(core_id) != 1U) {
            ok = 0;
            break;
        }
        if (!kernel_scheduler_dequeue(core_id, &value)) {
            ok = 0;
            break;
        }
        if (value != token || kernel_scheduler_runqueue_count(core_id) != 0U) {
            ok = 0;
            break;
        }
    }

    if (saved_feed) {
        set_timer_worker_feed_enabled(1);
    }
    return ok;
}

int kernel_scheduler_backpressure_selftest(void) {
    if (!initialized) {
        kernel_scheduler_init();
    }
    if (KERNEL_SCHEDULER_VERSION != 43U) {
        return 0;
    }
    if (kernel_scheduler_core_count() != KERNEL_SCHEDULER_CORE_CAPACITY ||
        kernel_scheduler_runqueue_capacity() != KERNEL_SCHEDULER_RUNQUEUE_CAPACITY) {
        return 0;
    }
    if (kernel_scheduler_runqueue_overflow_total() >= KERNEL_SCHEDULER_CORE_CAPACITY &&
        kernel_scheduler_runqueue_high_water_max() >= KERNEL_SCHEDULER_RUNQUEUE_CAPACITY) {
        return 1;
    }

    unsigned int saved_feed = kernel_scheduler_timer_worker_feed_enabled();
    set_timer_worker_feed_enabled(0);
    wait_for_secondary_queues_empty();

    int ok = 1;
    for (unsigned int core_id = 0; core_id < KERNEL_SCHEDULER_CORE_CAPACITY; core_id++) {
        unsigned long overflow_before = kernel_scheduler_runqueue_overflow_count(core_id);
        if (kernel_scheduler_runqueue_count(core_id) != 0) {
            ok = 0;
        }

        for (unsigned int slot = 0; slot < KERNEL_SCHEDULER_RUNQUEUE_CAPACITY; slot++) {
            unsigned int token = KERNEL_SCHEDULER_PRESSURE_TOKEN_BASE |
                ((core_id & 0x0fU) << 4) |
                (slot & 0x0fU);
            if (!kernel_scheduler_enqueue(core_id, token)) {
                ok = 0;
                break;
            }
        }

        if (kernel_scheduler_runqueue_count(core_id) != KERNEL_SCHEDULER_RUNQUEUE_CAPACITY ||
            kernel_scheduler_runqueue_high_water(core_id) < KERNEL_SCHEDULER_RUNQUEUE_CAPACITY) {
            ok = 0;
        }

        unsigned int overflow_token = KERNEL_SCHEDULER_PRESSURE_TOKEN_BASE |
            ((core_id & 0x0fU) << 4) |
            KERNEL_SCHEDULER_RUNQUEUE_CAPACITY;
        if (kernel_scheduler_enqueue(core_id, overflow_token)) {
            ok = 0;
        }
        if (kernel_scheduler_runqueue_overflow_count(core_id) <= overflow_before) {
            ok = 0;
        }

        for (unsigned int slot = 0; slot < KERNEL_SCHEDULER_RUNQUEUE_CAPACITY; slot++) {
            unsigned int expected = KERNEL_SCHEDULER_PRESSURE_TOKEN_BASE |
                ((core_id & 0x0fU) << 4) |
                (slot & 0x0fU);
            unsigned int value = 0;
            if (!kernel_scheduler_dequeue(core_id, &value) || value != expected) {
                ok = 0;
                break;
            }
        }

        for (unsigned int cleanup = 0;
             cleanup < KERNEL_SCHEDULER_RUNQUEUE_CAPACITY &&
             kernel_scheduler_runqueue_count(core_id) != 0;
             cleanup++) {
            unsigned int ignored = 0;
            (void)kernel_scheduler_dequeue(core_id, &ignored);
        }
        if (kernel_scheduler_runqueue_count(core_id) != 0) {
            ok = 0;
        }
    }

    if (saved_feed) {
        set_timer_worker_feed_enabled(1);
    }
    return ok &&
        kernel_scheduler_runqueue_high_water_max() >= KERNEL_SCHEDULER_RUNQUEUE_CAPACITY &&
        kernel_scheduler_runqueue_overflow_total() >= KERNEL_SCHEDULER_CORE_CAPACITY ? 1 : 0;
}

int kernel_scheduler_work_steal_selftest(void) {
    if (!initialized) {
        kernel_scheduler_init();
    }
    if (KERNEL_SCHEDULER_VERSION != 43U) {
        return 0;
    }
    if (kernel_scheduler_core_count() != KERNEL_SCHEDULER_CORE_CAPACITY ||
        kernel_scheduler_runqueue_capacity() != KERNEL_SCHEDULER_RUNQUEUE_CAPACITY) {
        return 0;
    }
    if (!kernel_smp_core_online(1) || !kernel_smp_core_online(2) || !kernel_smp_core_online(3)) {
        return 0;
    }
    if (kernel_scheduler_steal_total() >= 2U &&
        kernel_scheduler_steal_completion_total() >= 2U) {
        return 1;
    }

    unsigned int saved_feed = kernel_scheduler_timer_worker_feed_enabled();
    set_timer_worker_feed_enabled(0);
    wait_for_secondary_queues_empty();

    unsigned long steal_before = kernel_scheduler_steal_total();
    unsigned long completion_before = kernel_scheduler_steal_completion_total();
    unsigned long source1_before = kernel_scheduler_steal_source_count(1);
    unsigned long dest2_before = kernel_scheduler_steal_success_count(2);
    unsigned long dest3_before = kernel_scheduler_steal_success_count(3);

    int ok = 1;
    for (unsigned int slot = 0; slot < 4U; slot++) {
        if (!kernel_scheduler_enqueue(1, scheduler_steal_token_for_source(1, slot + 1U))) {
            ok = 0;
        }
    }

    if (ok) {
        kernel_smp_signal_scheduler_work((1U << 2) | (1U << 3));
        for (unsigned int spin = 0; spin < 200000U; spin++) {
            if (kernel_scheduler_steal_success_count(2) > dest2_before &&
                kernel_scheduler_steal_success_count(3) > dest3_before &&
                kernel_scheduler_runqueue_count(1) == 0) {
                break;
            }
            if ((spin & 0x3ffU) == 0) {
                kernel_smp_signal_scheduler_work((1U << 2) | (1U << 3));
            }
            __asm__ volatile("nop" ::: "memory");
        }
    }

    if (kernel_scheduler_steal_success_count(2) <= dest2_before ||
        kernel_scheduler_steal_success_count(3) <= dest3_before ||
        kernel_scheduler_steal_source_count(1) < source1_before + 2U ||
        kernel_scheduler_steal_total() < steal_before + 2U ||
        kernel_scheduler_steal_completion_total() < completion_before + 2U) {
        ok = 0;
    }

    for (unsigned int core_id = 1; core_id < KERNEL_SCHEDULER_CORE_CAPACITY; core_id++) {
        for (unsigned int cleanup = 0;
             cleanup < KERNEL_SCHEDULER_RUNQUEUE_CAPACITY &&
             kernel_scheduler_runqueue_count(core_id) != 0;
             cleanup++) {
            unsigned int ignored = 0;
            (void)kernel_scheduler_dequeue(core_id, &ignored);
        }
        if (kernel_scheduler_runqueue_count(core_id) != 0) {
            ok = 0;
        }
    }

    if (saved_feed) {
        set_timer_worker_feed_enabled(1);
    }
    kernel_smp_signal_scheduler_work(KERNEL_SMP_SECONDARY_MASK);
    wait_for_secondary_queues_empty();
    return ok &&
        kernel_scheduler_steal_total() >= 2U &&
        kernel_scheduler_steal_completion_total() >= 2U ? 1 : 0;
}

int kernel_scheduler_fairness_selftest(void) {
    if (!initialized) {
        kernel_scheduler_init();
    }
    if (KERNEL_SCHEDULER_VERSION != 43U) {
        return 0;
    }
    if (kernel_scheduler_core_count() != KERNEL_SCHEDULER_CORE_CAPACITY ||
        kernel_scheduler_runqueue_capacity() != KERNEL_SCHEDULER_RUNQUEUE_CAPACITY) {
        return 0;
    }
    if (!kernel_smp_core_online(1) || !kernel_smp_core_online(2) || !kernel_smp_core_online(3)) {
        return 0;
    }
    if (kernel_scheduler_balance_total() >= 2U &&
        kernel_scheduler_balance_completion_total() >= 2U) {
        return 1;
    }

    unsigned int saved_feed = kernel_scheduler_timer_worker_feed_enabled();
    set_timer_worker_feed_enabled(0);
    wait_for_secondary_queues_empty();

    unsigned long balance_before = kernel_scheduler_balance_total();
    unsigned long completion_before = kernel_scheduler_balance_completion_total();
    unsigned long source1_before = kernel_scheduler_balance_source_count(1);
    unsigned long dest2_before = kernel_scheduler_balance_success_count(2);
    unsigned long dest3_before = kernel_scheduler_balance_success_count(3);

    int ok = 1;
    for (unsigned int slot = 0; slot < 4U; slot++) {
        if (!kernel_scheduler_enqueue(1, scheduler_balance_token_for_source(1, slot + 1U))) {
            ok = 0;
        }
    }

    if (ok) {
        kernel_smp_signal_scheduler_work((1U << 2) | (1U << 3));
        for (unsigned int spin = 0; spin < 200000U; spin++) {
            if (kernel_scheduler_balance_success_count(2) > dest2_before &&
                kernel_scheduler_balance_success_count(3) > dest3_before &&
                kernel_scheduler_runqueue_count(1) == 0) {
                break;
            }
            if ((spin & 0x3ffU) == 0) {
                kernel_smp_signal_scheduler_work((1U << 2) | (1U << 3));
            }
            __asm__ volatile("nop" ::: "memory");
        }
    }

    if (kernel_scheduler_balance_success_count(2) <= dest2_before ||
        kernel_scheduler_balance_success_count(3) <= dest3_before ||
        kernel_scheduler_balance_source_count(1) < source1_before + 2U ||
        kernel_scheduler_balance_total() < balance_before + 2U ||
        kernel_scheduler_balance_completion_total() < completion_before + 2U) {
        ok = 0;
    }

    for (unsigned int core_id = 1; core_id < KERNEL_SCHEDULER_CORE_CAPACITY; core_id++) {
        for (unsigned int cleanup = 0;
             cleanup < KERNEL_SCHEDULER_RUNQUEUE_CAPACITY &&
             kernel_scheduler_runqueue_count(core_id) != 0;
             cleanup++) {
            unsigned int ignored = 0;
            (void)kernel_scheduler_dequeue(core_id, &ignored);
        }
        if (kernel_scheduler_runqueue_count(core_id) != 0) {
            ok = 0;
        }
    }

    if (saved_feed) {
        set_timer_worker_feed_enabled(1);
    }
    kernel_smp_signal_scheduler_work(KERNEL_SMP_SECONDARY_MASK);
    wait_for_secondary_queues_empty();
    return ok &&
        kernel_scheduler_balance_total() >= 2U &&
        kernel_scheduler_balance_completion_total() >= 2U &&
        kernel_scheduler_secondary_queue_imbalance() <= 1UL ? 1 : 0;
}

int kernel_scheduler_priority_selftest(void) {
    if (!initialized) {
        kernel_scheduler_init();
    }
    if (KERNEL_SCHEDULER_VERSION != 43U) {
        return 0;
    }
    if (kernel_scheduler_core_count() != KERNEL_SCHEDULER_CORE_CAPACITY ||
        kernel_scheduler_runqueue_capacity() != KERNEL_SCHEDULER_RUNQUEUE_CAPACITY) {
        return 0;
    }
    if (!kernel_smp_core_online(1) || !priority_lanes_enabled) {
        return 0;
    }
    if (kernel_scheduler_priority_preempt_total() >= 2U &&
        kernel_scheduler_priority_yield_total() >= 2U &&
        kernel_scheduler_priority_completion_total() >= 4U) {
        return 1;
    }

    unsigned int saved_feed = kernel_scheduler_timer_worker_feed_enabled();
    unsigned int saved_dispatch = kernel_scheduler_smp_dispatch_enabled();
    set_timer_worker_feed_enabled(0);
    set_smp_dispatch_enabled(0);
    wait_for_secondary_queues_empty();

    unsigned long preempt_before = kernel_scheduler_priority_preempt_count(1);
    unsigned long yield_before = kernel_scheduler_priority_yield_count(1);
    unsigned long completion_before = kernel_scheduler_priority_completion_total();

    int ok = 1;
    for (unsigned int slot = 0; slot < 2U; slot++) {
        if (!kernel_scheduler_enqueue(
                1,
                scheduler_priority_token_for_core(1, KERNEL_SCHEDULER_PRIORITY_LANE_LOW, slot + 1U))) {
            ok = 0;
        }
    }
    for (unsigned int slot = 0; slot < 2U; slot++) {
        if (!kernel_scheduler_enqueue(
                1,
                scheduler_priority_token_for_core(1, KERNEL_SCHEDULER_PRIORITY_LANE_HIGH, slot + 3U))) {
            ok = 0;
        }
    }

    if (ok) {
        kernel_smp_signal_scheduler_work(KERNEL_SMP_SECONDARY_MASK);
        for (unsigned int spin = 0; spin < 500000U; spin++) {
            (void)kernel_scheduler_try_preempt_priority_work(1);
            if (kernel_scheduler_priority_preempt_count(1) >= preempt_before + 2U &&
                kernel_scheduler_priority_yield_count(1) >= yield_before + 2U &&
                kernel_scheduler_priority_completion_total() >= completion_before + 4U &&
                kernel_scheduler_runqueue_count(1) == 0) {
                break;
            }
            if ((spin & 0x3ffU) == 0U) {
                kernel_smp_signal_scheduler_work(KERNEL_SMP_SECONDARY_MASK);
            }
            __asm__ volatile("nop" ::: "memory");
        }
    }

    if (kernel_scheduler_priority_preempt_count(1) < preempt_before + 2U ||
        kernel_scheduler_priority_yield_count(1) < yield_before + 2U ||
        kernel_scheduler_priority_completion_total() < completion_before + 4U) {
        ok = 0;
    }

    for (unsigned int core_id = 1; core_id < KERNEL_SCHEDULER_CORE_CAPACITY; core_id++) {
        for (unsigned int cleanup = 0;
             cleanup < KERNEL_SCHEDULER_RUNQUEUE_CAPACITY &&
             kernel_scheduler_runqueue_count(core_id) != 0;
             cleanup++) {
            unsigned int ignored = 0;
            (void)kernel_scheduler_dequeue(core_id, &ignored);
        }
        if (kernel_scheduler_runqueue_count(core_id) != 0) {
            ok = 0;
        }
    }

    if (saved_dispatch) {
        set_smp_dispatch_enabled(1);
    }
    if (saved_feed) {
        set_timer_worker_feed_enabled(1);
    }
    kernel_smp_signal_scheduler_work(KERNEL_SMP_SECONDARY_MASK);
    wait_for_secondary_queues_empty();
    return ok &&
        kernel_scheduler_priority_preempt_total() >= 2U &&
        kernel_scheduler_priority_yield_total() >= 2U &&
        kernel_scheduler_priority_completion_total() >= 4U ? 1 : 0;
}

int kernel_scheduler_smp_selftest(void) {
    if (!initialized) {
        kernel_scheduler_init();
    }
    if (KERNEL_SCHEDULER_VERSION != 43U) {
        return 0;
    }
    if (!kernel_scheduler_active() || !kernel_scheduler_smp_dispatch_enabled()) {
        return 0;
    }
    if (kernel_scheduler_core_count() != 4U || kernel_smp_online_count() != 4U) {
        return 0;
    }
    if (!kernel_scheduler_runqueue_selftest()) {
        return 0;
    }

    if (kernel_scheduler_total_dispatch_count() == 0) {
        route_dispatch_for_online_cores(0x34U);
    }

    unsigned long min = kernel_scheduler_fairness_min();
    unsigned long max = kernel_scheduler_fairness_max();
    return min > 0 &&
        max >= min &&
        kernel_scheduler_fairness_imbalance() <= 1UL &&
        kernel_scheduler_total_route_count() >= 4UL &&
        kernel_scheduler_total_dispatch_count() >= 4UL ? 1 : 0;
}

int kernel_scheduler_secondary_worker_selftest(void) {
    if (!initialized) {
        kernel_scheduler_init();
    }
    if (KERNEL_SCHEDULER_VERSION != 43U) {
        return 0;
    }
    if (!kernel_scheduler_active() || !kernel_scheduler_secondary_workers_enabled()) {
        return 0;
    }
    if (kernel_smp_online_count() != 4U || kernel_smp_online_mask() != 0xfU) {
        return 0;
    }
    if (kernel_scheduler_secondary_worker_total() >= 3UL &&
        kernel_scheduler_secondary_worker_min() > 0UL &&
        kernel_scheduler_secondary_worker_imbalance() <= KERNEL_SCHEDULER_CORE_CAPACITY) {
        return 1;
    }

    unsigned int saved_feed = kernel_scheduler_timer_worker_feed_enabled();
    set_timer_worker_feed_enabled(0);
    wait_for_secondary_queues_empty();

    for (unsigned int core_id = 1; core_id < KERNEL_SCHEDULER_CORE_CAPACITY; core_id++) {
        if (kernel_scheduler_runqueue_count(core_id) == 0) {
            (void)enqueue_worker_probe_for_core(core_id);
        }
    }

    unsigned long drain1_before = kernel_scheduler_worker_drain_count(1);
    unsigned long drain2_before = kernel_scheduler_worker_drain_count(2);
    unsigned long drain3_before = kernel_scheduler_worker_drain_count(3);
    for (unsigned int spin = 0; spin < 200000U; spin++) {
        if (kernel_scheduler_worker_drain_count(1) > drain1_before &&
            kernel_scheduler_worker_drain_count(2) > drain2_before &&
            kernel_scheduler_worker_drain_count(3) > drain3_before) {
            break;
        }
        if ((spin & 0x3ffU) == 0U) {
            kernel_smp_signal_scheduler_work(KERNEL_SMP_SECONDARY_MASK);
        }
        __asm__ volatile("nop" ::: "memory");
    }

    wait_for_secondary_queues_empty();
    unsigned long min = kernel_scheduler_secondary_worker_min();
    unsigned long max = kernel_scheduler_secondary_worker_max();
    int ok = min > 0 &&
        max >= min &&
        kernel_scheduler_secondary_worker_imbalance() <= KERNEL_SCHEDULER_CORE_CAPACITY &&
        kernel_scheduler_secondary_worker_total() >= 3UL &&
        kernel_scheduler_runqueue_count(1) == 0 &&
        kernel_scheduler_runqueue_count(2) == 0 &&
        kernel_scheduler_runqueue_count(3) == 0 ? 1 : 0;

    if (saved_feed) {
        set_timer_worker_feed_enabled(1);
    }
    return ok;
}

int kernel_scheduler_timer_worker_feed_selftest(void) {
    if (!initialized) {
        kernel_scheduler_init();
    }
    if (KERNEL_SCHEDULER_VERSION != 43U) {
        return 0;
    }
    if (!kernel_scheduler_active() ||
        !kernel_scheduler_secondary_workers_enabled() ||
        !kernel_scheduler_timer_worker_feed_enabled()) {
        return 0;
    }
    if (kernel_smp_online_count() != 4U || kernel_smp_online_mask() != 0xfU) {
        return 0;
    }

    for (unsigned int core_id = 1; core_id < KERNEL_SCHEDULER_CORE_CAPACITY; core_id++) {
        if (kernel_scheduler_worker_feed_count(core_id) == 0) {
            (void)route_worker_feed_for_core(core_id);
        }
    }

    for (unsigned int spin = 0; spin < 200000U; spin++) {
        if (kernel_scheduler_worker_feed_count(1) > 0 &&
            kernel_scheduler_worker_feed_count(2) > 0 &&
            kernel_scheduler_worker_feed_count(3) > 0 &&
            kernel_scheduler_worker_drain_count(1) > 0 &&
            kernel_scheduler_worker_drain_count(2) > 0 &&
            kernel_scheduler_worker_drain_count(3) > 0) {
            break;
        }
        if ((spin & 0xffU) == 0U) {
            route_worker_feed_for_online_secondary_cores();
        }
        __asm__ volatile("nop" ::: "memory");
    }

    unsigned long feed_min = kernel_scheduler_secondary_worker_feed_min();
    unsigned long feed_max = kernel_scheduler_secondary_worker_feed_max();
    unsigned long drain_min = kernel_scheduler_secondary_worker_min();
    unsigned long drain_max = kernel_scheduler_secondary_worker_max();
    return feed_min > 0 &&
        feed_max >= feed_min &&
        drain_min > 0 &&
        drain_max >= drain_min &&
        kernel_scheduler_worker_feed_count(0) == 0 &&
        kernel_scheduler_worker_drain_count(0) == 0 &&
        kernel_scheduler_secondary_worker_feed_total() >= 3UL &&
        kernel_scheduler_secondary_worker_total() >= 3UL &&
        kernel_scheduler_secondary_worker_feed_imbalance() <= KERNEL_SCHEDULER_CORE_CAPACITY &&
        kernel_scheduler_secondary_worker_imbalance() <= KERNEL_SCHEDULER_CORE_CAPACITY &&
        kernel_scheduler_worker_feed_drain_gap() <= KERNEL_SCHEDULER_CORE_CAPACITY ? 1 : 0;
}

int kernel_scheduler_secondary_job_selftest(void) {
    if (!initialized) {
        kernel_scheduler_init();
    }
    if (KERNEL_SCHEDULER_VERSION != 43U) {
        return 0;
    }
    if (!kernel_scheduler_active() ||
        !kernel_scheduler_secondary_workers_enabled() ||
        !kernel_scheduler_timer_worker_feed_enabled() ||
        !kernel_scheduler_secondary_job_execution_enabled()) {
        return 0;
    }
    if (kernel_smp_online_count() != 4U || kernel_smp_online_mask() != 0xfU) {
        return 0;
    }
    if (kernel_scheduler_secondary_job_total() >= 3UL &&
        kernel_scheduler_secondary_job_completion_total() >= 3UL &&
        kernel_scheduler_secondary_job_completion_gap() == 0) {
        return 1;
    }

    for (unsigned int core_id = 1; core_id < KERNEL_SCHEDULER_CORE_CAPACITY; core_id++) {
        if (kernel_scheduler_secondary_job_execution_count(core_id) == 0) {
            (void)route_worker_feed_for_core(core_id);
        }
    }

    for (unsigned int spin = 0; spin < 200000U; spin++) {
        if (kernel_scheduler_secondary_job_execution_count(1) > 0 &&
            kernel_scheduler_secondary_job_execution_count(2) > 0 &&
            kernel_scheduler_secondary_job_execution_count(3) > 0 &&
            kernel_scheduler_secondary_job_completion_count(1) > 0 &&
            kernel_scheduler_secondary_job_completion_count(2) > 0 &&
            kernel_scheduler_secondary_job_completion_count(3) > 0) {
            break;
        }
        if ((spin & 0xffU) == 0U) {
            route_worker_feed_for_online_secondary_cores();
        }
        __asm__ volatile("nop" ::: "memory");
    }

    unsigned long min = kernel_scheduler_secondary_job_min();
    unsigned long max = kernel_scheduler_secondary_job_max();
    return min > 0 &&
        max >= min &&
        kernel_scheduler_secondary_job_execution_count(0) == 0 &&
        kernel_scheduler_secondary_job_completion_count(0) == 0 &&
        kernel_scheduler_secondary_job_total() >= 3UL &&
        kernel_scheduler_secondary_job_completion_total() >= 3UL &&
        kernel_scheduler_secondary_job_checksum_total() > 0 &&
        kernel_scheduler_secondary_job_imbalance() <= KERNEL_SCHEDULER_CORE_CAPACITY &&
        kernel_scheduler_secondary_job_completion_gap() == 0 ? 1 : 0;
}

int kernel_scheduler_secondary_wake_selftest(void) {
    if (!initialized) {
        kernel_scheduler_init();
    }
    if (KERNEL_SCHEDULER_VERSION != 43U) {
        return 0;
    }
    if (!kernel_scheduler_active() ||
        !kernel_scheduler_secondary_workers_enabled() ||
        !kernel_scheduler_timer_worker_feed_enabled() ||
        !kernel_scheduler_secondary_job_execution_enabled() ||
        !kernel_scheduler_secondary_wake_signals_enabled()) {
        return 0;
    }
    if (kernel_smp_online_count() != 4U || kernel_smp_online_mask() != 0xfU) {
        return 0;
    }
    if (kernel_scheduler_secondary_wake_ack_total() >= 3UL &&
        kernel_scheduler_secondary_wake_wait_total() >= 3UL &&
        kernel_scheduler_secondary_wake_signal_total() > 0) {
        return 1;
    }

    for (unsigned int core_id = 1; core_id < KERNEL_SCHEDULER_CORE_CAPACITY; core_id++) {
        if (kernel_scheduler_secondary_wake_ack_count(core_id) == 0 ||
            kernel_scheduler_secondary_job_execution_count(core_id) == 0) {
            (void)route_worker_feed_for_core(core_id);
        }
    }

    for (unsigned int spin = 0; spin < 200000U; spin++) {
        if (kernel_scheduler_secondary_wake_signal_total() > 0 &&
            kernel_scheduler_secondary_wake_wait_count(1) > 0 &&
            kernel_scheduler_secondary_wake_wait_count(2) > 0 &&
            kernel_scheduler_secondary_wake_wait_count(3) > 0 &&
            kernel_scheduler_secondary_wake_ack_count(1) > 0 &&
            kernel_scheduler_secondary_wake_ack_count(2) > 0 &&
            kernel_scheduler_secondary_wake_ack_count(3) > 0 &&
            kernel_scheduler_secondary_job_execution_count(1) > 0 &&
            kernel_scheduler_secondary_job_execution_count(2) > 0 &&
            kernel_scheduler_secondary_job_execution_count(3) > 0) {
            break;
        }
        if ((spin & 0xffU) == 0U) {
            route_worker_feed_for_online_secondary_cores();
        }
        __asm__ volatile("nop" ::: "memory");
    }

    // On the Pi 4, WFE can return for architectural events beyond our SEV
    // pulses, so wake imbalance is telemetry, not a gate.
    return kernel_smp_scheduler_wake_selftest() != 0 &&
        kernel_scheduler_secondary_job_selftest() != 0 &&
        kernel_scheduler_secondary_wake_signal_total() > 0 &&
        kernel_scheduler_secondary_wake_signal_mask() == KERNEL_SMP_SECONDARY_MASK &&
        kernel_scheduler_secondary_wake_target_total() >= 3UL &&
        kernel_scheduler_secondary_wake_wait_count(0) == 0 &&
        kernel_scheduler_secondary_wake_ack_count(0) == 0 &&
        kernel_scheduler_secondary_wake_wait_total() >= 3UL &&
        kernel_scheduler_secondary_wake_ack_total() >= 3UL &&
        kernel_scheduler_secondary_wake_gap() <= KERNEL_SCHEDULER_CORE_CAPACITY ? 1 : 0;
}

int kernel_scheduler_secondary_handoff_selftest(void) {
    if (!initialized) {
        kernel_scheduler_init();
    }
    if (KERNEL_SCHEDULER_VERSION != 43U) {
        return 0;
    }
    if (!kernel_scheduler_active() ||
        !kernel_scheduler_secondary_workers_enabled() ||
        !kernel_scheduler_timer_worker_feed_enabled() ||
        !kernel_scheduler_secondary_job_execution_enabled() ||
        !kernel_scheduler_secondary_wake_signals_enabled() ||
        !kernel_scheduler_secondary_handoffs_enabled()) {
        return 0;
    }
    if (kernel_smp_online_count() != 4U || kernel_smp_online_mask() != 0xfU) {
        return 0;
    }
    if (kernel_scheduler_secondary_handoff_issue_total() >= 3UL &&
        kernel_scheduler_secondary_handoff_completion_total() >= 3UL &&
        kernel_scheduler_secondary_handoff_gap() <= KERNEL_SCHEDULER_CORE_CAPACITY) {
        return 1;
    }

    for (unsigned int core_id = 1; core_id < KERNEL_SCHEDULER_CORE_CAPACITY; core_id++) {
        if (kernel_scheduler_secondary_handoff_issue_count(core_id) == 0 ||
            kernel_scheduler_secondary_handoff_completion_count(core_id) == 0) {
            (void)route_worker_feed_for_core(core_id);
        }
    }

    for (unsigned int spin = 0; spin < 200000U; spin++) {
        if (kernel_scheduler_secondary_handoff_issue_count(1) > 0 &&
            kernel_scheduler_secondary_handoff_issue_count(2) > 0 &&
            kernel_scheduler_secondary_handoff_issue_count(3) > 0 &&
            kernel_scheduler_secondary_handoff_completion_count(1) > 0 &&
            kernel_scheduler_secondary_handoff_completion_count(2) > 0 &&
            kernel_scheduler_secondary_handoff_completion_count(3) > 0) {
            break;
        }
        if ((spin & 0xffU) == 0U) {
            route_worker_feed_for_online_secondary_cores();
        }
        __asm__ volatile("nop" ::: "memory");
    }

    return kernel_scheduler_secondary_wake_selftest() != 0 &&
        kernel_scheduler_secondary_job_selftest() != 0 &&
        kernel_scheduler_secondary_handoff_issue_count(0) == 0 &&
        kernel_scheduler_secondary_handoff_completion_count(0) == 0 &&
        kernel_scheduler_secondary_handoff_issue_total() >= 3UL &&
        kernel_scheduler_secondary_handoff_completion_total() >= 3UL &&
        kernel_scheduler_secondary_handoff_gap() <= KERNEL_SCHEDULER_CORE_CAPACITY &&
        kernel_scheduler_secondary_handoff_imbalance() <= KERNEL_SCHEDULER_CORE_CAPACITY ? 1 : 0;
}
