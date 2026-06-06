#include "Support.h"

//===----------------------------------------------------------------------===//
// Runtime V22 fixed guarded typed pools.
//
// This is a tiny slab substrate beside the heap, not a replacement allocator.
// Every pool has fixed slots, guard words, generation counters, and explicit
// counters so Swift can inspect ownership pressure without depending on ARC or
// heap allocation on hot paths.
//
// Runtime V23 pool pressure aggregate telemetry summarizes the same fixed
// records across active pools so bootcert/shell probes can see pressure without
// walking every pool line.
//===----------------------------------------------------------------------===//

#define KERNEL_POOL_CAPACITY_VALUE 4U
#define KERNEL_POOL_SLOT_CAPACITY_VALUE 8U
#define KERNEL_POOL_SLOT_BYTES_VALUE 64U
#define KERNEL_POOL_GUARD_HEAD 0xa37e2200feedbabeUL
#define KERNEL_POOL_GUARD_TAIL 0xa37e2200deadbeefUL

typedef struct kernel_pool_slot {
    unsigned int active;
    unsigned int generation;
    unsigned long head_guard;
    unsigned char payload[KERNEL_POOL_SLOT_BYTES_VALUE];
    unsigned long tail_guard;
} kernel_pool_slot;

typedef struct kernel_pool_record {
    unsigned int active;
    const unsigned char *name;
    unsigned int name_len;
    unsigned int slot_size;
    unsigned int slot_capacity;
    unsigned int used;
    unsigned int high_water;
    unsigned int generation;
    unsigned long allocs;
    unsigned long frees;
    unsigned long failed_allocs;
    unsigned long bad_frees;
    unsigned long double_frees;
    unsigned int last_error;
    kernel_pool_slot slots[KERNEL_POOL_SLOT_CAPACITY_VALUE];
} kernel_pool_record;

static kernel_pool_record pools[KERNEL_POOL_CAPACITY_VALUE];
static unsigned int initialized;
static unsigned int pool_count_value;

static const unsigned char selftest_name[] = "selftest";
static const unsigned char runtime_name[] = "runtime";
static const unsigned char messages_name[] = "messages";

static unsigned int bytes_len(const unsigned char *name) {
    unsigned int n = 0;
    while (name[n] != '\0') {
        n++;
    }
    return n;
}

static unsigned int next_generation(unsigned int generation) {
    unsigned int next = generation + 1U;
    return next == 0 ? 1U : next;
}

static void clear_payload_unsafe(kernel_pool_slot *slot) {
    for (unsigned int i = 0; i < KERNEL_POOL_SLOT_BYTES_VALUE; i++) {
        slot->payload[i] = 0;
    }
}

static void poison_payload_unsafe(kernel_pool_slot *slot) {
    for (unsigned int i = 0; i < KERNEL_POOL_SLOT_BYTES_VALUE; i++) {
        slot->payload[i] = 0xddU;
    }
}

static void clear_record_unsafe(kernel_pool_record *pool) {
    pool->active = 0;
    pool->name = 0;
    pool->name_len = 0;
    pool->slot_size = 0;
    pool->slot_capacity = 0;
    pool->used = 0;
    pool->high_water = 0;
    pool->generation = 1;
    pool->allocs = 0;
    pool->frees = 0;
    pool->failed_allocs = 0;
    pool->bad_frees = 0;
    pool->double_frees = 0;
    pool->last_error = KERNEL_POOL_ERROR_NONE;
    for (unsigned int i = 0; i < KERNEL_POOL_SLOT_CAPACITY_VALUE; i++) {
        pool->slots[i].active = 0;
        pool->slots[i].generation = 1;
        pool->slots[i].head_guard = KERNEL_POOL_GUARD_HEAD;
        pool->slots[i].tail_guard = KERNEL_POOL_GUARD_TAIL;
        clear_payload_unsafe(&pool->slots[i]);
    }
}

