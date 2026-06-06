#include "Support.h"

//===----------------------------------------------------------------------===//
// Runtime V12 fixed kernel object and task registries.
//
// All records are fixed-capacity and store static name pointers supplied by
// Swift or this C module. No allocation is allowed here; later runtime layers can
// build capabilities, mailboxes, and drivers on top of these stable handles.
//===----------------------------------------------------------------------===//

#define KERNEL_OBJECT_CAPACITY 16U
#define KERNEL_TASK_CAPACITY   8U

typedef struct kernel_object_record {
    unsigned int active;
    unsigned int id;
    unsigned int kind;
    unsigned int flags;
    unsigned int caps;
    unsigned int generation;
    const unsigned char *name;
    unsigned int name_len;
} kernel_object_record;

typedef struct kernel_task_record {
    unsigned int active;
    unsigned int object_id;
    unsigned int parent_task_id;
    unsigned int state;
    unsigned int period_ms;
    const unsigned char *name;
    unsigned int name_len;
    unsigned long ticks;
    unsigned long spawns;
    unsigned long completions;
} kernel_task_record;

static kernel_object_record objects[KERNEL_OBJECT_CAPACITY];
static kernel_task_record tasks[KERNEL_TASK_CAPACITY];
static unsigned int object_initialized;
static unsigned int task_initialized;
static unsigned int object_count_value;
static unsigned int task_count_value;
static unsigned int object_handle_last_error_value;

#define OBJECT_HANDLE_SLOT_MASK 0xffUL
#define OBJECT_HANDLE_GENERATION_SHIFT 8U
#define OBJECT_HANDLE_GENERATION_MASK 0xffffUL
#define OBJECT_HANDLE_KIND_SHIFT 24U
#define OBJECT_HANDLE_KIND_MASK 0xffUL
#define OBJECT_HANDLE_CAP_SHIFT 32U

static unsigned int cstr_len(const char *name) {
    unsigned int n = 0;
    while (name[n] != '\0') {
        n++;
    }
    return n;
}

static void clear_objects_unsafe(void) {
    for (unsigned int i = 0; i < KERNEL_OBJECT_CAPACITY; i++) {
        objects[i].active = 0;
        objects[i].id = i + 1U;
        objects[i].kind = 0;
        objects[i].flags = 0;
        objects[i].caps = 0;
        objects[i].generation = 1;
        objects[i].name = 0;
        objects[i].name_len = 0;
    }
    object_count_value = 0;
    object_handle_last_error_value = KERNEL_OBJECT_LOOKUP_OK;
}

static void clear_tasks_unsafe(void) {
    for (unsigned int i = 0; i < KERNEL_TASK_CAPACITY; i++) {
        tasks[i].active = 0;
        tasks[i].object_id = 0;
        tasks[i].parent_task_id = KERNEL_TASK_ROOT_PARENT;
        tasks[i].state = KERNEL_TASK_STATE_IDLE;
        tasks[i].period_ms = 0;
        tasks[i].name = 0;
        tasks[i].name_len = 0;
        tasks[i].ticks = 0;
        tasks[i].spawns = 0;
        tasks[i].completions = 0;
    }
    task_count_value = 0;
}

void kernel_object_registry_init(void) {
    unsigned long flags = irq_save();
    clear_objects_unsafe();
    object_initialized = 1;
    irq_restore(flags);

    (void)kernel_object_register(KERNEL_OBJECT_KIND_RUNTIME,
                                 KERNEL_OBJECT_FLAG_ACTIVE,
                                 (const unsigned char *)"runtime",
                                 cstr_len("runtime"));
    (void)kernel_object_register(KERNEL_OBJECT_KIND_DRIVER,
                                 KERNEL_OBJECT_FLAG_ACTIVE,
                                 (const unsigned char *)"uart0",
                                 cstr_len("uart0"));
    (void)kernel_object_register(KERNEL_OBJECT_KIND_DRIVER,
                                 KERNEL_OBJECT_FLAG_ACTIVE,
                                 (const unsigned char *)"cntp",
                                 cstr_len("cntp"));
}

