#include "Support.h"

#define HEAP_BASE 0x400000UL
#define HEAP_SIZE 0x400000UL
#define HEAP_END  (HEAP_BASE + HEAP_SIZE)

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
    size_t payload_size = (HEAP_END - 8) - (HEAP_BASE + 8) - 16;
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

    irq_restore(flags);
    return (void *)((char *)found + 8);
}

void free(void *ptr) {
    if (ptr == NULL) {
        return;
    }

    unsigned long flags = irq_save();
    free_calls++;

    size_t val = *((size_t *)ptr - 1);
    block_header *H;
    if (val & 1UL) {
        // Standard block
        H = (block_header *)((char *)ptr - 8);
    } else {
        // Back-pointer
        H = (block_header *)val;
    }

    size_t S = H->size & ~1UL;

    // Next block
    block_header *next_H = (block_header *)((char *)H + 16 + S);
    int next_free = !(next_H->size & 1UL);
    size_t next_S = next_H->size & ~1UL;

    // Previous block
    size_t prev_footer_val = *((size_t *)H - 1);
    int prev_free = !(prev_footer_val & 1UL);
    size_t prev_S = prev_footer_val;

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

    void *new_ptr = malloc(size);
    if (new_ptr == NULL) {
        return NULL;
    }

    size_t val = *((size_t *)ptr - 1);
    block_header *H;
    if (val & 1UL) {
        H = (block_header *)((char *)ptr - 8);
    } else {
        H = (block_header *)val;
    }
    size_t old_size = H->size & ~1UL;

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

    unsigned long total = 0;
    free_block *curr = free_list_head;
    while (curr != NULL) {
        total += curr->size & ~1UL;
        curr = curr->next;
    }

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
