#include "Support.h"

#if KERNEL_PAGE_SIZE != 4096UL
#error "Runtime V7 frame allocator assumes 4 KiB pages"
#endif

#define KERNEL_MEMORY_LOW_LIMIT     0x00070000UL
#define KERNEL_BOOT_STACK_BASE      0x00070000UL
#define KERNEL_BOOT_STACK_LIMIT     0x00080000UL
#define KERNEL_IMAGE_MMU_BASE       0x00080000UL
#define KERNEL_IMAGE_MMU_LIMIT      0x00100000UL
#define KERNEL_SPARE_BASE           0x00100000UL
#define KERNEL_RETAINED_LIMIT       0x00400000UL
#define KERNEL_HEAP_BASE            0x00400000UL
#define KERNEL_HEAP_LIMIT           0x00800000UL

#define KERNEL_FRAME_COUNT ((KERNEL_FRAME_LIMIT - KERNEL_FRAME_BASE) / KERNEL_PAGE_SIZE)
#define FRAME_BITMAP_WORDS ((KERNEL_FRAME_COUNT + 31UL) / 32UL)
#define FRAME_PRESSURE_COUNT 16U

#define KERNEL_REGION_FIRMWARE_LOW     0U
#define KERNEL_REGION_BOOT_STACK       1U
#define KERNEL_REGION_KERNEL_IMAGE_MMU 2U
#define KERNEL_REGION_KERNEL_SPARE     3U
#define KERNEL_REGION_RETAINED         4U
#define KERNEL_REGION_HEAP             5U
#define KERNEL_REGION_FRAMES           6U
#define KERNEL_REGION_COUNT            7U

typedef struct kernel_memory_region {
    const char *name;
    unsigned long start;
    unsigned long end;
    unsigned int kind;
} kernel_memory_region;

static const kernel_memory_region regions[KERNEL_REGION_COUNT] = {
    {"firmware_low", 0x00000000UL, KERNEL_MEMORY_LOW_LIMIT, KERNEL_MEMORY_REGION_KIND_RESERVED},
    {"boot_stack", KERNEL_BOOT_STACK_BASE, KERNEL_BOOT_STACK_LIMIT, KERNEL_MEMORY_REGION_KIND_RESERVED},
    {"kernel_image_mmu", KERNEL_IMAGE_MMU_BASE, KERNEL_IMAGE_MMU_LIMIT, KERNEL_MEMORY_REGION_KIND_RESERVED},
    {"kernel_spare", KERNEL_SPARE_BASE, KERNEL_RETAINED_RECORD_ADDR, KERNEL_MEMORY_REGION_KIND_RESERVED},
    {"retained", KERNEL_RETAINED_RECORD_ADDR, KERNEL_RETAINED_LIMIT, KERNEL_MEMORY_REGION_KIND_RESERVED},
    {"heap", KERNEL_HEAP_BASE, KERNEL_HEAP_LIMIT, KERNEL_MEMORY_REGION_KIND_HEAP},
    {"frames", KERNEL_FRAME_BASE, KERNEL_FRAME_LIMIT, KERNEL_MEMORY_REGION_KIND_FRAMES},
};

static unsigned int frame_bitmap[FRAME_BITMAP_WORDS];
static unsigned long frame_used;
static unsigned int memory_initialized;
static unsigned int memory_map_valid;
static unsigned int memory_last_error;
static unsigned int frame_last_error;
static unsigned long frame_bad_frees;
static unsigned long frame_double_frees;
static unsigned long frame_pressure_last_peak;
static unsigned long frame_pressure_last_leak;
static unsigned int frame_guard_probe_last_ok;

static unsigned int frame_bit(unsigned long index) {
    return 1U << (unsigned int)(index & 31UL);
}

static unsigned int frame_word(unsigned long index) {
    return (unsigned int)(index >> 5);
}

static int frame_is_used(unsigned long index) {
    return (frame_bitmap[frame_word(index)] & frame_bit(index)) != 0;
}

static void frame_mark_used(unsigned long index) {
    frame_bitmap[frame_word(index)] |= frame_bit(index);
}

static void frame_mark_free(unsigned long index) {
    frame_bitmap[frame_word(index)] &= ~frame_bit(index);
}

static void frame_record_error(unsigned int error) {
    frame_last_error = error;
}

static void frame_pressure_record(unsigned long peak, unsigned long leak) {
    unsigned long flags = irq_save();
    frame_pressure_last_peak = peak;
    frame_pressure_last_leak = leak;
    irq_restore(flags);
}

