#include "Support.h"

#define HEAP_BASE 0x400000UL
#define HEAP_SIZE 0x400000UL
#define HEAP_END  (HEAP_BASE + HEAP_SIZE)
#define HEAP_PAYLOAD_SIZE ((HEAP_END - 8) - (HEAP_BASE + 8) - 16)
#define HEAP_FREE_POISON 0xA5U

#define EINVAL 22
#define ENOMEM 12
#define NULL ((void *)0)

typedef unsigned long size_t;

typedef struct block_header {
    size_t size; // Size of payload. Low bit: 1 = allocated, 0 = free.
} block_header;

typedef struct free_block {
    size_t size; // Header containing size and low bit (free)
    struct free_block *next;
} free_block;

static free_block *free_list_head = NULL;
static int heap_initialized = 0;
static unsigned long malloc_calls = 0;
static unsigned long free_calls = 0;
static unsigned long realloc_calls = 0;
static unsigned long calloc_calls = 0;
static unsigned long heap_high_water = 0;
static unsigned long heap_failed_allocs = 0;
static unsigned int heap_last_error = HEAP_GUARD_OK;
static unsigned long heap_invalid_frees = 0;
static unsigned long heap_double_frees = 0;
static unsigned long heap_corruptions = 0;

static void heap_record_guard_error(unsigned int reason) {
    heap_last_error = reason;
}

static void heap_record_corruption(unsigned int reason) {
    heap_last_error = reason;
    heap_corruptions++;
}

static void heap_init(void) {
    // Left sentinel: at HEAP_BASE (8 bytes), value = 1 (allocated, payload 0)
    *(size_t *)HEAP_BASE = 1UL;

    // Right sentinel: at HEAP_END - 8 (8 bytes), value = 1 (allocated, payload 0)
    *(size_t *)(HEAP_END - 8) = 1UL;

    // Main initial free block:
    // Header is at HEAP_BASE + 8 (0x400008)
    free_block *first = (free_block *)(HEAP_BASE + 8);
    // Payload spans from this header's payload start to just before the right
    // sentinel, minus this block's own header+footer (16). Computed (not a hand
    // literal) so the initial block's footer butts exactly against the right
    // sentinel — otherwise end-of-heap coalescing reads the gap as a fake block.
    // = (HEAP_END-8) - (HEAP_BASE+8) - 16 = 0x3FFFE0.
    size_t payload_size = HEAP_PAYLOAD_SIZE;
    first->size = payload_size; // Low bit is 0 (free)
    first->next = NULL;

    // Footer is at HEAP_BASE + 8 + 8 + payload_size = HEAP_END - 16 (0x7FFFF0)
    size_t *footer = (size_t *)((char *)first + 8 + payload_size);
    *footer = payload_size;

    free_list_head = first;
    heap_initialized = 1;
}

static void remove_from_free_list(free_block *b) {
    free_block *curr = free_list_head;
    free_block *prev = NULL;
    while (curr != NULL) {
        if (curr == b) {
            if (prev == NULL) {
                free_list_head = curr->next;
            } else {
                prev->next = curr->next;
            }
            return;
        }
        prev = curr;
        curr = curr->next;
    }
}

static void insert_into_free_list(free_block *b) {
    b->next = free_list_head;
    free_list_head = b;
}

static unsigned long heap_free_bytes_unsafe(void) {
    unsigned long total = 0;
    free_block *curr = free_list_head;
    while (curr != NULL) {
        total += curr->size & ~1UL;
        curr = curr->next;
    }
    return total;
}

static unsigned long heap_allocated_bytes_unsafe(void) {
    unsigned long free_bytes = heap_free_bytes_unsafe();
    if (free_bytes >= HEAP_PAYLOAD_SIZE) {
        return 0;
    }
    return HEAP_PAYLOAD_SIZE - free_bytes;
}

static void heap_update_high_water_unsafe(void) {
    unsigned long allocated = heap_allocated_bytes_unsafe();
    if (allocated > heap_high_water) {
        heap_high_water = allocated;
    }
}

static int heap_header_in_range(block_header *H) {
    unsigned long addr = (unsigned long)H;
    if (addr < HEAP_BASE + 8 || addr >= HEAP_END - 8) {
        return 0;
    }
    return ((addr - (HEAP_BASE + 8)) & 0xFUL) == 0;
}

