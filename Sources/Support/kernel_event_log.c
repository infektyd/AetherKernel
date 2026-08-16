#include "Support.h"

//===----------------------------------------------------------------------===//
// Runtime V16 fixed kernel event log.
//
// This is a small overwrite-on-full ring for proof and diagnosis, not a tracing
// system. Emit coarse subsystem events only; hot-path heartbeat spam belongs in
// counters, not here.
//===----------------------------------------------------------------------===//

#define KERNEL_EVENT_CAPACITY_VALUE 64U

typedef struct event_record {
    unsigned int active;
    unsigned int kind;
    unsigned long seq;
    unsigned long ticks;
    unsigned long arg0;
    unsigned long arg1;
    unsigned long arg2;
} event_record;

static event_record events[KERNEL_EVENT_CAPACITY_VALUE];
static kernel_spinlock_t event_log_lock; /* BSS-zero: state/acquisitions/contentions == 0 == unlocked */
static unsigned int initialized;
static unsigned int write_index;
static unsigned int count_value;
static unsigned long next_sequence;
static unsigned long lost_count;

static void event_log_lock_irq(unsigned long *flags) {
    *flags = irq_save();
    kernel_spinlock_lock(&event_log_lock);
}

static void event_log_unlock_irq(unsigned long flags) {
    kernel_spinlock_unlock(&event_log_lock);
    irq_restore(flags);
}

static void clear_events_unsafe(void) {
    for (unsigned int i = 0; i < KERNEL_EVENT_CAPACITY_VALUE; i++) {
        events[i].active = 0;
        events[i].kind = 0;
        events[i].seq = 0;
        events[i].ticks = 0;
        events[i].arg0 = 0;
        events[i].arg1 = 0;
        events[i].arg2 = 0;
    }
    write_index = 0;
    count_value = 0;
    next_sequence = 1;
    lost_count = 0;
}

void kernel_event_log_init(void) {
    unsigned long flags;
    event_log_lock_irq(&flags);
    clear_events_unsafe();
    initialized = 1;
    event_log_unlock_irq(flags);
}

void kernel_event_emit(unsigned int kind,
                       unsigned long arg0,
                       unsigned long arg1,
                       unsigned long arg2) {
    unsigned long flags;
    event_log_lock_irq(&flags);
    if (!initialized) {
        clear_events_unsafe();
        initialized = 1;
    }

    event_record *record = &events[write_index];
    record->active = 1;
    record->kind = kind;
    record->seq = next_sequence;
    record->ticks = kernel_timer_now();
    record->arg0 = arg0;
    record->arg1 = arg1;
    record->arg2 = arg2;

    next_sequence++;
    write_index = (write_index + 1U) % KERNEL_EVENT_CAPACITY_VALUE;
    if (count_value < KERNEL_EVENT_CAPACITY_VALUE) {
        count_value++;
    } else {
        lost_count++;
    }
    event_log_unlock_irq(flags);
}

unsigned int kernel_event_capacity(void) {
    return KERNEL_EVENT_CAPACITY_VALUE;
}

unsigned int kernel_event_count(void) {
    unsigned long flags;
    event_log_lock_irq(&flags);
    unsigned int count = count_value;
    event_log_unlock_irq(flags);
    return count;
}

unsigned long kernel_event_lost_count(void) {
    unsigned long flags;
    event_log_lock_irq(&flags);
    unsigned long count = lost_count;
    event_log_unlock_irq(flags);
    return count;
}

unsigned long kernel_event_sequence(void) {
    unsigned long flags;
    event_log_lock_irq(&flags);
    unsigned long seq = next_sequence == 0 ? 0 : next_sequence - 1UL;
    event_log_unlock_irq(flags);
    return seq;
}

static event_record *event_at_logical_index(unsigned int index) {
    if (index >= count_value) {
        return 0;
    }

    unsigned int oldest = 0;
    if (count_value == KERNEL_EVENT_CAPACITY_VALUE) {
        oldest = write_index;
    }
    unsigned int physical = (oldest + index) % KERNEL_EVENT_CAPACITY_VALUE;
    if (!events[physical].active) {
        return 0;
    }
    return &events[physical];
}

unsigned int kernel_event_kind(unsigned int index) {
    unsigned long flags;
    event_log_lock_irq(&flags);
    event_record *record = event_at_logical_index(index);
    unsigned int value = record ? record->kind : 0;
    event_log_unlock_irq(flags);
    return value;
}

unsigned long kernel_event_ticks(unsigned int index) {
    unsigned long flags;
    event_log_lock_irq(&flags);
    event_record *record = event_at_logical_index(index);
    unsigned long value = record ? record->ticks : 0;
    event_log_unlock_irq(flags);
    return value;
}

unsigned long kernel_event_seq(unsigned int index) {
    unsigned long flags;
    event_log_lock_irq(&flags);
    event_record *record = event_at_logical_index(index);
    unsigned long value = record ? record->seq : 0;
    event_log_unlock_irq(flags);
    return value;
}

unsigned long kernel_event_arg0(unsigned int index) {
    unsigned long flags;
    event_log_lock_irq(&flags);
    event_record *record = event_at_logical_index(index);
    unsigned long value = record ? record->arg0 : 0;
    event_log_unlock_irq(flags);
    return value;
}

unsigned long kernel_event_arg1(unsigned int index) {
    unsigned long flags;
    event_log_lock_irq(&flags);
    event_record *record = event_at_logical_index(index);
    unsigned long value = record ? record->arg1 : 0;
    event_log_unlock_irq(flags);
    return value;
}

unsigned long kernel_event_arg2(unsigned int index) {
    unsigned long flags;
    event_log_lock_irq(&flags);
    event_record *record = event_at_logical_index(index);
    unsigned long value = record ? record->arg2 : 0;
    event_log_unlock_irq(flags);
    return value;
}

int kernel_event_log_selftest(void) {
    if (!initialized) {
        kernel_event_log_init();
    }
    if (kernel_event_capacity() != KERNEL_EVENT_CAPACITY_VALUE) {
        return 0;
    }
    if (kernel_event_count() < KERNEL_EVENT_CAPACITY_VALUE) {
        unsigned int before = kernel_event_count();
        kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, before, KERNEL_EVENT_CAPACITY_VALUE, kernel_event_lost_count());
        if (kernel_event_count() != before + 1U) {
            return 0;
        }
        unsigned int last = kernel_event_count() - 1U;
        if (kernel_event_kind(last) != KERNEL_EVENT_KIND_SELFTEST) {
            return 0;
        }
    } else {
        /* Full ring: read-only walk — no emit (would bump lost_count). */
        unsigned int full_count = kernel_event_count();
        if (full_count != KERNEL_EVENT_CAPACITY_VALUE) {
            return 0;
        }
        for (unsigned int i = 0; i < full_count; i++) {
            if (kernel_event_seq(i) == 0UL) {
                return 0;
            }
            (void)kernel_event_kind(i);
            (void)kernel_event_ticks(i);
            (void)kernel_event_arg0(i);
            (void)kernel_event_arg1(i);
            (void)kernel_event_arg2(i);
        }
    }
    return 1;
}
