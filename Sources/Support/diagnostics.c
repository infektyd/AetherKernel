#include "Support.h"

#define CNTP_INTID 30U
#define UART0_INTID 153U
#define SPURIOUS_INTID_1022 1022U
#define SPURIOUS_INTID_1023 1023U

#define UART0_BASE 0xFE201000UL
#define UART0_DR   0x00UL
#define UART0_FR   0x18UL
#define UART0_FR_BUSY (1U << 3)
#define UART0_FR_TXFF (1U << 5)

#define RETAINED_MAGIC 0x4145544852561b01UL  // "AETHRV27" + version byte
#define RETAINED_REASON_CAPACITY 40U

static unsigned long irq_total;
static unsigned long irq_cntp;
static unsigned long irq_uart0;
static unsigned long irq_spurious;
static unsigned long irq_unknown;

static unsigned int fault_seen;
static unsigned long fault_esr;
static unsigned long fault_elr;
static unsigned long fault_far;
static unsigned int panic_seen;

typedef struct retained_record {
    unsigned long magic;
    unsigned long checksum;
    unsigned long sequence;
    unsigned long kind;
    unsigned long category;
    unsigned long reason_id;
    unsigned long esr;
    unsigned long elr;
    unsigned long far;
    unsigned long reason_len;
    unsigned char retained_reason[RETAINED_REASON_CAPACITY];
} retained_record;

static volatile retained_record *retained(void) {
    return (volatile retained_record *)KERNEL_RETAINED_RECORD_ADDR;
}

static unsigned long dcache_line_size(void) {
    unsigned long ctr;
    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
    return 4UL << ((ctr >> 16) & 0xf);
}

static void retained_flush(const volatile retained_record *r) {
    unsigned long line_size = dcache_line_size();
    unsigned long start = (unsigned long)r & ~(line_size - 1);
    unsigned long end = ((unsigned long)r + sizeof(retained_record) + line_size - 1) & ~(line_size - 1);

    for (unsigned long addr = start; addr < end; addr += line_size) {
        __asm__ volatile("dc cvac, %0" :: "r"(addr) : "memory");
    }
    __asm__ volatile("dsb sy" ::: "memory");
}

static unsigned long retained_checksum(const volatile retained_record *r) {
    unsigned long c = 0x9e3779b97f4a7c15UL;

    c ^= r->magic;
    c = (c << 7) | (c >> (sizeof(unsigned long) * 8 - 7));
    c ^= r->sequence;
    c = (c << 7) | (c >> (sizeof(unsigned long) * 8 - 7));
    c ^= r->kind;
    c = (c << 7) | (c >> (sizeof(unsigned long) * 8 - 7));
    c ^= r->category;
    c = (c << 7) | (c >> (sizeof(unsigned long) * 8 - 7));
    c ^= r->reason_id;
    c = (c << 7) | (c >> (sizeof(unsigned long) * 8 - 7));
    c ^= r->esr;
    c = (c << 7) | (c >> (sizeof(unsigned long) * 8 - 7));
    c ^= r->elr;
    c = (c << 7) | (c >> (sizeof(unsigned long) * 8 - 7));
    c ^= r->far;
    c = (c << 7) | (c >> (sizeof(unsigned long) * 8 - 7));
    c ^= r->reason_len;

    for (unsigned int i = 0; i < RETAINED_REASON_CAPACITY; i++) {
        c = (c * 131UL) ^ (unsigned long)r->retained_reason[i];
    }
    return c;
}

static unsigned int retained_record_valid(void) {
    volatile retained_record *r = retained();
    unsigned long len = r->reason_len;

    if (r->magic != RETAINED_MAGIC) {
        return 0;
    }
    if (r->kind != KERNEL_RETAINED_KIND_PANIC && r->kind != KERNEL_RETAINED_KIND_FAULT) {
        return 0;
    }
    if (r->category > KERNEL_RETAINED_CATEGORY_INTERNAL) {
        return 0;
    }
    if (len > RETAINED_REASON_CAPACITY) {
        return 0;
    }
    return r->checksum == retained_checksum(r);
}

