#include "Support.h"

//===----------------------------------------------------------------------===//
// Runtime V31 preemptive scheduler substrate.
//
// This is intentionally still a substrate, not cross-core Swift task dispatch.
// It owns a periodic CNTP timer client, records IRQ preemption opportunities,
// and exposes a bounded core-0 run queue shape for later SMP slices.
//===----------------------------------------------------------------------===//

typedef struct scheduler_core {
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

    unsigned long flags = irq_save();
    cores[0].ticks++;
    cores[0].irq_ticks++;
    cores[0].preemptions++;
    irq_restore(flags);

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
    unsigned long flags = irq_save();
    unsigned int count = cores[core_id].count;
    irq_restore(flags);
    return count;
}

unsigned long kernel_scheduler_tick_count(unsigned int core_id) {
    if (!valid_core(core_id)) {
        return 0;
    }
    unsigned long flags = irq_save();
    unsigned long count = cores[core_id].ticks;
    irq_restore(flags);
    return count;
}

unsigned long kernel_scheduler_irq_tick_count(unsigned int core_id) {
    if (!valid_core(core_id)) {
        return 0;
    }
    unsigned long flags = irq_save();
    unsigned long count = cores[core_id].irq_ticks;
    irq_restore(flags);
    return count;
}

unsigned long kernel_scheduler_preempt_count(unsigned int core_id) {
    if (!valid_core(core_id)) {
        return 0;
    }
    unsigned long flags = irq_save();
    unsigned long count = cores[core_id].preemptions;
    irq_restore(flags);
    return count;
}

unsigned long kernel_scheduler_enqueue_count(unsigned int core_id) {
    if (!valid_core(core_id)) {
        return 0;
    }
    unsigned long flags = irq_save();
    unsigned long count = cores[core_id].enqueues;
    irq_restore(flags);
    return count;
}

unsigned long kernel_scheduler_dequeue_count(unsigned int core_id) {
    if (!valid_core(core_id)) {
        return 0;
    }
    unsigned long flags = irq_save();
    unsigned long count = cores[core_id].dequeues;
    irq_restore(flags);
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
    if (KERNEL_SCHEDULER_VERSION != 31U) {
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

    unsigned int value = 0;
    unsigned long flags = irq_save();
    unsigned int before = cores[0].count;
    int ok = before == 0 &&
        queue_push_unsafe(&cores[0], 0x31U) &&
        queue_pop_unsafe(&cores[0], &value) &&
        value == 0x31U &&
        cores[0].count == 0;
    irq_restore(flags);

    return ok ? 1 : 0;
}