static int heap_payload_in_range(void *ptr) {
    unsigned long addr = (unsigned long)ptr;
    if (addr < HEAP_BASE + 16 || addr >= HEAP_END - 8) {
        return 0;
    }
    return (addr & 0xFUL) == 0;
}

static void heap_poison_payload(block_header *H, size_t S) {
    unsigned char *p = (unsigned char *)H + 8;
    for (size_t i = 0; i < S; i++) {
        p[i] = HEAP_FREE_POISON;
    }
}

static void heap_panic_invalid_free(void) {
    heap_invalid_frees++;
    kernel_panic("heap-invalid-free");
}

static void heap_panic_double_free(void) {
    heap_double_frees++;
    kernel_panic("heap-double-free");
}

static int heap_validate_allocated_header_unsafe(block_header *H, size_t *out_size) {
    if (!heap_header_in_range(H)) {
        heap_record_guard_error(HEAP_GUARD_INVALID_FREE);
        return 0;
    }

    size_t raw = H->size;
    size_t size = raw & ~1UL;
    if ((raw & 1UL) == 0) {
        heap_record_guard_error(HEAP_GUARD_DOUBLE_FREE);
        return 0;
    }
    if (size == 0 || (size & 0xFUL) != 0) {
        heap_record_guard_error(HEAP_GUARD_BLOCK_SIZE);
        return 0;
    }

    size_t *footer = (size_t *)((char *)H + 8 + size);
    if ((unsigned long)(footer + 1) > HEAP_END - 8) {
        heap_record_guard_error(HEAP_GUARD_BLOCK_SIZE);
        return 0;
    }
    if (*footer != raw) {
        heap_record_guard_error(HEAP_GUARD_BLOCK_FOOTER);
        return 0;
    }

    *out_size = size;
    return 1;
}

static int heap_validate_allocation_unsafe(void *ptr, block_header **out_header, size_t *out_size) {
    if (!heap_payload_in_range(ptr)) {
        heap_record_guard_error(HEAP_GUARD_INVALID_FREE);
        return 0;
    }

    size_t val = *((size_t *)ptr - 1);
    block_header *direct = (block_header *)((char *)ptr - 8);
    block_header *H = NULL;

    if (heap_header_in_range(direct) && direct->size == val && (val & ~1UL) <= HEAP_PAYLOAD_SIZE) {
        if ((val & 1UL) == 0) {
            heap_record_guard_error(HEAP_GUARD_DOUBLE_FREE);
            return 0;
        }
        H = direct;
    } else if ((val & 1UL) == 0) {
        H = (block_header *)val;
        if (!heap_header_in_range(H)) {
            heap_record_guard_error(HEAP_GUARD_BACKPTR);
            return 0;
        }
    } else {
        heap_record_guard_error(HEAP_GUARD_INVALID_FREE);
        return 0;
    }

    if (!heap_validate_allocated_header_unsafe(H, out_size)) {
        return 0;
    }

    unsigned long payload_start = (unsigned long)H + 8;
    unsigned long payload_end = payload_start + *out_size;
    unsigned long ptr_addr = (unsigned long)ptr;
    if (ptr_addr < payload_start || ptr_addr >= payload_end) {
        heap_record_guard_error(HEAP_GUARD_BACKPTR);
        return 0;
    }

    *out_header = H;
    return 1;
}

void *malloc(size_t size) {
    unsigned long flags = irq_save();
    malloc_calls++;

    if (!heap_initialized) {
        heap_init();
    }

    if (size == 0) {
        size = 16;
    }

    // Round up size to a multiple of 16 for alignment
    size = (size + 15) & ~15UL;

    free_block *curr = free_list_head;
    free_block *prev = NULL;
    free_block *found = NULL;
    free_block *found_prev = NULL;

    while (curr != NULL) {
        size_t curr_size = curr->size & ~1UL;
        if (curr_size >= size) {
            found = curr;
            found_prev = prev;
            break;
        }
        prev = curr;
        curr = curr->next;
    }

    if (found == NULL) {
        heap_failed_allocs++;
        irq_restore(flags);
        return NULL;
    }

    size_t total_size = found->size & ~1UL;

    // Split block if remaining space is at least 32 bytes (header + minimum payload + footer)
    if (total_size - size >= 32) {
        // Allocate the block at the beginning
        found->size = size | 1UL;
        size_t *alloc_footer = (size_t *)((char *)found + 8 + size);
        *alloc_footer = size | 1UL;

        // Remaining free block
        free_block *rem = (free_block *)((char *)found + 16 + size);
        size_t rem_size = total_size - size - 16;
        rem->size = rem_size; // low bit 0 (free)
        size_t *rem_footer = (size_t *)((char *)rem + 8 + rem_size);
        *rem_footer = rem_size;

        // Replace found with rem in the free list
        if (found_prev == NULL) {
            free_list_head = rem;
        } else {
            found_prev->next = rem;
        }
        rem->next = found->next;
    } else {
        // Allocate the entire block
        found->size = total_size | 1UL;
        size_t *alloc_footer = (size_t *)((char *)found + 8 + total_size);
        *alloc_footer = total_size | 1UL;

        // Remove found from the free list
        if (found_prev == NULL) {
            free_list_head = found->next;
        } else {
            found_prev->next = found->next;
        }
    }

    heap_update_high_water_unsafe();
    irq_restore(flags);
    return (void *)((char *)found + 8);
}

