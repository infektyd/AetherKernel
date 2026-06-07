// Runtime V49: fault-safe copy_from_user / copy_to_user via page-table probes.
#include "Support.h"

#define KERNEL_UACCESS_EFAULT (-14L)

static unsigned long kernel_uaccess_active_pt = 0;

void kernel_uaccess_set_active_pt(unsigned long l1_pa) {
    kernel_uaccess_active_pt = l1_pa;
}

unsigned long kernel_uaccess_active_pt_read(void) {
    return kernel_uaccess_active_pt;
}

long kernel_copy_from_user(void *kdst, unsigned long usrc, unsigned long len) {
    if (kdst == 0 || len == 0) {
        return 0;
    }
    if (kernel_uaccess_active_pt == 0) {
        return KERNEL_UACCESS_EFAULT;
    }

    unsigned char *dst = (unsigned char *)kdst;
    unsigned long copied = 0;

    while (copied < len) {
        unsigned long uva = usrc + copied;
        unsigned long page_base = uva & ~0xfffUL;
        unsigned long page_off = uva & 0xfffUL;
        unsigned long attrs = 0;
        unsigned long pa = kernel_vmm_lookup_in_table(kernel_uaccess_active_pt, page_base, &attrs);

        if (pa == 0 || (attrs & KERNEL_VMM_ATTR_USER) == 0) {
            return copied == 0 ? KERNEL_UACCESS_EFAULT : (long)copied;
        }

        unsigned long chunk = 4096UL - page_off;
        if (chunk > len - copied) {
            chunk = len - copied;
        }

        const unsigned char *src = (const unsigned char *)(pa + page_off);
        for (unsigned long i = 0; i < chunk; i++) {
            dst[copied + i] = src[i];
        }
        copied += chunk;
    }

    return (long)copied;
}

long kernel_copy_to_user(unsigned long udst, const void *ksrc, unsigned long len) {
    if (ksrc == 0 || len == 0) {
        return 0;
    }
    if (kernel_uaccess_active_pt == 0) {
        return KERNEL_UACCESS_EFAULT;
    }

    const unsigned char *src = (const unsigned char *)ksrc;
    unsigned long copied = 0;

    while (copied < len) {
        unsigned long uva = udst + copied;
        unsigned long page_base = uva & ~0xfffUL;
        unsigned long page_off = uva & 0xfffUL;
        unsigned long attrs = 0;
        unsigned long pa = kernel_vmm_lookup_in_table(kernel_uaccess_active_pt, page_base, &attrs);

        if (pa == 0 || (attrs & KERNEL_VMM_ATTR_USER) == 0) {
            return copied == 0 ? KERNEL_UACCESS_EFAULT : (long)copied;
        }

        unsigned long chunk = 4096UL - page_off;
        if (chunk > len - copied) {
            chunk = len - copied;
        }

        unsigned char *dst = (unsigned char *)(pa + page_off);
        for (unsigned long i = 0; i < chunk; i++) {
            dst[i] = src[copied + i];
        }
        copied += chunk;
    }

    return (long)copied;
}

int kernel_uaccess_selftest(void) {
    unsigned long pt = kernel_vmm_alloc_pt();
    if (pt == 0) {
        return 0;
    }
    kernel_vmm_init_space(pt);

    unsigned long data_frame = kernel_frame_alloc();
    if (data_frame == 0) {
        kernel_vmm_free_space(pt);
        return 0;
    }

    volatile unsigned char *data = (volatile unsigned char *)data_frame;
    for (unsigned int i = 0; i < 16; i++) {
        data[i] = (unsigned char)(0xA0 + i);
    }

    unsigned long user_data_va = 0x100004000UL;
    unsigned long bad_user_va = 0x200000000UL;
    unsigned long attrs = KERNEL_VMM_ATTR_USER | (1UL << 11);

    if (!kernel_vmm_map_in_table(pt, user_data_va, data_frame, attrs)) {
        kernel_frame_free(data_frame);
        kernel_vmm_free_space(pt);
        return 0;
    }

    kernel_uaccess_set_active_pt(pt);

    unsigned char kbuf[16];
    for (unsigned int i = 0; i < 16; i++) {
        kbuf[i] = 0;
    }

    long from_ok = kernel_copy_from_user(kbuf, user_data_va, 8);
    long from_bad = kernel_copy_from_user(kbuf, bad_user_va, 8);

    unsigned char ksrc[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    long to_ok = kernel_copy_to_user(user_data_va + 8, ksrc, 8);
    long to_bad = kernel_copy_to_user(bad_user_va, ksrc, 8);

    kernel_uaccess_set_active_pt(0);

    int pattern_ok = 1;
    for (unsigned int i = 0; i < 8; i++) {
        if (kbuf[i] != (unsigned char)(0xA0 + i)) {
            pattern_ok = 0;
            break;
        }
    }

    volatile unsigned char *mapped = (volatile unsigned char *)(data_frame + 8);
    int to_pattern_ok = 1;
    for (unsigned int i = 0; i < 8; i++) {
        if (mapped[i] != ksrc[i]) {
            to_pattern_ok = 0;
            break;
        }
    }

    kernel_vmm_unmap_in_table(pt, user_data_va);
    kernel_frame_free(data_frame);
    kernel_vmm_free_space(pt);

    return (from_ok == 8 && from_bad == KERNEL_UACCESS_EFAULT &&
            to_ok == 8 && to_bad == KERNEL_UACCESS_EFAULT &&
            pattern_ok && to_pattern_ok) ? 1 : 0;
}