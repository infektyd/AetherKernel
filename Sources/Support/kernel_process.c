// Runtime V51: Process abstraction — address space + lifecycle state.
#include "Support.h"

#define KPROC_CAPACITY 8

#define KPROC_STATE_FREE    0
#define KPROC_STATE_CREATED 1

typedef struct {
    unsigned int  state;
    unsigned int  id;
    unsigned long pt_pa;
    unsigned int  asid;
} kernel_process_t;

static kernel_process_t kproc_table[KPROC_CAPACITY];
static unsigned int     kproc_next_id = 1;
// 256-bit ASID bitmap (bit i = 1 means ASID i in use). ASID 0 = kernel, reserved.
static unsigned char    kproc_asid_bitmap[32];

static unsigned int kproc_asid_alloc(void) {
    for (unsigned int i = 1; i < 255; i++) {
        if (!(kproc_asid_bitmap[i >> 3] & (unsigned char)(1u << (i & 7u)))) {
            kproc_asid_bitmap[i >> 3] |= (unsigned char)(1u << (i & 7u));
            return i;
        }
    }
    return 0; // exhausted
}

static void kproc_asid_free(unsigned int asid) {
    if (asid >= 1 && asid < 255)
        kproc_asid_bitmap[asid >> 3] &= (unsigned char)~(1u << (asid & 7u));
}

int kernel_process_create(unsigned long *pid_out) {
    kernel_process_t *slot = 0;
    for (int i = 0; i < KPROC_CAPACITY; i++) {
        if (kproc_table[i].state == KPROC_STATE_FREE) {
            slot = &kproc_table[i];
            break;
        }
    }
    if (!slot) return 0;

    unsigned long pt = kernel_vmm_alloc_pt();
    if (!pt) return 0;
    kernel_vmm_init_space(pt);

    unsigned int asid = kproc_asid_alloc();
    if (!asid) {
        kernel_vmm_free_space(pt);
        return 0;
    }

    slot->state = KPROC_STATE_CREATED;
    slot->id    = kproc_next_id++;
    slot->pt_pa = pt;
    slot->asid  = asid;
    if (pid_out) *pid_out = (unsigned long)slot->id;
    return 1;
}

int kernel_process_destroy(unsigned long pid) {
    for (int i = 0; i < KPROC_CAPACITY; i++) {
        kernel_process_t *p = &kproc_table[i];
        if (p->state != KPROC_STATE_FREE && p->id == (unsigned int)pid) {
            kernel_vmm_free_space(p->pt_pa);
            kproc_asid_free(p->asid);
            p->state = KPROC_STATE_FREE;
            p->id    = 0;
            p->pt_pa = 0;
            p->asid  = 0;
            return 1;
        }
    }
    return 0;
}

int kernel_process_count(void) {
    int n = 0;
    for (int i = 0; i < KPROC_CAPACITY; i++) {
        if (kproc_table[i].state != KPROC_STATE_FREE) n++;
    }
    return n;
}

int kernel_process_capacity(void) {
    return KPROC_CAPACITY;
}

int kernel_process_selftest(void) {
    unsigned long pids[KPROC_CAPACITY];
    int created = 0;

    for (int i = 0; i < KPROC_CAPACITY; i++) {
        if (!kernel_process_create(&pids[i])) break;
        created++;
    }
    if (created == 0) return 0;

    // Verify each pair has unique pt_pa and unique asid
    for (int i = 0; i < created; i++) {
        kernel_process_t *pi = 0;
        for (int k = 0; k < KPROC_CAPACITY; k++) {
            if (kproc_table[k].id == (unsigned int)pids[i]) { pi = &kproc_table[k]; break; }
        }
        if (!pi) goto fail;

        for (int j = i + 1; j < created; j++) {
            kernel_process_t *pj = 0;
            for (int k = 0; k < KPROC_CAPACITY; k++) {
                if (kproc_table[k].id == (unsigned int)pids[j]) { pj = &kproc_table[k]; break; }
            }
            if (!pj) goto fail;
            if (pi->pt_pa == pj->pt_pa || pi->asid == pj->asid) goto fail;
        }
    }

    {
        int destroyed = 0;
        for (int i = 0; i < created; i++) {
            if (kernel_process_destroy(pids[i])) destroyed++;
        }
        return (destroyed == created) ? 1 : 0;
    }

fail:
    for (int i = 0; i < created; i++) kernel_process_destroy(pids[i]);
    return 0;
}
