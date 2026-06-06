#include "Support.h"

//===----------------------------------------------------------------------===//
// Runtime V32 SMP secondary-core bring-up substrate.
// Runtime V35 secondary scheduler worker loop.
//
// Secondary cores deliberately stay in this C-only accounting loop for V32. The
// goal is hardware proof that the other Cortex-A72 cores reached EL1 with
// private stacks, MMU on, coherent shared memory, and stable per-core records.
//===----------------------------------------------------------------------===//

typedef struct smp_core_record {
    volatile unsigned int online;
    volatile unsigned long mpidr;
    volatile unsigned long entry_count;
    volatile unsigned long heartbeat;
} smp_core_record;

static smp_core_record cores[KERNEL_SMP_CORE_CAPACITY];
static volatile unsigned int initialized;
static volatile unsigned int primary_core_id;

static unsigned int core_from_mpidr(unsigned long mpidr) {
    return (unsigned int)(mpidr & 0x3UL);
}

static int valid_core(unsigned int core_id) {
    return core_id < KERNEL_SMP_CORE_CAPACITY;
}

static void memory_barrier(void) {
    __asm__ volatile("dmb ish" ::: "memory");
}

void kernel_smp_init(void) {
    for (unsigned int i = 0; i < KERNEL_SMP_CORE_CAPACITY; i++) {
        cores[i].online = 0;
        cores[i].mpidr = 0;
        cores[i].entry_count = 0;
        cores[i].heartbeat = 0;
    }
    primary_core_id = 0;
    memory_barrier();
    initialized = 1;
    memory_barrier();
}

void kernel_smp_note_primary(unsigned long mpidr) {
    if (!initialized) {
        kernel_smp_init();
    }
    unsigned int core_id = core_from_mpidr(mpidr);
    if (!valid_core(core_id)) {
        core_id = 0;
    }
    primary_core_id = core_id;
    cores[core_id].mpidr = mpidr;
    cores[core_id].entry_count++;
    cores[core_id].heartbeat++;
    memory_barrier();
    cores[core_id].online = 1;
    memory_barrier();
}

void kernel_smp_secondary_entry(unsigned int core_id, unsigned long mpidr) {
    if (!initialized) {
        kernel_smp_init();
    }
    if (!valid_core(core_id)) {
        for (;;) {
            __asm__ volatile("wfe" ::: "memory");
        }
    }

    cores[core_id].mpidr = mpidr;
    cores[core_id].entry_count++;
    cores[core_id].heartbeat++;
    memory_barrier();
    cores[core_id].online = 1;
    memory_barrier();

    for (;;) {
        kernel_scheduler_secondary_worker_tick(core_id);
        cores[core_id].heartbeat++;
        for (volatile unsigned int spin = 0; spin < 4096U; spin++) {
            __asm__ volatile("nop");
        }
    }
}

unsigned int kernel_smp_core_capacity(void) {
    return KERNEL_SMP_CORE_CAPACITY;
}

unsigned int kernel_smp_online_count(void) {
    unsigned int count = 0;
    for (unsigned int i = 0; i < KERNEL_SMP_CORE_CAPACITY; i++) {
        if (cores[i].online != 0) {
            count++;
        }
    }
    return count;
}

unsigned int kernel_smp_online_mask(void) {
    unsigned int mask = 0;
    for (unsigned int i = 0; i < KERNEL_SMP_CORE_CAPACITY; i++) {
        if (cores[i].online != 0) {
            mask |= (1U << i);
        }
    }
    return mask;
}

unsigned int kernel_smp_primary_core_id(void) {
    return primary_core_id;
}

unsigned int kernel_smp_core_online(unsigned int core_id) {
    if (!valid_core(core_id)) {
        return 0;
    }
    return cores[core_id].online;
}

unsigned long kernel_smp_core_mpidr(unsigned int core_id) {
    if (!valid_core(core_id)) {
        return 0;
    }
    return cores[core_id].mpidr;
}

unsigned long kernel_smp_core_entry_count(unsigned int core_id) {
    if (!valid_core(core_id)) {
        return 0;
    }
    return cores[core_id].entry_count;
}

unsigned long kernel_smp_core_heartbeat(unsigned int core_id) {
    if (!valid_core(core_id)) {
        return 0;
    }
    return cores[core_id].heartbeat;
}

unsigned int kernel_smp_release_map(void) {
    return KERNEL_SMP_SECONDARY_MASK;
}

int kernel_smp_selftest(void) {
    if (!initialized) {
        return 0;
    }
    if (KERNEL_SMP_VERSION != 32U) {
        return 0;
    }
    if (kernel_smp_core_capacity() != KERNEL_SMP_CORE_CAPACITY) {
        return 0;
    }
    if (kernel_smp_release_map() != KERNEL_SMP_SECONDARY_MASK) {
        return 0;
    }
    if (kernel_smp_primary_core_id() != 0) {
        return 0;
    }
    if (kernel_smp_online_count() != KERNEL_SMP_CORE_CAPACITY) {
        return 0;
    }
    if (kernel_smp_online_mask() != 0xfU) {
        return 0;
    }
    for (unsigned int i = 0; i < KERNEL_SMP_CORE_CAPACITY; i++) {
        if (kernel_smp_core_online(i) == 0) {
            return 0;
        }
        if (kernel_smp_core_entry_count(i) == 0) {
            return 0;
        }
        if (kernel_smp_core_heartbeat(i) == 0) {
            return 0;
        }
    }
    return 1;
}