static void activate_pool_unsafe(unsigned int pool_id,
                                 const unsigned char *name,
                                 unsigned int slot_size) {
    if (pool_id >= KERNEL_POOL_CAPACITY_VALUE ||
        slot_size == 0 ||
        slot_size > KERNEL_POOL_SLOT_BYTES_VALUE) {
        return;
    }

    clear_record_unsafe(&pools[pool_id]);
    pools[pool_id].active = 1;
    pools[pool_id].name = name;
    pools[pool_id].name_len = bytes_len(name);
    pools[pool_id].slot_size = slot_size;
    pools[pool_id].slot_capacity = KERNEL_POOL_SLOT_CAPACITY_VALUE;
}

static void clear_all_unsafe(void) {
    for (unsigned int i = 0; i < KERNEL_POOL_CAPACITY_VALUE; i++) {
        clear_record_unsafe(&pools[i]);
    }
    activate_pool_unsafe(KERNEL_POOL_SELFTEST_ID, selftest_name, 32U);
    activate_pool_unsafe(1U, runtime_name, 64U);
    activate_pool_unsafe(2U, messages_name, 64U);
    pool_count_value = 3U;
}

static void ensure_initialized_unsafe(void) {
    if (!initialized) {
        clear_all_unsafe();
        initialized = 1;
    }
}

static kernel_pool_record *pool_at_unsafe(unsigned int pool_id) {
    if (pool_id >= KERNEL_POOL_CAPACITY_VALUE || !pools[pool_id].active) {
        return 0;
    }
    return &pools[pool_id];
}

static int slot_guards_ok(kernel_pool_slot *slot) {
    return slot->head_guard == KERNEL_POOL_GUARD_HEAD &&
           slot->tail_guard == KERNEL_POOL_GUARD_TAIL;
}

void kernel_pool_init(void) {
    unsigned long flags = irq_save();
    clear_all_unsafe();
    initialized = 1;
    irq_restore(flags);
}

unsigned int kernel_pool_count(void) {
    unsigned long flags = irq_save();
    ensure_initialized_unsafe();
    unsigned int count = pool_count_value;
    irq_restore(flags);
    return count;
}

unsigned int kernel_pool_capacity(void) {
    return KERNEL_POOL_CAPACITY_VALUE;
}

unsigned int kernel_pool_name_len(unsigned int pool_id) {
    unsigned long flags = irq_save();
    ensure_initialized_unsafe();
    kernel_pool_record *pool = pool_at_unsafe(pool_id);
    unsigned int value = pool ? pool->name_len : 0;
    irq_restore(flags);
    return value;
}

unsigned int kernel_pool_name_byte(unsigned int pool_id, unsigned int offset) {
    unsigned long flags = irq_save();
    ensure_initialized_unsafe();
    kernel_pool_record *pool = pool_at_unsafe(pool_id);
    unsigned int value = 0;
    if (pool && offset < pool->name_len) {
        value = (unsigned int)(unsigned char)pool->name[offset];
    }
    irq_restore(flags);
    return value;
}

unsigned int kernel_pool_slot_size(unsigned int pool_id) {
    unsigned long flags = irq_save();
    ensure_initialized_unsafe();
    kernel_pool_record *pool = pool_at_unsafe(pool_id);
    unsigned int value = pool ? pool->slot_size : 0;
    irq_restore(flags);
    return value;
}

unsigned int kernel_pool_slot_capacity(unsigned int pool_id) {
    unsigned long flags = irq_save();
    ensure_initialized_unsafe();
    kernel_pool_record *pool = pool_at_unsafe(pool_id);
    unsigned int value = pool ? pool->slot_capacity : 0;
    irq_restore(flags);
    return value;
}

unsigned int kernel_pool_used(unsigned int pool_id) {
    unsigned long flags = irq_save();
    ensure_initialized_unsafe();
    kernel_pool_record *pool = pool_at_unsafe(pool_id);
    unsigned int value = pool ? pool->used : 0;
    irq_restore(flags);
    return value;
}

unsigned int kernel_pool_high_water(unsigned int pool_id) {
    unsigned long flags = irq_save();
    ensure_initialized_unsafe();
    kernel_pool_record *pool = pool_at_unsafe(pool_id);
    unsigned int value = pool ? pool->high_water : 0;
    irq_restore(flags);
    return value;
}

