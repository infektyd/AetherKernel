#include "Support.h"

//===----------------------------------------------------------------------===//
// Runtime V33 per-core scheduler run queues.
//
// This is intentionally still a substrate, not cross-core Swift task dispatch.
// It owns a periodic CNTP timer client, records IRQ preemption opportunities on
// core 0, and exposes bounded spinlock-protected queues for all A72 cores.
//===----------------------------------------------------------------------===//

typedef struct scheduler_core {
    kernel_spinlock_t lock;
    unsigned long ticks;
    unsigned long irq_ticks;
    unsigned long preemptions;
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
    kernel_spinlock_unlock(&cores[0].lock);

    kernel_timer_set_deadline(KERNEL_TIMER_CLIENT_SCHEDULER, now + interval);
}

unsigned int kernel_scheduler_active(void) {
    unsigned long flags = irq_save();
    unsigned int value = active;
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
    if (KERNEL_SCHEDULER_VERSION != 33U) {
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
    if (KERNEL_SCHEDULER_VERSION != 33U) {
        return 0;
    }
    if (kernel_scheduler_core_count() != 4U) {
        return 0;
    }
    if (kernel_scheduler_runqueue_capacity() != KERNEL_SCHEDULER_RUNQUEUE_CAPACITY) {
        return 0;
    }

    for (unsigned int core_id = 0; core_id < KERNEL_SCHEDULER_CORE_CAPACITY; core_id++) {
        unsigned int value = 0;
        unsigned int token = 0x33U + core_id;
        unsigned int before = kernel_scheduler_runqueue_count(core_id);
        if (before != 0) {
            return 0;
        }
        if (!kernel_scheduler_enqueue(core_id, token)) {
            return 0;
        }
        if (kernel_scheduler_runqueue_count(core_id) != 1U) {
            return 0;
        }
        if (!kernel_scheduler_dequeue(core_id, &value)) {
            return 0;
        }
        if (value != token || kernel_scheduler_runqueue_count(core_id) != 0U) {
            return 0;
        }
    }

    return 1;
}