void free(void *ptr) {
    if (ptr == NULL) {
        return;
    }

    unsigned long flags = irq_save();
    free_calls++;

    if (!heap_initialized) {
        heap_init();
    }

    block_header *H = NULL;
    size_t S = 0;
    if (!heap_validate_allocation_unsafe(ptr, &H, &S)) {
        unsigned int reason = heap_last_error;
        if (reason == HEAP_GUARD_DOUBLE_FREE) {
            heap_panic_double_free();
        } else {
            heap_panic_invalid_free();
        }
        irq_restore(flags);
        return;
    }

    // Next block
    block_header *next_H = (block_header *)((char *)H + 16 + S);
    int next_free = !(next_H->size & 1UL);
    size_t next_S = next_H->size & ~1UL;

    // Previous block
    size_t prev_footer_val = *((size_t *)H - 1);
    int prev_free = !(prev_footer_val & 1UL);
    size_t prev_S = prev_footer_val;

    heap_poison_payload(H, S);

    if (prev_free && next_free) {
        remove_from_free_list((free_block *)next_H);

        block_header *prev_H = (block_header *)((char *)H - 16 - prev_S);
        size_t new_S = prev_S + 16 + S + 16 + next_S;

        prev_H->size = new_S;
        size_t *footer = (size_t *)((char *)next_H + 8 + next_S);
        *footer = new_S;

    } else if (prev_free) {
        block_header *prev_H = (block_header *)((char *)H - 16 - prev_S);
        size_t new_S = prev_S + 16 + S;

        prev_H->size = new_S;
        size_t *footer = (size_t *)((char *)H + 8 + S);
        *footer = new_S;

    } else if (next_free) {
        remove_from_free_list((free_block *)next_H);

        size_t new_S = S + 16 + next_S;
        H->size = new_S;

        size_t *footer = (size_t *)((char *)next_H + 8 + next_S);
        *footer = new_S;

        insert_into_free_list((free_block *)H);

    } else {
        H->size = S;
        size_t *footer = (size_t *)((char *)H + 8 + S);
        *footer = S;

        insert_into_free_list((free_block *)H);
    }

    irq_restore(flags);
}

int posix_memalign(void **memptr, size_t alignment, size_t size) {
    if (memptr == NULL) {
        return EINVAL;
    }

    if (alignment < sizeof(void *) || (alignment & (alignment - 1)) != 0) {
        return EINVAL;
    }

    // Over-allocate by alignment + one word: we both shift the payload up to an
    // alignment boundary AND store an 8-byte back-pointer at p-8. `size+alignment`
    // alone can run up to ~8 bytes past the block (corrupting the next header).
    void *base = malloc(size + alignment + sizeof(void *));
    if (base == NULL) {
        return ENOMEM;
    }

    size_t base_addr = (size_t)base;
    size_t p_addr = (base_addr + 8 + (alignment - 1)) & ~(alignment - 1);

    void **back_ptr_loc = (void **)(p_addr - 8);
    *back_ptr_loc = (void *)(base_addr - 8);

    *memptr = (void *)p_addr;
    return 0;
}

void *calloc(size_t nmemb, size_t size) {
    unsigned long flags = irq_save();
    calloc_calls++;
    irq_restore(flags);

    if (nmemb == 0 || size == 0) {
        return NULL;
    }

    size_t total = nmemb * size;
    if (total / nmemb != size) {
        flags = irq_save();
        heap_failed_allocs++;
        irq_restore(flags);
        return NULL; // Overflow
    }

    void *ptr = malloc(total);
    if (ptr == NULL) {
        return NULL;
    }

    char *p = (char *)ptr;
    for (size_t i = 0; i < total; i++) {
        p[i] = 0;
    }
    return ptr;
}