unsigned int kernel_pool_generation(unsigned int pool_id) {
    unsigned long flags = irq_save();
    ensure_initialized_unsafe();
    kernel_pool_record *pool = pool_at_unsafe(pool_id);
    unsigned int value = pool ? pool->generation : 0;
    irq_restore(flags);
    return value;
}

unsigned int kernel_pool_total_slot_count(void) {
    unsigned long flags = irq_save();
    ensure_initialized_unsafe();
    unsigned int total = 0;
    for (unsigned int i = 0; i < KERNEL_POOL_CAPACITY_VALUE; i++) {
        if (pools[i].active) {
            total += pools[i].slot_capacity;
        }
    }
    irq_restore(flags);
    return total;
}

unsigned int kernel_pool_used_slot_count(void) {
    unsigned long flags = irq_save();
    ensure_initialized_unsafe();
    unsigned int total = 0;
    for (unsigned int i = 0; i < KERNEL_POOL_CAPACITY_VALUE; i++) {
        if (pools[i].active) {
            total += pools[i].used;
        }
    }
    irq_restore(flags);
    return total;
}

unsigned int kernel_pool_high_water_slot_count(void) {
    unsigned long flags = irq_save();
    ensure_initialized_unsafe();
    unsigned int total = 0;
    for (unsigned int i = 0; i < KERNEL_POOL_CAPACITY_VALUE; i++) {
        if (pools[i].active) {
            total += pools[i].high_water;
        }
    }
    irq_restore(flags);
    return total;
}

unsigned long kernel_pool_alloc_count(unsigned int pool_id) {
    unsigned long flags = irq_save();
    ensure_initialized_unsafe();
    kernel_pool_record *pool = pool_at_unsafe(pool_id);
    unsigned long value = pool ? pool->allocs : 0;
    irq_restore(flags);
    return value;
}

unsigned long kernel_pool_free_count(unsigned int pool_id) {
    unsigned long flags = irq_save();
    ensure_initialized_unsafe();
    kernel_pool_record *pool = pool_at_unsafe(pool_id);
    unsigned long value = pool ? pool->frees : 0;
    irq_restore(flags);
    return value;
}

unsigned long kernel_pool_failed_alloc_count(unsigned int pool_id) {
    unsigned long flags = irq_save();
    ensure_initialized_unsafe();
    kernel_pool_record *pool = pool_at_unsafe(pool_id);
    unsigned long value = pool ? pool->failed_allocs : 0;
    irq_restore(flags);
    return value;
}

unsigned long kernel_pool_bad_free_count(unsigned int pool_id) {
    unsigned long flags = irq_save();
    ensure_initialized_unsafe();
    kernel_pool_record *pool = pool_at_unsafe(pool_id);
    unsigned long value = pool ? pool->bad_frees : 0;
    irq_restore(flags);
    return value;
}

unsigned long kernel_pool_double_free_count(unsigned int pool_id) {
    unsigned long flags = irq_save();
    ensure_initialized_unsafe();
    kernel_pool_record *pool = pool_at_unsafe(pool_id);
    unsigned long value = pool ? pool->double_frees : 0;
    irq_restore(flags);
    return value;
}

unsigned long kernel_pool_failed_alloc_total(void) {
    unsigned long flags = irq_save();
    ensure_initialized_unsafe();
    unsigned long total = 0;
    for (unsigned int i = 0; i < KERNEL_POOL_CAPACITY_VALUE; i++) {
        if (pools[i].active) {
            total += pools[i].failed_allocs;
        }
    }
    irq_restore(flags);
    return total;
}

unsigned long kernel_pool_bad_free_total(void) {
    unsigned long flags = irq_save();
    ensure_initialized_unsafe();
    unsigned long total = 0;
    for (unsigned int i = 0; i < KERNEL_POOL_CAPACITY_VALUE; i++) {
        if (pools[i].active) {
            total += pools[i].bad_frees;
        }
    }
    irq_restore(flags);
    return total;
}