static unsigned int next_generation(unsigned int generation) {
    unsigned int next = (generation + 1U) & (unsigned int)OBJECT_HANDLE_GENERATION_MASK;
    return next == 0 ? 1U : next;
}

static unsigned int object_default_caps(unsigned int kind) {
    if (kind == KERNEL_OBJECT_KIND_DRIVER) {
        return KERNEL_OBJECT_CAP_INSPECT | KERNEL_OBJECT_CAP_CONTROL;
    }
    if (kind == KERNEL_OBJECT_KIND_TASK) {
        return KERNEL_OBJECT_CAP_INSPECT | KERNEL_OBJECT_CAP_SUPERVISE;
    }
    if (kind == KERNEL_OBJECT_KIND_MAILBOX) {
        return KERNEL_OBJECT_CAP_INSPECT | KERNEL_OBJECT_CAP_SEND | KERNEL_OBJECT_CAP_RECEIVE;
    }
    return KERNEL_OBJECT_CAP_INSPECT;
}

static int caps_include(unsigned int available, unsigned int required) {
    return (required & ~available) == 0;
}

static unsigned long encode_object_handle(unsigned int index, unsigned int generation,
                                          unsigned int kind, unsigned int caps) {
    return ((unsigned long)(index + 1U) & OBJECT_HANDLE_SLOT_MASK) |
           (((unsigned long)generation & OBJECT_HANDLE_GENERATION_MASK) << OBJECT_HANDLE_GENERATION_SHIFT) |
           (((unsigned long)kind & OBJECT_HANDLE_KIND_MASK) << OBJECT_HANDLE_KIND_SHIFT) |
           ((unsigned long)caps << OBJECT_HANDLE_CAP_SHIFT);
}

static unsigned int decode_object_handle_slot(unsigned long handle) {
    unsigned int slot = (unsigned int)(handle & OBJECT_HANDLE_SLOT_MASK);
    if (slot == 0 || slot > KERNEL_OBJECT_CAPACITY) {
        return KERNEL_OBJECT_CAPACITY;
    }
    return slot - 1U;
}

static unsigned int decode_object_handle_generation(unsigned long handle) {
    return (unsigned int)((handle >> OBJECT_HANDLE_GENERATION_SHIFT) &
                          OBJECT_HANDLE_GENERATION_MASK);
}

static unsigned int decode_object_handle_kind(unsigned long handle) {
    return (unsigned int)((handle >> OBJECT_HANDLE_KIND_SHIFT) & OBJECT_HANDLE_KIND_MASK);
}

unsigned int kernel_object_register(unsigned int kind,
                                    unsigned int flags,
                                    const unsigned char *name,
                                    unsigned int name_len) {
    unsigned long irq_flags = irq_save();
    if (!object_initialized) {
        clear_objects_unsafe();
        object_initialized = 1;
    }

    for (unsigned int i = 0; i < KERNEL_OBJECT_CAPACITY; i++) {
        if (!objects[i].active) {
            objects[i].active = 1;
            objects[i].kind = kind;
            objects[i].flags = flags | KERNEL_OBJECT_FLAG_ACTIVE;
            objects[i].caps = object_default_caps(kind);
            objects[i].name = name;
            objects[i].name_len = name_len;
            object_count_value++;
            unsigned int id = objects[i].id;
            irq_restore(irq_flags);
            return id;
        }
    }

    irq_restore(irq_flags);
    kernel_panic("kernel-object-registry-full");
    return 0;
}

unsigned int kernel_object_count(void) {
    unsigned long flags = irq_save();
    unsigned int count = object_count_value;
    irq_restore(flags);
    return count;
}

unsigned int kernel_object_capacity(void) {
    return KERNEL_OBJECT_CAPACITY;
}

unsigned int kernel_object_active_count(void) {
    return kernel_object_count();
}

static kernel_object_record *object_at(unsigned int index) {
    if (index >= KERNEL_OBJECT_CAPACITY || !objects[index].active) {
        return 0;
    }
    return &objects[index];
}