static void frame_guard_probe_record(unsigned int ok) {
    unsigned long flags = irq_save();
    frame_guard_probe_last_ok = ok;
    irq_restore(flags);
}

static int page_aligned(unsigned long value) {
    return (value & (KERNEL_PAGE_SIZE - 1UL)) == 0;
}

static unsigned long region_bytes(const kernel_memory_region *r) {
    return r->end - r->start;
}

static unsigned int kernel_memory_check_invariants(void) {
    unsigned long previous_end = 0;

    for (unsigned int i = 0; i < KERNEL_REGION_COUNT; i++) {
        const kernel_memory_region *r = &regions[i];
        if (r->start >= r->end) {
            memory_last_error = 1;
            return 0;
        }
        if (!page_aligned(r->start) || !page_aligned(r->end)) {
            memory_last_error = 2;
            return 0;
        }
        if (i > 0 && r->start < previous_end) {
            memory_last_error = 3;
            return 0;
        }
        previous_end = r->end;
    }

    if (regions[KERNEL_REGION_RETAINED].start != KERNEL_RETAINED_RECORD_ADDR) {
        memory_last_error = 4;
        return 0;
    }
    if (regions[KERNEL_REGION_HEAP].start != KERNEL_HEAP_BASE ||
        regions[KERNEL_REGION_HEAP].end != KERNEL_HEAP_LIMIT) {
        memory_last_error = 5;
        return 0;
    }
    if (regions[KERNEL_REGION_FRAMES].start != KERNEL_FRAME_BASE ||
        regions[KERNEL_REGION_FRAMES].end != KERNEL_FRAME_LIMIT) {
        memory_last_error = 6;
        return 0;
    }

    memory_last_error = 0;
    return 1;
}

static void frame_bitmap_init(void) {
    for (unsigned int i = 0; i < FRAME_BITMAP_WORDS; i++) {
        frame_bitmap[i] = 0;
    }
    frame_used = 0;
}

void kernel_memory_init(void) {
    if (memory_initialized) {
        return;
    }

    frame_bitmap_init();
    memory_initialized = 1;
    memory_map_valid = kernel_memory_check_invariants();
    if (!memory_map_valid) {
        kernel_panic("memory-map-overlap");
    }
}

unsigned int kernel_memory_region_count(void) {
    return KERNEL_REGION_COUNT;
}

unsigned long kernel_memory_region_start(unsigned int index) {
    if (index >= KERNEL_REGION_COUNT) {
        return 0;
    }
    return regions[index].start;
}

unsigned long kernel_memory_region_end(unsigned int index) {
    if (index >= KERNEL_REGION_COUNT) {
        return 0;
    }
    return regions[index].end;
}

unsigned int kernel_memory_region_kind(unsigned int index) {
    if (index >= KERNEL_REGION_COUNT) {
        return 0;
    }
    return regions[index].kind;
}

unsigned int kernel_memory_region_name_len(unsigned int index) {
    if (index >= KERNEL_REGION_COUNT) {
        return 0;
    }

    const char *name = regions[index].name;
    unsigned int n = 0;
    while (name[n] != '\0') {
        n++;
    }
    return n;
}

unsigned int kernel_memory_region_name_byte(unsigned int index, unsigned int offset) {
    if (index >= KERNEL_REGION_COUNT) {
        return 0;
    }
    if (offset >= kernel_memory_region_name_len(index)) {
        return 0;
    }
    return (unsigned int)(unsigned char)regions[index].name[offset];
}

unsigned long kernel_memory_reserved_bytes(void) {
    unsigned long total = 0;
    for (unsigned int i = 0; i < KERNEL_REGION_COUNT; i++) {
        if (regions[i].kind != KERNEL_MEMORY_REGION_KIND_FRAMES) {
            total += region_bytes(&regions[i]);
        }
    }
    return total;
}

unsigned int kernel_memory_map_valid(void) {
    if (!memory_initialized) {
        kernel_memory_init();
    }
    return memory_map_valid;
}

unsigned int kernel_memory_last_error(void) {
    return memory_last_error;
}

unsigned long kernel_frame_base(void) {
    return KERNEL_FRAME_BASE;
}

unsigned long kernel_frame_limit(void) {
    return KERNEL_FRAME_LIMIT;
}

unsigned long kernel_frame_total_count(void) {
    return KERNEL_FRAME_COUNT;
}

unsigned long kernel_frame_free_count(void) {
    unsigned long flags = irq_save();
    if (!memory_initialized) {
        kernel_memory_init();
    }
    unsigned long count = KERNEL_FRAME_COUNT - frame_used;
    irq_restore(flags);
    return count;
}