unsigned long kernel_pool_double_free_total(void) {
    unsigned long flags = irq_save();
    ensure_initialized_unsafe();
    unsigned long total = 0;
    for (unsigned int i = 0; i < KERNEL_POOL_CAPACITY_VALUE; i++) {
        if (pools[i].active) {
            total += pools[i].double_frees;
        }
    }
    irq_restore(flags);
    return total;
}

unsigned int kernel_pool_last_error(unsigned int pool_id) {
    unsigned long flags = irq_save();
    ensure_initialized_unsafe();
    kernel_pool_record *pool = pool_at_unsafe(pool_id);
    unsigned int value = pool ? pool->last_error : KERNEL_POOL_ERROR_BAD_POOL;
    irq_restore(flags);
    return value;
}

void *kernel_pool_alloc(unsigned int pool_id) {
    unsigned long flags = irq_save();
    ensure_initialized_unsafe();
    kernel_pool_record *pool = pool_at_unsafe(pool_id);
    if (!pool) {
        irq_restore(flags);
        return 0;
    }

    for (unsigned int i = 0; i < pool->slot_capacity; i++) {
        kernel_pool_slot *slot = &pool->slots[i];
        if (!slot->active) {
            slot->active = 1;
            slot->generation = next_generation(slot->generation);
            slot->head_guard = KERNEL_POOL_GUARD_HEAD;
            slot->tail_guard = KERNEL_POOL_GUARD_TAIL;
            clear_payload_unsafe(slot);
            pool->used++;
            if (pool->used > pool->high_water) {
                pool->high_water = pool->used;
            }
            pool->generation = next_generation(pool->generation);
            pool->allocs++;
            pool->last_error = KERNEL_POOL_ERROR_NONE;
            void *payload = (void *)slot->payload;
            irq_restore(flags);
            return payload;
        }
    }

    pool->failed_allocs++;
    pool->last_error = KERNEL_POOL_ERROR_FULL;
    irq_restore(flags);
    return 0;
}

int kernel_pool_free(unsigned int pool_id, void *ptr) {
    unsigned long flags = irq_save();
    ensure_initialized_unsafe();
    kernel_pool_record *pool = pool_at_unsafe(pool_id);
    if (!pool || !ptr) {
        if (pool) {
            pool->bad_frees++;
            pool->last_error = KERNEL_POOL_ERROR_BAD_FREE;
        }
        irq_restore(flags);
        return 0;
    }

    for (unsigned int i = 0; i < pool->slot_capacity; i++) {
        kernel_pool_slot *slot = &pool->slots[i];
        if ((void *)slot->payload == ptr) {
            if (!slot->active) {
                pool->double_frees++;
                pool->last_error = KERNEL_POOL_ERROR_DOUBLE_FREE;
                irq_restore(flags);
                return 0;
            }
            if (!slot_guards_ok(slot)) {
                pool->bad_frees++;
                pool->last_error = KERNEL_POOL_ERROR_GUARD;
                irq_restore(flags);
                return 0;
            }
            slot->active = 0;
            poison_payload_unsafe(slot);
            if (pool->used > 0) {
                pool->used--;
            }
            pool->generation = next_generation(pool->generation);
            pool->frees++;
            pool->last_error = KERNEL_POOL_ERROR_NONE;
            irq_restore(flags);
            return 1;
        }
    }

    pool->bad_frees++;
    pool->last_error = KERNEL_POOL_ERROR_BAD_FREE;
    irq_restore(flags);
    return 0;
}

static void reset_selftest_pool(void) {
    unsigned long flags = irq_save();
    ensure_initialized_unsafe();
    activate_pool_unsafe(KERNEL_POOL_SELFTEST_ID, selftest_name, 32U);
    irq_restore(flags);
}

