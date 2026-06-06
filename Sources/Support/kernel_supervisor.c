#include "Support.h"

//===----------------------------------------------------------------------===//
// Runtime V14 deterministic cooperative supervisor.
//===----------------------------------------------------------------------===//

#define KERNEL_SUPERVISOR_CAPACITY_VALUE 8U

typedef struct kernel_supervisor_record {
    unsigned int active;
    unsigned int task_id;
    unsigned int deadline_ms;
    unsigned int policy;
    unsigned int state;
    unsigned long last_heartbeat_ms;
    unsigned long missed;
} kernel_supervisor_record;

static kernel_supervisor_record supervisors[KERNEL_SUPERVISOR_CAPACITY_VALUE];
static unsigned int supervisor_initialized;
static unsigned int supervisor_count_value;

unsigned long kernel_supervisor_now_ms(void) {
    unsigned long freq = read_cntfrq();
    if (freq == 0) {
        return 0;
    }
    return (kernel_timer_now() * 1000UL) / freq;
}

static void clear_supervisors_unsafe(void) {
    for (unsigned int i = 0; i < KERNEL_SUPERVISOR_CAPACITY_VALUE; i++) {
        supervisors[i].active = 0;
        supervisors[i].task_id = 0;
        supervisors[i].deadline_ms = 0;
        supervisors[i].policy = KERNEL_SUPERVISOR_POLICY_OBSERVE;
        supervisors[i].state = KERNEL_SUPERVISOR_STATE_HEALTHY;
        supervisors[i].last_heartbeat_ms = 0;
        supervisors[i].missed = 0;
    }
    supervisor_count_value = 0;
}

static kernel_supervisor_record *record_for_task(unsigned int task_id) {
    for (unsigned int i = 0; i < KERNEL_SUPERVISOR_CAPACITY_VALUE; i++) {
        if (supervisors[i].active && supervisors[i].task_id == task_id) {
            return &supervisors[i];
        }
    }
    return 0;
}

static kernel_supervisor_record *record_at(unsigned int index) {
    if (index >= KERNEL_SUPERVISOR_CAPACITY_VALUE || !supervisors[index].active) {
        return 0;
    }
    return &supervisors[index];
}

void kernel_supervisor_init(void) {
    unsigned long flags = irq_save();
    clear_supervisors_unsafe();
    supervisor_initialized = 1;
    irq_restore(flags);
}

unsigned int kernel_supervisor_register_task(unsigned int task_id,
                                             unsigned int deadline_ms,
                                             unsigned int policy) {
    unsigned long flags = irq_save();
    if (!supervisor_initialized) {
        clear_supervisors_unsafe();
        supervisor_initialized = 1;
    }

    kernel_supervisor_record *existing = record_for_task(task_id);
    if (existing) {
        existing->deadline_ms = deadline_ms;
        existing->policy = policy;
        existing->state = KERNEL_SUPERVISOR_STATE_HEALTHY;
        existing->last_heartbeat_ms = kernel_supervisor_now_ms();
        irq_restore(flags);
        return 1;
    }

    for (unsigned int i = 0; i < KERNEL_SUPERVISOR_CAPACITY_VALUE; i++) {
        if (!supervisors[i].active) {
            supervisors[i].active = 1;
            supervisors[i].task_id = task_id;
            supervisors[i].deadline_ms = deadline_ms;
            supervisors[i].policy = policy;
            supervisors[i].state = KERNEL_SUPERVISOR_STATE_HEALTHY;
            supervisors[i].last_heartbeat_ms = kernel_supervisor_now_ms();
            supervisors[i].missed = 0;
            supervisor_count_value++;
            irq_restore(flags);
            return 1;
        }
    }

    irq_restore(flags);
    kernel_panic("kernel-supervisor-full");
    return 0;
}

void kernel_supervisor_heartbeat(unsigned int task_id) {
    unsigned long flags = irq_save();
    kernel_supervisor_record *record = record_for_task(task_id);
    if (record) {
        record->last_heartbeat_ms = kernel_supervisor_now_ms();
        record->state = KERNEL_SUPERVISOR_STATE_HEALTHY;
    }
    irq_restore(flags);
}

