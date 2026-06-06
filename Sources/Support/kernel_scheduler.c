#include "Support.h"

//===----------------------------------------------------------------------===//
// Runtime V36 timer-fed secondary scheduler workers.
// Runtime V35 secondary-owned scheduler workers.
// Runtime V34 timer-driven SMP scheduler dispatch.
//
// This is intentionally still a substrate, not cross-core Swift task dispatch.
// It owns a periodic CNTP timer client, records IRQ preemption opportunities on
// core 0, routes bounded dispatch tokens through each online A72 core queue, and
// lets C-only secondary workers drain V35/V36 worker tokens from their own queues.
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
        return 0;
    }
    core->queue[core->tail] = token;
    core->tail = (core->tail + 1U) % KERNEL_SCHEDULER_RUNQUEUE_CAPACITY;
    core->count++;
    core->enqueues++;
    return 1;
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

static unsigned int worker_token_for_core(unsigned int core_id) {
    return KERNEL_SCHEDULER_WORKER_TOKEN_BASE | (core_id & 0x0fU);
}

static int is_worker_token(unsigned int token) {
    return (token & 0xfff0U) == KERNEL_SCHEDULER_WORKER_TOKEN_BASE;
}

static int enqueue_worker_probe_for_core(unsigned int core_id) {
    if (!valid_core(core_id) || core_id == 0 || !kernel_smp_core_online(core_id)) {
        return 0;
    }
    if (kernel_scheduler_runqueue_count(core_id) != 0) {
        return 0;
    }
    return kernel_scheduler_enqueue(core_id, worker_token_for_core(core_id));
}

static void set_timer_worker_feed_enabled(unsigned int value) {
    unsigned long flags = irq_save();
    timer_worker_feed_enabled = value ? 1U : 0U;
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

    if (kernel_scheduler_enqueue(core_id, worker_token_for_core(core_id))) {
        lock_core(core_id, &flags);
        cores[core_id].worker_feeds++;
        unlock_core(core_id, flags);
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

int kernel_scheduler_enqueue(unsigned int core_id, unsigned int token) {
    if (!valid_core(core_id) || token == 0) {
        return 0;
    }
    unsigned long flags = 0;
    lock_core(core_id, &flags);
    int ok = queue_push_unsafe(&cores[core_id], token);
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

void kernel_scheduler_secondary_worker_tick(unsigned int core_id) {
    if (!secondary_workers_enabled || !valid_core(core_id) || core_id == 0) {
        return;
    }

    unsigned long flags = 0;
    lock_core(core_id, &flags);
    if (cores[core_id].count == 0) {
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

int kernel_scheduler_selftest(void) {
    if (!initialized) {
        kernel_scheduler_init();
    }
    if (KERNEL_SCHEDULER_VERSION != 36U) {
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
    if (KERNEL_SCHEDULER_VERSION != 36U) {
        return 0;
    }
    if (kernel_scheduler_core_count() != 4U) {
        return 0;
    }
    if (kernel_scheduler_runqueue_capacity() != KERNEL_SCHEDULER_RUNQUEUE_CAPACITY) {
        return 0;
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

int kernel_scheduler_smp_selftest(void) {
    if (!initialized) {
        kernel_scheduler_init();
    }
    if (KERNEL_SCHEDULER_VERSION != 36U) {
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
    if (KERNEL_SCHEDULER_VERSION != 36U) {
        return 0;
    }
    if (!kernel_scheduler_active() || !kernel_scheduler_secondary_workers_enabled()) {
        return 0;
    }
    if (kernel_smp_online_count() != 4U || kernel_smp_online_mask() != 0xfU) {
        return 0;
    }

    unsigned int saved_feed = kernel_scheduler_timer_worker_feed_enabled();
    set_timer_worker_feed_enabled(0);
    wait_for_secondary_queues_empty();

    for (unsigned int core_id = 1; core_id < KERNEL_SCHEDULER_CORE_CAPACITY; core_id++) {
        if (kernel_scheduler_worker_drain_count(core_id) == 0) {
            (void)enqueue_worker_probe_for_core(core_id);
        }
    }

    for (unsigned int spin = 0; spin < 200000U; spin++) {
        if (kernel_scheduler_worker_drain_count(1) > 0 &&
            kernel_scheduler_worker_drain_count(2) > 0 &&
            kernel_scheduler_worker_drain_count(3) > 0) {
            break;
        }
        __asm__ volatile("nop" ::: "memory");
    }

    wait_for_secondary_queues_empty();
    unsigned long min = kernel_scheduler_secondary_worker_min();
    unsigned long max = kernel_scheduler_secondary_worker_max();
    int ok = min > 0 &&
        max >= min &&
        kernel_scheduler_secondary_worker_imbalance() <= 1UL &&
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
    if (KERNEL_SCHEDULER_VERSION != 36U) {
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