unsigned long kernel_frame_used_count(void) {
    unsigned long flags = irq_save();
    if (!memory_initialized) {
        kernel_memory_init();
    }
    unsigned long count = frame_used;
    irq_restore(flags);
    return count;
}

unsigned long kernel_frame_reserved_count(void) {
    return 0;
}

unsigned long kernel_frame_alloc(void) {
    unsigned long flags = irq_save();
    if (!memory_initialized) {
        kernel_memory_init();
    }

    for (unsigned long i = 0; i < KERNEL_FRAME_COUNT; i++) {
        if (!frame_is_used(i)) {
            frame_mark_used(i);
            frame_used++;
            frame_record_error(KERNEL_FRAME_ERROR_NONE);
            irq_restore(flags);
            return KERNEL_FRAME_BASE + (i * KERNEL_PAGE_SIZE);
        }
    }

    frame_record_error(KERNEL_FRAME_ERROR_EXHAUSTED);
    irq_restore(flags);
    return 0;
}

int kernel_frame_free(unsigned long address) {
    if (address < KERNEL_FRAME_BASE || address >= KERNEL_FRAME_LIMIT) {
        unsigned long flags = irq_save();
        frame_bad_frees++;
        frame_record_error(KERNEL_FRAME_ERROR_BAD_FREE);
        irq_restore(flags);
        return 0;
    }
    if (!page_aligned(address)) {
        unsigned long flags = irq_save();
        frame_bad_frees++;
        frame_record_error(KERNEL_FRAME_ERROR_BAD_FREE);
        irq_restore(flags);
        return 0;
    }

    unsigned long index = (address - KERNEL_FRAME_BASE) / KERNEL_PAGE_SIZE;
    unsigned long flags = irq_save();
    if (!memory_initialized) {
        kernel_memory_init();
    }
    if (!frame_is_used(index)) {
        frame_double_frees++;
        frame_record_error(KERNEL_FRAME_ERROR_DOUBLE_FREE);
        irq_restore(flags);
        return 0;
    }

    frame_mark_free(index);
    frame_used--;
    frame_record_error(KERNEL_FRAME_ERROR_NONE);
    irq_restore(flags);
    return 1;
}

int kernel_frame_allocator_selftest(void) {
    unsigned long frame = kernel_frame_alloc();
    if (frame == 0) {
        return 0;
    }
    if (frame < KERNEL_FRAME_BASE || frame >= KERNEL_FRAME_LIMIT) {
        return 0;
    }
    if (!page_aligned(frame)) {
        return 0;
    }
    return kernel_frame_free(frame);
}

unsigned int kernel_frame_last_error(void) {
    unsigned long flags = irq_save();
    unsigned int error = frame_last_error;
    irq_restore(flags);
    return error;
}

unsigned long kernel_frame_bad_free_count(void) {
    unsigned long flags = irq_save();
    unsigned long count = frame_bad_frees;
    irq_restore(flags);
    return count;
}

unsigned long kernel_frame_double_free_count(void) {
    unsigned long flags = irq_save();
    unsigned long count = frame_double_frees;
    irq_restore(flags);
    return count;
}

int kernel_frame_allocator_stress_selftest(void) {
    unsigned long before_free = kernel_frame_free_count();
    unsigned long before_used = kernel_frame_used_count();
    unsigned long frames[4] = {0, 0, 0, 0};

    for (unsigned int i = 0; i < 4; i++) {
        frames[i] = kernel_frame_alloc();
        if (frames[i] == 0 || !page_aligned(frames[i])) {
            for (unsigned int j = 0; j < i; j++) {
                (void)kernel_frame_free(frames[j]);
            }
            return 0;
        }
        for (unsigned int j = 0; j < i; j++) {
            if (frames[i] == frames[j]) {
                for (unsigned int k = 0; k <= i; k++) {
                    (void)kernel_frame_free(frames[k]);
                }
                return 0;
            }
        }
    }

    for (unsigned int i = 0; i < 4; i++) {
        if (!kernel_frame_free(frames[i])) {
            return 0;
        }
    }

    return kernel_frame_free_count() == before_free && kernel_frame_used_count() == before_used;
}