int kernel_pool_selftest(void) {
    void *slots[KERNEL_POOL_SLOT_CAPACITY_VALUE];

    reset_selftest_pool();
    if (kernel_pool_capacity() != KERNEL_POOL_CAPACITY_VALUE ||
        kernel_pool_count() != 3U ||
        kernel_pool_slot_capacity(KERNEL_POOL_SELFTEST_ID) != KERNEL_POOL_SLOT_CAPACITY_VALUE ||
        kernel_pool_slot_size(KERNEL_POOL_SELFTEST_ID) != 32U) {
        return 0;
    }

    void *first = kernel_pool_alloc(KERNEL_POOL_SELFTEST_ID);
    if (!first ||
        kernel_pool_used(KERNEL_POOL_SELFTEST_ID) != 1U ||
        kernel_pool_high_water(KERNEL_POOL_SELFTEST_ID) != 1U) {
        return 0;
    }
    if (!kernel_pool_free(KERNEL_POOL_SELFTEST_ID, first) ||
        kernel_pool_used(KERNEL_POOL_SELFTEST_ID) != 0U) {
        return 0;
    }
    if (kernel_pool_free(KERNEL_POOL_SELFTEST_ID, first)) {
        return 0;
    }
    if (kernel_pool_double_free_count(KERNEL_POOL_SELFTEST_ID) != 1UL ||
        kernel_pool_last_error(KERNEL_POOL_SELFTEST_ID) != KERNEL_POOL_ERROR_DOUBLE_FREE) {
        return 0;
    }

    unsigned long not_a_slot = 0;
    if (kernel_pool_free(KERNEL_POOL_SELFTEST_ID, &not_a_slot)) {
        return 0;
    }
    if (kernel_pool_bad_free_count(KERNEL_POOL_SELFTEST_ID) != 1UL ||
        kernel_pool_last_error(KERNEL_POOL_SELFTEST_ID) != KERNEL_POOL_ERROR_BAD_FREE) {
        return 0;
    }

    for (unsigned int i = 0; i < KERNEL_POOL_SLOT_CAPACITY_VALUE; i++) {
        slots[i] = kernel_pool_alloc(KERNEL_POOL_SELFTEST_ID);
        if (!slots[i]) {
            return 0;
        }
    }
    if (kernel_pool_alloc(KERNEL_POOL_SELFTEST_ID) != 0) {
        return 0;
    }
    if (kernel_pool_failed_alloc_count(KERNEL_POOL_SELFTEST_ID) != 1UL ||
        kernel_pool_last_error(KERNEL_POOL_SELFTEST_ID) != KERNEL_POOL_ERROR_FULL ||
        kernel_pool_high_water(KERNEL_POOL_SELFTEST_ID) != KERNEL_POOL_SLOT_CAPACITY_VALUE) {
        return 0;
    }
    for (unsigned int i = 0; i < KERNEL_POOL_SLOT_CAPACITY_VALUE; i++) {
        if (!kernel_pool_free(KERNEL_POOL_SELFTEST_ID, slots[i])) {
            return 0;
        }
    }

    return kernel_pool_used(KERNEL_POOL_SELFTEST_ID) == 0U &&
           kernel_pool_alloc_count(KERNEL_POOL_SELFTEST_ID) == 9UL &&
           kernel_pool_free_count(KERNEL_POOL_SELFTEST_ID) == 9UL &&
           kernel_pool_failed_alloc_count(KERNEL_POOL_SELFTEST_ID) == 1UL &&
           kernel_pool_bad_free_count(KERNEL_POOL_SELFTEST_ID) == 1UL &&
           kernel_pool_double_free_count(KERNEL_POOL_SELFTEST_ID) == 1UL;
}

int kernel_pool_pressure_selftest(void) {
    int ok = kernel_pool_selftest();
    unsigned int active = kernel_pool_count();
    unsigned int total_slots = kernel_pool_total_slot_count();
    unsigned int used_slots = kernel_pool_used_slot_count();
    unsigned int high_water_slots = kernel_pool_high_water_slot_count();
    unsigned long failed = kernel_pool_failed_alloc_total();
    unsigned long bad = kernel_pool_bad_free_total();
    unsigned long doubles = kernel_pool_double_free_total();

    return ok != 0 &&
           active == 3U &&
           total_slots == active * KERNEL_POOL_SLOT_CAPACITY_VALUE &&
           used_slots == 0U &&
           high_water_slots >= KERNEL_POOL_SLOT_CAPACITY_VALUE &&
           failed >= 1UL &&
           bad >= 1UL &&
           doubles >= 1UL;
}
