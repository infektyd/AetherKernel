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
    const unsigned char *name;
    unsigned int name_len;
} kernel_object_record;

typedef struct kernel_task_record {
    unsigned int active;
    unsigned int object_id;
    unsigned int state;
    unsigned int period_ms;
    const unsigned char *name;
    unsigned int name_len;
    unsigned long ticks;
} kernel_task_record;

static kernel_object_record objects[KERNEL_OBJECT_CAPACITY];
static kernel_task_record tasks[KERNEL_TASK_CAPACITY];
static unsigned int object_initialized;
static unsigned int task_initialized;
static unsigned int object_count_value;
static unsigned int task_count_value;

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
        objects[i].name = 0;
        objects[i].name_len = 0;
    }
    object_count_value = 0;
}

static void clear_tasks_unsafe(void) {
    for (unsigned int i = 0; i < KERNEL_TASK_CAPACITY; i++) {
        tasks[i].active = 0;
        tasks[i].object_id = 0;
        tasks[i].state = KERNEL_TASK_STATE_IDLE;
        tasks[i].period_ms = 0;
        tasks[i].name = 0;
        tasks[i].name_len = 0;
        tasks[i].ticks = 0;
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
    for (unsigned int i = 0; i < kernel_object_count(); i++) {
        if (kernel_object_id(i) == 0 || kernel_object_name_len(i) == 0) {
            return 0;
        }
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
    tasks[task_id].state = KERNEL_TASK_STATE_IDLE;
    tasks[task_id].period_ms = period_ms;
    tasks[task_id].name = name;
    tasks[task_id].name_len = name_len;
    tasks[task_id].ticks = 0;
    irq_restore(flags);
    return object_id;
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
            (tasks[i].object_id == 0 || tasks[i].name_len == 0)) {
            return 0;
        }
    }
    return 1;
}
