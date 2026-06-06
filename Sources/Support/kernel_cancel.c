#include "Support.h"

//===----------------------------------------------------------------------===//
// Runtime V18 cooperative cancellation tokens.
//
// Fixed token records only. Swift tasks can cooperate by checking tokens; this
// layer owns storage, generation checks, and counters.
//===----------------------------------------------------------------------===//

#define KERNEL_CANCEL_TOKEN_CAPACITY_VALUE 16U
#define CANCEL_TOKEN_SLOT_MASK             0xffffU
#define CANCEL_TOKEN_GENERATION_SHIFT      16U

typedef struct cancel_token_record {
    unsigned int state;
    unsigned int generation;
    unsigned int owner_task_id;
} cancel_token_record;

static cancel_token_record tokens[KERNEL_CANCEL_TOKEN_CAPACITY_VALUE];
static unsigned int initialized;
static unsigned int active_count;
static unsigned long requested_count;
static unsigned long completed_count;
static unsigned int last_error;

static void clear_tokens_unsafe(void) {
    for (unsigned int i = 0; i < KERNEL_CANCEL_TOKEN_CAPACITY_VALUE; i++) {
        tokens[i].state = KERNEL_CANCEL_STATE_FREE;
        tokens[i].generation = 1;
        tokens[i].owner_task_id = 0;
    }
    active_count = 0;
    requested_count = 0;
    completed_count = 0;
    last_error = KERNEL_CANCEL_ERROR_NONE;
}

static unsigned int make_token(unsigned int index) {
    return ((tokens[index].generation & CANCEL_TOKEN_SLOT_MASK)
            << CANCEL_TOKEN_GENERATION_SHIFT) | (index + 1U);
}

static cancel_token_record *record_for_token(unsigned int token) {
    unsigned int raw_slot = token & CANCEL_TOKEN_SLOT_MASK;
    unsigned int generation = token >> CANCEL_TOKEN_GENERATION_SHIFT;
    if (raw_slot == 0 || raw_slot > KERNEL_CANCEL_TOKEN_CAPACITY_VALUE) {
        last_error = KERNEL_CANCEL_ERROR_BAD_TOKEN;
        return 0;
    }

    cancel_token_record *record = &tokens[raw_slot - 1U];
    if (record->state == KERNEL_CANCEL_STATE_FREE ||
        record->generation != generation) {
        last_error = KERNEL_CANCEL_ERROR_BAD_TOKEN;
        return 0;
    }
    return record;
}

void kernel_cancel_init(void) {
    unsigned long flags = irq_save();
    clear_tokens_unsafe();
    initialized = 1;
    irq_restore(flags);
}

unsigned int kernel_cancel_token_capacity(void) {
    return KERNEL_CANCEL_TOKEN_CAPACITY_VALUE;
}

unsigned int kernel_cancel_token_count(void) {
    unsigned long flags = irq_save();
    unsigned int value = active_count;
    irq_restore(flags);
    return value;
}

unsigned long kernel_cancel_requested_count(void) {
    unsigned long flags = irq_save();
    unsigned long value = requested_count;
    irq_restore(flags);
    return value;
}

unsigned long kernel_cancel_completed_count(void) {
    unsigned long flags = irq_save();
    unsigned long value = completed_count;
    irq_restore(flags);
    return value;
}

unsigned int kernel_cancel_last_error(void) {
    unsigned long flags = irq_save();
    unsigned int value = last_error;
    irq_restore(flags);
    return value;
}

unsigned int kernel_cancel_create(unsigned int owner_task_id, unsigned int *token_out) {
    unsigned long flags = irq_save();
    if (!initialized) {
        clear_tokens_unsafe();
        initialized = 1;
    }

    for (unsigned int i = 0; i < KERNEL_CANCEL_TOKEN_CAPACITY_VALUE; i++) {
        if (tokens[i].state == KERNEL_CANCEL_STATE_FREE ||
            tokens[i].state == KERNEL_CANCEL_STATE_COMPLETED) {
            if (tokens[i].state == KERNEL_CANCEL_STATE_COMPLETED) {
                tokens[i].generation++;
                if (tokens[i].generation == 0 ||
                    tokens[i].generation > CANCEL_TOKEN_SLOT_MASK) {
                    tokens[i].generation = 1;
                }
            }
            tokens[i].state = KERNEL_CANCEL_STATE_ACTIVE;
            tokens[i].owner_task_id = owner_task_id;
            active_count++;
            last_error = KERNEL_CANCEL_ERROR_NONE;
            if (token_out) {
                *token_out = make_token(i);
            }
            irq_restore(flags);
            return 1;
        }
    }

    last_error = KERNEL_CANCEL_ERROR_CAPACITY;
    if (token_out) {
        *token_out = 0;
    }
    irq_restore(flags);
    return 0;
}