void kernel_supervisor_check(void) {
    unsigned long flags = irq_save();
    unsigned long now = kernel_supervisor_now_ms();

    for (unsigned int i = 0; i < KERNEL_SUPERVISOR_CAPACITY_VALUE; i++) {
        kernel_supervisor_record *record = &supervisors[i];
        if (!record->active || record->deadline_ms == 0) {
            continue;
        }
        if ((now - record->last_heartbeat_ms) > (unsigned long)record->deadline_ms) {
            if (record->state != KERNEL_SUPERVISOR_STATE_MISSED) {
                record->missed++;
            }
            record->state = KERNEL_SUPERVISOR_STATE_MISSED;
            if (record->policy == KERNEL_SUPERVISOR_POLICY_PANIC) {
                irq_restore(flags);
                kernel_panic("supervisor-missed-heartbeat");
            }
        }
    }

    irq_restore(flags);
}

unsigned int kernel_supervisor_count(void) {
    unsigned long flags = irq_save();
    unsigned int count = supervisor_count_value;
    irq_restore(flags);
    return count;
}

unsigned int kernel_supervisor_capacity(void) {
    return KERNEL_SUPERVISOR_CAPACITY_VALUE;
}

unsigned int kernel_supervisor_unhealthy_count(void) {
    kernel_supervisor_check();
    unsigned long flags = irq_save();
    unsigned int count = 0;
    for (unsigned int i = 0; i < KERNEL_SUPERVISOR_CAPACITY_VALUE; i++) {
        if (supervisors[i].active &&
            supervisors[i].state == KERNEL_SUPERVISOR_STATE_MISSED) {
            count++;
        }
    }
    irq_restore(flags);
    return count;
}

unsigned long kernel_supervisor_total_missed_count(void) {
    kernel_supervisor_check();
    unsigned long flags = irq_save();
    unsigned long count = 0;
    for (unsigned int i = 0; i < KERNEL_SUPERVISOR_CAPACITY_VALUE; i++) {
        if (supervisors[i].active) {
            count += supervisors[i].missed;
        }
    }
    irq_restore(flags);
    return count;
}

unsigned int kernel_supervisor_task_id(unsigned int index) {
    unsigned long flags = irq_save();
    kernel_supervisor_record *record = record_at(index);
    unsigned int value = record ? record->task_id : 0;
    irq_restore(flags);
    return value;
}

unsigned int kernel_supervisor_deadline_ms(unsigned int index) {
    unsigned long flags = irq_save();
    kernel_supervisor_record *record = record_at(index);
    unsigned int value = record ? record->deadline_ms : 0;
    irq_restore(flags);
    return value;
}

unsigned long kernel_supervisor_last_heartbeat_ms(unsigned int index) {
    unsigned long flags = irq_save();
    kernel_supervisor_record *record = record_at(index);
    unsigned long value = record ? record->last_heartbeat_ms : 0;
    irq_restore(flags);
    return value;
}

unsigned long kernel_supervisor_missed_count(unsigned int index) {
    unsigned long flags = irq_save();
    kernel_supervisor_record *record = record_at(index);
    unsigned long value = record ? record->missed : 0;
    irq_restore(flags);
    return value;
}

unsigned int kernel_supervisor_state(unsigned int index) {
    unsigned long flags = irq_save();
    kernel_supervisor_record *record = record_at(index);
    unsigned int value = record ? record->state : 0;
    irq_restore(flags);
    return value;
}

unsigned int kernel_supervisor_policy(unsigned int index) {
    unsigned long flags = irq_save();
    kernel_supervisor_record *record = record_at(index);
    unsigned int value = record ? record->policy : 0;
    irq_restore(flags);
    return value;
}

int kernel_supervisor_selftest(void) {
    if (!supervisor_initialized) {
        return 0;
    }
    if (kernel_supervisor_capacity() != KERNEL_SUPERVISOR_CAPACITY_VALUE) {
        return 0;
    }
    if (kernel_supervisor_count() == 0) {
        return 0;
    }
    for (unsigned int i = 0; i < KERNEL_SUPERVISOR_CAPACITY_VALUE; i++) {
        if (supervisors[i].active &&
            supervisors[i].state != KERNEL_SUPERVISOR_STATE_HEALTHY &&
            supervisors[i].state != KERNEL_SUPERVISOR_STATE_MISSED) {
            return 0;
        }
    }
    return 1;
}