unsigned int kernel_object_kind(unsigned int index) {
    unsigned long flags = irq_save();
    kernel_object_record *record = object_at(index);
    unsigned int value = record ? record->kind : 0;
    irq_restore(flags);
    return value;
}

unsigned int kernel_object_flags(unsigned int index) {
    unsigned long flags = irq_save();
    kernel_object_record *record = object_at(index);
    unsigned int value = record ? record->flags : 0;
    irq_restore(flags);
    return value;
}

unsigned int kernel_object_id(unsigned int index) {
    unsigned long flags = irq_save();
    kernel_object_record *record = object_at(index);
    unsigned int value = record ? record->id : 0;
    irq_restore(flags);
    return value;
}

unsigned int kernel_object_caps(unsigned int index) {
    unsigned long flags = irq_save();
    kernel_object_record *record = object_at(index);
    unsigned int value = record ? record->caps : 0;
    irq_restore(flags);
    return value;
}

unsigned int kernel_object_generation(unsigned int index) {
    unsigned long flags = irq_save();
    kernel_object_record *record = object_at(index);
    unsigned int value = record ? record->generation : 0;
    irq_restore(flags);
    return value;
}

unsigned int kernel_object_name_len(unsigned int index) {
    unsigned long flags = irq_save();
    kernel_object_record *record = object_at(index);
    unsigned int value = record ? record->name_len : 0;
    irq_restore(flags);
    return value;
}

unsigned int kernel_object_name_byte(unsigned int index, unsigned int offset) {
    unsigned long flags = irq_save();
    kernel_object_record *record = object_at(index);
    unsigned int value = 0;
    if (record && offset < record->name_len) {
        value = (unsigned int)(unsigned char)record->name[offset];
    }
    irq_restore(flags);
    return value;
}

unsigned long kernel_object_make_handle(unsigned int index, unsigned int caps) {
    unsigned long flags = irq_save();
    kernel_object_record *record = object_at(index);
    if (!record) {
        object_handle_last_error_value = KERNEL_OBJECT_LOOKUP_BAD_HANDLE;
        irq_restore(flags);
        return KERNEL_OBJECT_HANDLE_INVALID;
    }

    unsigned int requested_caps = caps == 0 ? record->caps : caps;
    if (!caps_include(record->caps, requested_caps)) {
        object_handle_last_error_value = KERNEL_OBJECT_LOOKUP_CAP_DENIED;
        irq_restore(flags);
        return KERNEL_OBJECT_HANDLE_INVALID;
    }

    unsigned long handle = encode_object_handle(index,
                                                record->generation,
                                                record->kind,
                                                requested_caps);
    object_handle_last_error_value = KERNEL_OBJECT_LOOKUP_OK;
    irq_restore(flags);
    return handle;
}

unsigned int kernel_object_handle_index(unsigned long handle) {
    unsigned int index = decode_object_handle_slot(handle);
    return index >= KERNEL_OBJECT_CAPACITY ? KERNEL_OBJECT_CAPACITY : index;
}

unsigned int kernel_object_handle_generation(unsigned long handle) {
    return decode_object_handle_generation(handle);
}

unsigned int kernel_object_handle_caps(unsigned long handle) {
    return (unsigned int)(handle >> OBJECT_HANDLE_CAP_SHIFT);
}

unsigned int kernel_object_lookup_id(unsigned long handle, unsigned int required_caps) {
    unsigned long flags = irq_save();
    unsigned int index = decode_object_handle_slot(handle);
    if (handle == KERNEL_OBJECT_HANDLE_INVALID || index >= KERNEL_OBJECT_CAPACITY) {
        object_handle_last_error_value = KERNEL_OBJECT_LOOKUP_BAD_HANDLE;
        irq_restore(flags);
        return 0;
    }

    kernel_object_record *record = &objects[index];
    unsigned int handle_generation = decode_object_handle_generation(handle);
    if (record->generation != handle_generation) {
        object_handle_last_error_value = KERNEL_OBJECT_LOOKUP_STALE;
        irq_restore(flags);
        return 0;
    }
    if (!record->active || record->kind != decode_object_handle_kind(handle)) {
        object_handle_last_error_value = KERNEL_OBJECT_LOOKUP_BAD_HANDLE;
        irq_restore(flags);
        return 0;
    }

    unsigned int handle_caps = kernel_object_handle_caps(handle);
    if (!caps_include(handle_caps, required_caps) ||
        !caps_include(record->caps, required_caps)) {
        object_handle_last_error_value = KERNEL_OBJECT_LOOKUP_CAP_DENIED;
        irq_restore(flags);
        return 0;
    }

    object_handle_last_error_value = KERNEL_OBJECT_LOOKUP_OK;
    unsigned int id = record->id;
    irq_restore(flags);
    return id;
}