static int cstr_equal(const char *a, const char *b) {
    if (a == 0 || b == 0) {
        return 0;
    }
    while (*a != '\0' && *b != '\0') {
        if (*a != *b) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

unsigned int kernel_panic_reason_id_for(const char *reason) {
    if (cstr_equal(reason, "panic-test")) {
        return KERNEL_RETAINED_REASON_PANIC_TEST;
    }
    if (cstr_equal(reason, "sync-fault")) {
        return KERNEL_RETAINED_REASON_SYNC_FAULT;
    }
    if (cstr_equal(reason, "heap-invalid-free") ||
        cstr_equal(reason, "heap-invalid-free-test-survived")) {
        return KERNEL_RETAINED_REASON_HEAP_INVALID_FREE;
    }
    if (cstr_equal(reason, "heap-double-free") ||
        cstr_equal(reason, "heap-double-free-test-alloc") ||
        cstr_equal(reason, "heap-double-free-test-survived")) {
        return KERNEL_RETAINED_REASON_HEAP_DOUBLE_FREE;
    }
    if (cstr_equal(reason, "memory-map-overlap")) {
        return KERNEL_RETAINED_REASON_MEMORY_MAP_OVERLAP;
    }
    if (cstr_equal(reason, "kernel-object-registry-full")) {
        return KERNEL_RETAINED_REASON_KERNEL_OBJECT_REGISTRY_FULL;
    }
    if (cstr_equal(reason, "kernel-task-registry-bad-id")) {
        return KERNEL_RETAINED_REASON_KERNEL_TASK_REGISTRY_BAD_ID;
    }
    return KERNEL_RETAINED_REASON_UNKNOWN;
}

unsigned int kernel_panic_category_for_reason_id(unsigned int reason_id) {
    switch (reason_id) {
    case KERNEL_RETAINED_REASON_PANIC_TEST:
        return KERNEL_RETAINED_CATEGORY_COMMAND;
    case KERNEL_RETAINED_REASON_SYNC_FAULT:
        return KERNEL_RETAINED_CATEGORY_FAULT;
    case KERNEL_RETAINED_REASON_HEAP_INVALID_FREE:
    case KERNEL_RETAINED_REASON_HEAP_DOUBLE_FREE:
        return KERNEL_RETAINED_CATEGORY_HEAP;
    case KERNEL_RETAINED_REASON_MEMORY_MAP_OVERLAP:
        return KERNEL_RETAINED_CATEGORY_MEMORY;
    case KERNEL_RETAINED_REASON_KERNEL_OBJECT_REGISTRY_FULL:
    case KERNEL_RETAINED_REASON_KERNEL_TASK_REGISTRY_BAD_ID:
        return KERNEL_RETAINED_CATEGORY_REGISTRY;
    default:
        return KERNEL_RETAINED_CATEGORY_INTERNAL;
    }
}

static void retained_write(unsigned long kind,
                           unsigned long category,
                           unsigned long reason_id,
                           unsigned long esr,
                           unsigned long elr,
                           unsigned long far,
                           const char *reason) {
    volatile retained_record *r = retained();
    unsigned long next_sequence = retained_record_valid() ? r->sequence + 1 : 1;
    unsigned long i = 0;

    r->magic = RETAINED_MAGIC;
    r->checksum = 0;
    r->sequence = next_sequence;
    r->kind = kind;
    r->category = category;
    r->reason_id = reason_id;
    r->esr = esr;
    r->elr = elr;
    r->far = far;

    if (reason != 0) {
        while (i < RETAINED_REASON_CAPACITY && reason[i] != '\0') {
            r->retained_reason[i] = (unsigned char)reason[i];
            i++;
        }
    }
    r->reason_len = i;
    while (i < RETAINED_REASON_CAPACITY) {
        r->retained_reason[i] = 0;
        i++;
    }

    r->checksum = retained_checksum(r);
    retained_flush(r);
}

static void panic_uart_putc(char c) {
    while (mmio_read32(UART0_BASE + UART0_FR) & UART0_FR_TXFF) {
        nop();
    }
    mmio_write32(UART0_BASE + UART0_DR, (unsigned int)(unsigned char)c);
}

static void panic_uart_puts(const char *s) {
    while (*s != '\0') {
        if (*s == '\n') {
            panic_uart_putc('\r');
        }
        panic_uart_putc(*s);
        s++;
    }
}

static void panic_uart_drain(void) {
    while (mmio_read32(UART0_BASE + UART0_FR) & UART0_FR_BUSY) {
        nop();
    }
}

void kernel_irq_record(unsigned int intid) {
    irq_total++;
    if (intid == CNTP_INTID) {
        irq_cntp++;
    } else if (intid == UART0_INTID) {
        irq_uart0++;
    } else if (intid == SPURIOUS_INTID_1022 || intid == SPURIOUS_INTID_1023) {
        irq_spurious++;
    } else {
        irq_unknown++;
    }
}

unsigned long kernel_irq_total_count(void) {
    unsigned long flags = irq_save();
    unsigned long count = irq_total;
    irq_restore(flags);
    return count;
}

unsigned long kernel_irq_cntp_count(void) {
    unsigned long flags = irq_save();
    unsigned long count = irq_cntp;
    irq_restore(flags);
    return count;
}

unsigned long kernel_irq_uart0_count(void) {
    unsigned long flags = irq_save();
    unsigned long count = irq_uart0;
    irq_restore(flags);
    return count;
}

unsigned long kernel_irq_spurious_count(void) {
    unsigned long flags = irq_save();
    unsigned long count = irq_spurious;
    irq_restore(flags);
    return count;
}

unsigned long kernel_irq_unknown_count(void) {
    unsigned long flags = irq_save();
    unsigned long count = irq_unknown;
    irq_restore(flags);
    return count;
}

void kernel_record_fault(unsigned long esr, unsigned long elr, unsigned long far) {
    fault_seen = 1;
    fault_esr = esr;
    fault_elr = elr;
    fault_far = far;
}

unsigned int kernel_fault_seen(void) {
    return fault_seen;
}

unsigned long kernel_fault_esr(void) {
    return fault_esr;
}

unsigned long kernel_fault_elr(void) {
    return fault_elr;
}

unsigned long kernel_fault_far(void) {
    return fault_far;
}

unsigned int kernel_panic_seen(void) {
    return panic_seen;
}

unsigned int kernel_retained_valid(void) {
    return retained_record_valid();
}

unsigned int kernel_retained_kind(void) {
    if (!retained_record_valid()) {
        return KERNEL_RETAINED_KIND_NONE;
    }
    return (unsigned int)retained()->kind;
}

unsigned int kernel_retained_category(void) {
    if (!retained_record_valid()) {
        return KERNEL_RETAINED_CATEGORY_NONE;
    }
    return (unsigned int)retained()->category;
}

unsigned int kernel_retained_reason_id(void) {
    if (!retained_record_valid()) {
        return KERNEL_RETAINED_REASON_UNKNOWN;
    }
    return (unsigned int)retained()->reason_id;
}

unsigned long kernel_retained_sequence(void) {
    if (!retained_record_valid()) {
        return 0;
    }
    return retained()->sequence;
}

unsigned long kernel_retained_esr(void) {
    if (!retained_record_valid()) {
        return 0;
    }
    return retained()->esr;
}

unsigned long kernel_retained_elr(void) {
    if (!retained_record_valid()) {
        return 0;
    }
    return retained()->elr;
}

unsigned long kernel_retained_far(void) {
    if (!retained_record_valid()) {
        return 0;
    }
    return retained()->far;
}

unsigned int kernel_retained_reason_len(void) {
    if (!retained_record_valid()) {
        return 0;
    }
    return (unsigned int)retained()->reason_len;
}

unsigned int kernel_retained_reason_byte(unsigned int index) {
    if (!retained_record_valid() || index >= retained()->reason_len) {
        return 0;
    }
    return retained()->retained_reason[index];
}

void kernel_retained_clear(void) {
    unsigned long flags = irq_save();
    volatile retained_record *r = retained();
    r->magic = 0;
    r->checksum = 0;
    r->sequence = 0;
    r->kind = KERNEL_RETAINED_KIND_NONE;
    r->category = KERNEL_RETAINED_CATEGORY_NONE;
    r->reason_id = KERNEL_RETAINED_REASON_UNKNOWN;
    r->esr = 0;
    r->elr = 0;
    r->far = 0;
    r->reason_len = 0;
    for (unsigned int i = 0; i < RETAINED_REASON_CAPACITY; i++) {
        r->retained_reason[i] = 0;
    }
    retained_flush(r);
    irq_restore(flags);
}

void kernel_retained_write_panic(const char *reason) {
    unsigned int reason_id = kernel_panic_reason_id_for(reason);
    unsigned int category = kernel_panic_category_for_reason_id(reason_id);
    retained_write(KERNEL_RETAINED_KIND_PANIC, category, reason_id, 0, 0, 0, reason);
}

void kernel_retained_write_fault(unsigned long esr, unsigned long elr, unsigned long far) {
    retained_write(KERNEL_RETAINED_KIND_FAULT, KERNEL_RETAINED_CATEGORY_FAULT, KERNEL_RETAINED_REASON_SYNC_FAULT, esr, elr, far, "sync-fault");
}

static void panic_uart_put_uint(unsigned int value) {
    char buf[10];
    unsigned int i = 0;

    if (value == 0) {
        panic_uart_putc('0');
        return;
    }
    while (value != 0 && i < sizeof(buf)) {
        buf[i++] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    while (i > 0) {
        panic_uart_putc(buf[--i]);
    }
}

void kernel_panic_with_taxonomy(const char *reason,
                                unsigned int category,
                                unsigned int reason_id,
                                unsigned long esr,
                                unsigned long elr,
                                unsigned long far) {
    irq_disable();
    panic_seen = 1;
    retained_write(KERNEL_RETAINED_KIND_PANIC, category, reason_id, esr, elr, far, reason);
    panic_uart_puts("kernel panic reason=");
    panic_uart_puts(reason);
    panic_uart_puts(" category=");
    panic_uart_put_uint(category);
    panic_uart_puts(" reason_id=");
    panic_uart_put_uint(reason_id);
    panic_uart_puts("\n");
    panic_uart_drain();
    watchdog_reset_now();
    for (;;) {
        wait_for_interrupt();
    }
}

void kernel_panic_with_detail(const char *reason, unsigned long esr, unsigned long elr, unsigned long far) {
    unsigned int reason_id = kernel_panic_reason_id_for(reason);
    unsigned int category = kernel_panic_category_for_reason_id(reason_id);
    kernel_panic_with_taxonomy(reason, category, reason_id, esr, elr, far);
}

void kernel_panic_with_far(const char *reason, unsigned long far) {
    kernel_panic_with_detail(reason, 0, 0, far);
}

void kernel_panic(const char *reason) {
    kernel_panic_with_far(reason, 0);
}

void kernel_panic_test(void) {
    kernel_panic("panic-test");
}

void kernel_trigger_sync_fault(void) {
    __asm__ volatile("brk #0xA5");
}