void *realloc(void *ptr, size_t size) {
    unsigned long flags = irq_save();
    realloc_calls++;
    irq_restore(flags);

    if (ptr == NULL) {
        return malloc(size);
    }
    if (size == 0) {
        free(ptr);
        return NULL;
    }

    flags = irq_save();
    if (!heap_initialized) {
        heap_init();
    }
    block_header *H = NULL;
    size_t old_size = 0;
    if (!heap_validate_allocation_unsafe(ptr, &H, &old_size)) {
        unsigned int reason = heap_last_error;
        if (reason == HEAP_GUARD_DOUBLE_FREE) {
            heap_panic_double_free();
        } else {
            heap_panic_invalid_free();
        }
        irq_restore(flags);
        return NULL;
    }
    irq_restore(flags);

    void *new_ptr = malloc(size);
    if (new_ptr == NULL) {
        return NULL;
    }

    size_t copy_size = (old_size < size) ? old_size : size;
    char *src = (char *)ptr;
    char *dst = (char *)new_ptr;
    for (size_t i = 0; i < copy_size; i++) {
        dst[i] = src[i];
    }

    free(ptr);
    return new_ptr;
}

//===----------------------------------------------------------------------===//
// Swift runtime slow allocation — backed by the Stage-1 heap above.
//===----------------------------------------------------------------------===//

void *swift_slowAlloc(size_t size, size_t alignMask) {
    // alignMask is (alignment - 1), or ~0 / 0 meaning "default" (16-byte).
    if (alignMask == 0 || alignMask == (size_t)-1 || alignMask <= 15) {
        return malloc(size);
    }
    void *p = NULL;
    if (posix_memalign(&p, alignMask + 1, size) != 0) {
        return NULL;
    }
    return p;
}

void swift_slowDealloc(void *ptr, size_t size, size_t alignMask) {
    (void)size;
    (void)alignMask;
    free(ptr);
}

unsigned long heap_total_bytes(void) {
    return HEAP_SIZE;
}

unsigned long heap_free_bytes(void) {
    unsigned long flags = irq_save();
    if (!heap_initialized) {
        heap_init();
    }

    unsigned long total = heap_free_bytes_unsafe();

    irq_restore(flags);
    return total;
}

unsigned long heap_largest_free_bytes(void) {
    unsigned long flags = irq_save();
    if (!heap_initialized) {
        heap_init();
    }

    unsigned long largest = 0;
    free_block *curr = free_list_head;
    while (curr != NULL) {
        unsigned long size = curr->size & ~1UL;
        if (size > largest) {
            largest = size;
        }
        curr = curr->next;
    }

    irq_restore(flags);
    return largest;
}

unsigned long heap_malloc_count(void) {
    unsigned long flags = irq_save();
    unsigned long count = malloc_calls;
    irq_restore(flags);
    return count;
}

unsigned long heap_free_count(void) {
    unsigned long flags = irq_save();
    unsigned long count = free_calls;
    irq_restore(flags);
    return count;
}

unsigned long heap_realloc_count(void) {
    unsigned long flags = irq_save();
    unsigned long count = realloc_calls;
    irq_restore(flags);
    return count;
}

unsigned long heap_calloc_count(void) {
    unsigned long flags = irq_save();
    unsigned long count = calloc_calls;
    irq_restore(flags);
    return count;
}

unsigned long heap_allocated_bytes(void) {
    unsigned long flags = irq_save();
    if (!heap_initialized) {
        heap_init();
    }

    unsigned long allocated = heap_allocated_bytes_unsafe();
    irq_restore(flags);
    return allocated;
}

unsigned long heap_high_water_bytes(void) {
    unsigned long flags = irq_save();
    unsigned long high_water = heap_high_water;
    irq_restore(flags);
    return high_water;
}

unsigned long heap_failed_alloc_count(void) {
    unsigned long flags = irq_save();
    unsigned long count = heap_failed_allocs;
    irq_restore(flags);
    return count;
}