unsigned int kernel_object_unregister_handle(unsigned long handle) {
    unsigned int id = kernel_object_lookup_id(handle, KERNEL_OBJECT_CAP_CONTROL);
    if (id == 0) {
        return 0;
    }

    unsigned long flags = irq_save();
    unsigned int index = decode_object_handle_slot(handle);
    if (index >= KERNEL_OBJECT_CAPACITY || !objects[index].active) {
        object_handle_last_error_value = KERNEL_OBJECT_LOOKUP_BAD_HANDLE;
        irq_restore(flags);
        return 0;
    }

    objects[index].active = 0;
    objects[index].kind = 0;
    objects[index].flags = 0;
    objects[index].caps = 0;
    objects[index].generation = next_generation(objects[index].generation);
    objects[index].name = 0;
    objects[index].name_len = 0;
    if (object_count_value > 0) {
        object_count_value--;
    }
    object_handle_last_error_value = KERNEL_OBJECT_LOOKUP_OK;
    irq_restore(flags);
    return 1;
}

unsigned int kernel_object_handle_last_error(void) {
    unsigned long flags = irq_save();
    unsigned int value = object_handle_last_error_value;
    irq_restore(flags);
    return value;
}

int kernel_object_registry_selftest(void) {
    if (!object_initialized) {
        kernel_object_registry_init();
    }
    if (kernel_object_capacity() != KERNEL_OBJECT_CAPACITY) {
        return 0;
    }
    if (kernel_object_count() < 3U) {
        return 0;
    }
    for (unsigned int i = 0; i < kernel_object_capacity(); i++) {
        if (objects[i].active &&
            (kernel_object_id(i) == 0 ||
             kernel_object_name_len(i) == 0 ||
             kernel_object_caps(i) == 0 ||
             kernel_object_generation(i) == 0)) {
            return 0;
        }
    }
    return 1;
}

int kernel_object_handle_selftest(void) {
    if (!object_initialized) {
        kernel_object_registry_init();
    }
    if (kernel_object_count() >= KERNEL_OBJECT_CAPACITY) {
        return 0;
    }

    unsigned int id = kernel_object_register(KERNEL_OBJECT_KIND_DRIVER,
                                            KERNEL_OBJECT_FLAG_ACTIVE,
                                            (const unsigned char *)"handle-selftest",
                                            cstr_len("handle-selftest"));
    if (id == 0) {
        return 0;
    }
    unsigned int index = id - 1U;
    unsigned long handle = kernel_object_make_handle(index,
                                                     KERNEL_OBJECT_CAP_INSPECT |
                                                     KERNEL_OBJECT_CAP_CONTROL);
    if (handle == KERNEL_OBJECT_HANDLE_INVALID) {
        return 0;
    }
    if (kernel_object_lookup_id(handle, KERNEL_OBJECT_CAP_INSPECT) != id ||
        kernel_object_handle_last_error() != KERNEL_OBJECT_LOOKUP_OK) {
        return 0;
    }
    if (!kernel_object_unregister_handle(handle)) {
        return 0;
    }
    if (kernel_object_lookup_id(handle, KERNEL_OBJECT_CAP_INSPECT) != 0 ||
        kernel_object_handle_last_error() != KERNEL_OBJECT_LOOKUP_STALE) {
        return 0;
    }
    return 1;
}