unsigned int kernel_cancel_request(unsigned int token) {
    unsigned long flags = irq_save();
    cancel_token_record *record = record_for_token(token);
    if (!record ||
        record->state == KERNEL_CANCEL_STATE_COMPLETED) {
        irq_restore(flags);
        return 0;
    }
    if (record->state != KERNEL_CANCEL_STATE_CANCELLED) {
        record->state = KERNEL_CANCEL_STATE_CANCELLED;
        requested_count++;
    }
    last_error = KERNEL_CANCEL_ERROR_NONE;
    irq_restore(flags);
    return 1;
}

unsigned int kernel_cancel_is_requested(unsigned int token) {
    unsigned long flags = irq_save();
    cancel_token_record *record = record_for_token(token);
    unsigned int value = record &&
        record->state == KERNEL_CANCEL_STATE_CANCELLED ? 1U : 0U;
    if (record) {
        last_error = KERNEL_CANCEL_ERROR_NONE;
    }
    irq_restore(flags);
    return value;
}

unsigned int kernel_cancel_complete(unsigned int token) {
    unsigned long flags = irq_save();
    cancel_token_record *record = record_for_token(token);
    if (!record) {
        irq_restore(flags);
        return 0;
    }
    if (record->state == KERNEL_CANCEL_STATE_ACTIVE ||
        record->state == KERNEL_CANCEL_STATE_CANCELLED) {
        record->state = KERNEL_CANCEL_STATE_COMPLETED;
        record->owner_task_id = 0;
        if (active_count > 0) {
            active_count--;
        }
        completed_count++;
    }
    last_error = KERNEL_CANCEL_ERROR_NONE;
    irq_restore(flags);
    return 1;
}

unsigned int kernel_cancel_state(unsigned int token) {
    unsigned long flags = irq_save();
    cancel_token_record *record = record_for_token(token);
    unsigned int value = record ? record->state : KERNEL_CANCEL_STATE_FREE;
    if (record) {
        last_error = KERNEL_CANCEL_ERROR_NONE;
    }
    irq_restore(flags);
    return value;
}

unsigned int kernel_cancel_owner_task(unsigned int token) {
    unsigned long flags = irq_save();
    cancel_token_record *record = record_for_token(token);
    unsigned int value = record ? record->owner_task_id : 0U;
    if (record) {
        last_error = KERNEL_CANCEL_ERROR_NONE;
    }
    irq_restore(flags);
    return value;
}

int kernel_cancel_selftest(void) {
    kernel_cancel_init();

    unsigned int token = 0;
    if (!kernel_cancel_create(0xCAU, &token)) {
        return 0;
    }
    if (kernel_cancel_token_count() != 1U) {
        return 0;
    }
    if (kernel_cancel_owner_task(token) != 0xCAU) {
        return 0;
    }
    if (kernel_cancel_state(token) != KERNEL_CANCEL_STATE_ACTIVE) {
        return 0;
    }
    if (!kernel_cancel_request(token)) {
        return 0;
    }
    if (!kernel_cancel_is_requested(token)) {
        return 0;
    }
    if (kernel_cancel_state(token) != KERNEL_CANCEL_STATE_CANCELLED) {
        return 0;
    }
    if (!kernel_cancel_complete(token)) {
        return 0;
    }
    if (kernel_cancel_token_count() != 0U) {
        return 0;
    }
    if (kernel_cancel_state(token) != KERNEL_CANCEL_STATE_COMPLETED) {
        return 0;
    }
    if (kernel_cancel_requested_count() != 1UL ||
        kernel_cancel_completed_count() != 1UL) {
        return 0;
    }
    return kernel_cancel_last_error() == KERNEL_CANCEL_ERROR_NONE;
}
