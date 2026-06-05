#include "Support.h"

#define CNTP_INTID 30U
#define UART0_INTID 153U
#define SPURIOUS_INTID_1022 1022U
#define SPURIOUS_INTID_1023 1023U

#define UART0_BASE 0xFE201000UL
#define UART0_DR   0x00UL
#define UART0_FR   0x18UL
#define UART0_FR_TXFF (1U << 5)

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

void kernel_panic(const char *reason) {
    irq_disable();
    panic_seen = 1;
    panic_uart_puts("kernel panic reason=");
    panic_uart_puts(reason);
    panic_uart_puts("\n");
    for (;;) {
        wait_for_interrupt();
    }
}

void kernel_panic_test(void) {
    kernel_panic("panic-test");
}

void kernel_trigger_sync_fault(void) {
    __asm__ volatile("brk #0xA5");
}