int kernel_object_capcheck_selftest(void) {
    if (!object_initialized) {
        kernel_object_registry_init();
    }
    unsigned long handle = kernel_object_make_handle(0, KERNEL_OBJECT_CAP_INSPECT);
    if (handle == KERNEL_OBJECT_HANDLE_INVALID) {
        return 0;
    }
    if (kernel_object_lookup_id(handle, KERNEL_OBJECT_CAP_INSPECT) == 0 ||
        kernel_object_handle_last_error() != KERNEL_OBJECT_LOOKUP_OK) {
        return 0;
    }
    if (kernel_object_lookup_id(handle, KERNEL_OBJECT_CAP_CONTROL) != 0 ||
        kernel_object_handle_last_error() != KERNEL_OBJECT_LOOKUP_CAP_DENIED) {
        return 0;
    }
    return 1;
}

void kernel_task_registry_init(void) {
    unsigned long flags = irq_save();
    clear_tasks_unsafe();
    task_initialized = 1;
    irq_restore(flags);
}

unsigned int kernel_task_register(unsigned int task_id,
                                  const unsigned char *name,
                                  unsigned int name_len,
                                  unsigned int period_ms) {
    return kernel_task_register_with_parent(task_id,
                                            name,
                                            name_len,
                                            period_ms,
                                            KERNEL_TASK_ROOT_PARENT);
}

unsigned int kernel_task_register_with_parent(unsigned int task_id,
                                              const unsigned char *name,
                                              unsigned int name_len,
                                              unsigned int period_ms,
                                              unsigned int parent_task_id) {
    if (task_id >= KERNEL_TASK_CAPACITY) {
        kernel_panic("kernel-task-registry-bad-id");
    }

    unsigned int object_id = kernel_object_register(KERNEL_OBJECT_KIND_TASK,
                                                    KERNEL_OBJECT_FLAG_ACTIVE,
                                                    name,
                                                    name_len);

    unsigned long flags = irq_save();
    if (!task_initialized) {
        clear_tasks_unsafe();
        task_initialized = 1;
    }
    if (!tasks[task_id].active) {
        task_count_value++;
    }
    tasks[task_id].active = 1;
    tasks[task_id].object_id = object_id;
    tasks[task_id].parent_task_id = parent_task_id;
    tasks[task_id].state = KERNEL_TASK_STATE_IDLE;
    tasks[task_id].period_ms = period_ms;
    tasks[task_id].name = name;
    tasks[task_id].name_len = name_len;
    tasks[task_id].ticks = 0;
    tasks[task_id].spawns = 0;
    tasks[task_id].completions = 0;
    irq_restore(flags);
    return object_id;
}

void kernel_task_set_parent(unsigned int task_id, unsigned int parent_task_id) {
    unsigned long flags = irq_save();
    if (task_id < KERNEL_TASK_CAPACITY && tasks[task_id].active) {
        tasks[task_id].parent_task_id = parent_task_id;
    }
    irq_restore(flags);
}

void kernel_task_mark_state(unsigned int task_id, unsigned int state) {
    unsigned long flags = irq_save();
    if (task_id < KERNEL_TASK_CAPACITY && tasks[task_id].active) {
        tasks[task_id].state = state;
    }
    irq_restore(flags);
}

void kernel_task_record_tick(unsigned int task_id) {
    unsigned long flags = irq_save();
    if (task_id < KERNEL_TASK_CAPACITY && tasks[task_id].active) {
        tasks[task_id].ticks++;
    }
    irq_restore(flags);
}

void kernel_task_record_spawn(unsigned int task_id, unsigned int parent_task_id) {
    unsigned long flags = irq_save();
    if (task_id < KERNEL_TASK_CAPACITY && tasks[task_id].active) {
        tasks[task_id].parent_task_id = parent_task_id;
        tasks[task_id].spawns++;
        tasks[task_id].state = KERNEL_TASK_STATE_WAITING;
    }
    irq_restore(flags);
}

void kernel_task_record_completion(unsigned int task_id) {
    unsigned long flags = irq_save();
    if (task_id < KERNEL_TASK_CAPACITY && tasks[task_id].active) {
        tasks[task_id].completions++;
        tasks[task_id].state = KERNEL_TASK_STATE_IDLE;
    }
    irq_restore(flags);
}