int kernel_frame_pressure_selftest(void) {
    const unsigned int free_order[FRAME_PRESSURE_COUNT] = {
        15, 0, 14, 1, 13, 2, 12, 3, 11, 4, 10, 5, 9, 6, 8, 7
    };
    unsigned long frames[FRAME_PRESSURE_COUNT] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };
    unsigned long before_free = kernel_frame_free_count();
    unsigned long before_used = kernel_frame_used_count();
    unsigned long peak = 0;

    for (unsigned int i = 0; i < FRAME_PRESSURE_COUNT; i++) {
        frames[i] = kernel_frame_alloc();
        if (frames[i] == 0 || !page_aligned(frames[i])) {
            for (unsigned int j = 0; j < i; j++) {
                if (frames[j] != 0) {
                    (void)kernel_frame_free(frames[j]);
                    frames[j] = 0;
                }
            }
            unsigned long after_free_failed = kernel_frame_free_count();
            unsigned long leak_failed = before_free > after_free_failed ? before_free - after_free_failed : 0;
            frame_pressure_record(peak, leak_failed);
            return 0;
        }
        for (unsigned int j = 0; j < i; j++) {
            if (frames[i] == frames[j]) {
                for (unsigned int k = 0; k <= i; k++) {
                    if (frames[k] != 0) {
                        (void)kernel_frame_free(frames[k]);
                        frames[k] = 0;
                    }
                }
                unsigned long after_free_dup = kernel_frame_free_count();
                unsigned long leak_dup = before_free > after_free_dup ? before_free - after_free_dup : 0;
                frame_pressure_record(peak, leak_dup);
                return 0;
            }
        }

        unsigned long used = kernel_frame_used_count();
        unsigned long delta = used > before_used ? used - before_used : 0;
        if (delta > peak) {
            peak = delta;
        }
    }

    for (unsigned int i = 0; i < FRAME_PRESSURE_COUNT; i++) {
        unsigned int index = free_order[i];
        if (!kernel_frame_free(frames[index])) {
            for (unsigned int j = 0; j < FRAME_PRESSURE_COUNT; j++) {
                if (frames[j] != 0) {
                    (void)kernel_frame_free(frames[j]);
                    frames[j] = 0;
                }
            }
            unsigned long after_free_bad = kernel_frame_free_count();
            unsigned long leak_bad = before_free > after_free_bad ? before_free - after_free_bad : 0;
            frame_pressure_record(peak, leak_bad);
            return 0;
        }
        frames[index] = 0;
    }

    unsigned long after_free = kernel_frame_free_count();
    unsigned long after_used = kernel_frame_used_count();
    unsigned long leak = 0;
    if (before_free > after_free) {
        leak = before_free - after_free;
    } else if (after_used > before_used) {
        leak = after_used - before_used;
    }

    frame_pressure_record(peak, leak);
    return after_free == before_free && after_used == before_used;
}

unsigned long kernel_frame_pressure_last_peak_count(void) {
    unsigned long flags = irq_save();
    unsigned long peak = frame_pressure_last_peak;
    irq_restore(flags);
    return peak;
}

unsigned long kernel_frame_pressure_last_leak_count(void) {
    unsigned long flags = irq_save();
    unsigned long leak = frame_pressure_last_leak;
    irq_restore(flags);
    return leak;
}

int kernel_frame_guard_probe_selftest(void) {
    unsigned long before_free = kernel_frame_free_count();
    unsigned long before_used = kernel_frame_used_count();
    unsigned long before_bad = kernel_frame_bad_free_count();
    unsigned long before_double = kernel_frame_double_free_count();

    int bad_free_rejected = kernel_frame_free(KERNEL_FRAME_BASE - KERNEL_PAGE_SIZE) == 0;

    unsigned long frame = kernel_frame_alloc();
    if (frame == 0) {
        frame_guard_probe_record(0);
        return 0;
    }

    int first_free_ok = kernel_frame_free(frame) != 0;
    int double_free_rejected = kernel_frame_free(frame) == 0;

    unsigned long after_free = kernel_frame_free_count();
    unsigned long after_used = kernel_frame_used_count();
    unsigned long after_bad = kernel_frame_bad_free_count();
    unsigned long after_double = kernel_frame_double_free_count();

    unsigned int ok = bad_free_rejected &&
        first_free_ok &&
        double_free_rejected &&
        after_bad == before_bad + 1 &&
        after_double == before_double + 1 &&
        after_free == before_free &&
        after_used == before_used &&
        kernel_frame_last_error() == KERNEL_FRAME_ERROR_DOUBLE_FREE;
    frame_guard_probe_record(ok);
    return (int)ok;
}

unsigned int kernel_frame_guard_probe_last_ok(void) {
    unsigned long flags = irq_save();
    unsigned int ok = frame_guard_probe_last_ok;
    irq_restore(flags);
    return ok;
}