int heap_integrity_check(void) {
    unsigned long flags = irq_save();
    if (!heap_initialized) {
        heap_init();
    }

    if (*(size_t *)HEAP_BASE != 1UL) {
        heap_record_corruption(HEAP_GUARD_SENTINEL);
        irq_restore(flags);
        return 0;
    }
    if (*(size_t *)(HEAP_END - 8) != 1UL) {
        heap_record_corruption(HEAP_GUARD_SENTINEL);
        irq_restore(flags);
        return 0;
    }

    char *p = (char *)(HEAP_BASE + 8);
    char *end = (char *)(HEAP_END - 8);
    unsigned long blocks = 0;
    while (p < end) {
        block_header *h = (block_header *)p;
        size_t raw = h->size;
        size_t size = raw & ~1UL;
        char *footer_addr = p + 8 + size;

        if (size == 0 || (size & 0xFUL) != 0) {
            heap_record_corruption(HEAP_GUARD_BLOCK_SIZE);
            irq_restore(flags);
            return 0;
        }
        if (footer_addr + 8 > end) {
            heap_record_corruption(HEAP_GUARD_BLOCK_SIZE);
            irq_restore(flags);
            return 0;
        }
        if (*(size_t *)footer_addr != raw) {
            heap_record_corruption(HEAP_GUARD_BLOCK_FOOTER);
            irq_restore(flags);
            return 0;
        }

        p += 16 + size;
        blocks++;
        if (blocks > 65536) {
            heap_record_corruption(HEAP_GUARD_BLOCK_SIZE);
            irq_restore(flags);
            return 0;
        }
    }

    if (p != end) {
        heap_record_corruption(HEAP_GUARD_BLOCK_SIZE);
        irq_restore(flags);
        return 0;
    }

    free_block *curr = free_list_head;
    blocks = 0;
    while (curr != NULL) {
        unsigned long addr = (unsigned long)curr;
        if (addr < HEAP_BASE + 8 || addr >= HEAP_END - 8) {
            heap_record_corruption(HEAP_GUARD_FREE_RANGE);
            irq_restore(flags);
            return 0;
        }
        if (((addr - (HEAP_BASE + 8)) & 0xFUL) != 0) {
            heap_record_corruption(HEAP_GUARD_FREE_RANGE);
            irq_restore(flags);
            return 0;
        }

        free_block *seen = curr->next;
        unsigned long seen_count = 0;
        while (seen != NULL) {
            if (seen == curr) {
                heap_record_corruption(HEAP_GUARD_FREE_DUP);
                irq_restore(flags);
                return 0;
            }
            seen = seen->next;
            seen_count++;
            if (seen_count > 65536) {
                heap_record_corruption(HEAP_GUARD_FREE_DUP);
                irq_restore(flags);
                return 0;
            }
        }

        size_t size = curr->size & ~1UL;
        size_t *footer = (size_t *)((char *)curr + 8 + size);
        if ((curr->size & 1UL) != 0) {
            heap_record_corruption(HEAP_GUARD_FREE_ALLOCATED);
            irq_restore(flags);
            return 0;
        }
        if ((size & 0xFUL) != 0 || size == 0) {
            heap_record_corruption(HEAP_GUARD_BLOCK_SIZE);
            irq_restore(flags);
            return 0;
        }
        if ((unsigned long)(footer + 1) > HEAP_END - 8) {
            heap_record_corruption(HEAP_GUARD_BLOCK_SIZE);
            irq_restore(flags);
            return 0;
        }
        if (*footer != size) {
            heap_record_corruption(HEAP_GUARD_FREE_FOOTER);
            irq_restore(flags);
            return 0;
        }

        curr = curr->next;
        blocks++;
        if (blocks > 65536) {
            heap_record_corruption(HEAP_GUARD_FREE_DUP);
            irq_restore(flags);
            return 0;
        }
    }

    heap_record_guard_error(HEAP_GUARD_OK);
    irq_restore(flags);
    return 1;
}

unsigned int heap_guard_last_error(void) {
    unsigned long flags = irq_save();
    unsigned int error = heap_last_error;
    irq_restore(flags);
    return error;
}

unsigned long heap_invalid_free_count(void) {
    unsigned long flags = irq_save();
    unsigned long count = heap_invalid_frees;
    irq_restore(flags);
    return count;
}

unsigned long heap_double_free_count(void) {
    unsigned long flags = irq_save();
    unsigned long count = heap_double_frees;
    irq_restore(flags);
    return count;
}

unsigned long heap_corruption_count(void) {
    unsigned long flags = irq_save();
    unsigned long count = heap_corruptions;
    irq_restore(flags);
    return count;
}

int heap_guard_selftest(void) {
    return heap_integrity_check();
}