unsigned int kernel_task_count(void) {
    unsigned long flags = irq_save();
    unsigned int count = task_count_value;
    irq_restore(flags);
    return count;
}

unsigned int kernel_task_capacity(void) {
    return KERNEL_TASK_CAPACITY;
}

static kernel_task_record *task_at(unsigned int task_id) {
    if (task_id >= KERNEL_TASK_CAPACITY || !tasks[task_id].active) {
        return 0;
    }
    return &tasks[task_id];
}

unsigned int kernel_task_object_id(unsigned int task_id) {
    unsigned long flags = irq_save();
    kernel_task_record *record = task_at(task_id);
    unsigned int value = record ? record->object_id : 0;
    irq_restore(flags);
    return value;
}

unsigned int kernel_task_parent_id(unsigned int task_id) {
    unsigned long flags = irq_save();
    kernel_task_record *record = task_at(task_id);
    unsigned int value = record ? record->parent_task_id : KERNEL_TASK_ROOT_PARENT;
    irq_restore(flags);
    return value;
}

unsigned long kernel_task_handle(unsigned int task_id) {
    unsigned long flags = irq_save();
    kernel_task_record *record = task_at(task_id);
    unsigned int object_id = record ? record->object_id : 0;
    irq_restore(flags);

    if (object_id == 0) {
        return KERNEL_OBJECT_HANDLE_INVALID;
    }
    return kernel_object_make_handle(object_id - 1U,
                                     KERNEL_OBJECT_CAP_INSPECT |
                                     KERNEL_OBJECT_CAP_SUPERVISE);
}

unsigned int kernel_task_state(unsigned int task_id) {
    unsigned long flags = irq_save();
    kernel_task_record *record = task_at(task_id);
    unsigned int value = record ? record->state : KERNEL_TASK_STATE_IDLE;
    irq_restore(flags);
    return value;
}

unsigned long kernel_task_tick_count(unsigned int task_id) {
    unsigned long flags = irq_save();
    kernel_task_record *record = task_at(task_id);
    unsigned long value = record ? record->ticks : 0;
    irq_restore(flags);
    return value;
}

unsigned long kernel_task_spawn_count(unsigned int task_id) {
    unsigned long flags = irq_save();
    kernel_task_record *record = task_at(task_id);
    unsigned long value = record ? record->spawns : 0;
    irq_restore(flags);
    return value;
}

unsigned long kernel_task_completion_count(unsigned int task_id) {
    unsigned long flags = irq_save();
    kernel_task_record *record = task_at(task_id);
    unsigned long value = record ? record->completions : 0;
    irq_restore(flags);
    return value;
}

unsigned int kernel_task_period_ms(unsigned int task_id) {
    unsigned long flags = irq_save();
    kernel_task_record *record = task_at(task_id);
    unsigned int value = record ? record->period_ms : 0;
    irq_restore(flags);
    return value;
}

unsigned int kernel_task_name_len(unsigned int task_id) {
    unsigned long flags = irq_save();
    kernel_task_record *record = task_at(task_id);
    unsigned int value = record ? record->name_len : 0;
    irq_restore(flags);
    return value;
}

unsigned int kernel_task_name_byte(unsigned int task_id, unsigned int offset) {
    unsigned long flags = irq_save();
    kernel_task_record *record = task_at(task_id);
    unsigned int value = 0;
    if (record && offset < record->name_len) {
        value = (unsigned int)(unsigned char)record->name[offset];
    }
    irq_restore(flags);
    return value;
}

int kernel_task_registry_selftest(void) {
    if (!task_initialized) {
        return 0;
    }
    if (kernel_task_capacity() != KERNEL_TASK_CAPACITY) {
        return 0;
    }
    for (unsigned int i = 0; i < KERNEL_TASK_CAPACITY; i++) {
        if (tasks[i].active &&
            (tasks[i].object_id == 0 ||
             kernel_task_handle(i) == KERNEL_OBJECT_HANDLE_INVALID ||
             tasks[i].name_len == 0)) {
            return 0;
        }
    }
    return 1;
}
