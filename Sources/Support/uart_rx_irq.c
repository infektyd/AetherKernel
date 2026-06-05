#include "Support.h"

//===----------------------------------------------------------------------===//
// IRQ-backed PL011 UART0 RX ring.
//
// Runtime V4 keeps the interrupt path fixed and allocation-free. The Swift side
// awaits bytes; this file owns the PL011 RX interrupt service and byte storage.
//===----------------------------------------------------------------------===//

#define UART0_BASE 0xFE201000UL
#define UART0_DR   0x00UL
#define UART0_FR   0x18UL
#define UART0_IFLS 0x34UL
#define UART0_IMSC 0x38UL
#define UART0_MIS  0x40UL
#define UART0_ICR  0x44UL

#define UART_FR_RXFE (1U << 4)
#define UART_IMSC_RXIM (1U << 4)
#define UART_IMSC_RTIM (1U << 6)
#define UART_ICR_RXIC  (1U << 4)
#define UART_ICR_RTIC  (1U << 6)
#define UART_IRQ_RX_MASK (UART_IMSC_RXIM | UART_IMSC_RTIM)

#define UART_RX_RING_CAPACITY 128U

static unsigned char uart_rx_ring[UART_RX_RING_CAPACITY];
static unsigned int uart_rx_head;
static unsigned int uart_rx_tail;
static unsigned int uart_rx_overflows;

static unsigned int ring_next(unsigned int index) {
    return (index + 1U) % UART_RX_RING_CAPACITY;
}

static int ring_empty_unsafe(void) {
    return uart_rx_head == uart_rx_tail;
}

static void ring_push_unsafe(unsigned int byte) {
    unsigned int next = ring_next(uart_rx_tail);
    if (next == uart_rx_head) {
        uart_rx_overflows++;
        return;
    }

    uart_rx_ring[uart_rx_tail] = (unsigned char)(byte & 0xFFU);
    uart_rx_tail = next;
}

static int ring_pop_unsafe(unsigned int *out) {
    if (ring_empty_unsafe()) {
        return 0;
    }

    *out = (unsigned int)uart_rx_ring[uart_rx_head];
    uart_rx_head = ring_next(uart_rx_head);
    return 1;
}

void uart_rx_irq_init(void) {
    unsigned long flags = irq_save();

    uart_rx_head = 0;
    uart_rx_tail = 0;
    uart_rx_overflows = 0;

    // Leave TX interrupt masks untouched if future milestones enable them.
    mmio_write32(UART0_BASE + UART0_IMSC,
                 mmio_read32(UART0_BASE + UART0_IMSC) & ~UART_IRQ_RX_MASK);

    while ((mmio_read32(UART0_BASE + UART0_FR) & UART_FR_RXFE) == 0) {
        (void)mmio_read32(UART0_BASE + UART0_DR);
    }

    mmio_write32(UART0_BASE + UART0_ICR, UART_ICR_RXIC | UART_ICR_RTIC);
    irq_restore(flags);
}

void uart_rx_irq_enable(void) {
    unsigned long flags = irq_save();
    mmio_write32(UART0_BASE + UART0_ICR, UART_ICR_RXIC | UART_ICR_RTIC);
    mmio_write32(UART0_BASE + UART0_IMSC,
                 mmio_read32(UART0_BASE + UART0_IMSC) | UART_IRQ_RX_MASK);
    irq_restore(flags);
}

void uart_rx_irq_service(void) {
    unsigned long flags = irq_save();
    unsigned int pending = mmio_read32(UART0_BASE + UART0_MIS);

    if ((pending & UART_IRQ_RX_MASK) != 0) {
        while ((mmio_read32(UART0_BASE + UART0_FR) & UART_FR_RXFE) == 0) {
            ring_push_unsafe(mmio_read32(UART0_BASE + UART0_DR));
        }
    }

    mmio_write32(UART0_BASE + UART0_ICR, UART_ICR_RXIC | UART_ICR_RTIC);
    irq_restore(flags);
}

int uart_rx_ring_read_byte(unsigned int *out) {
    if (out == 0) {
        return 0;
    }

    unsigned long flags = irq_save();
    int ok = ring_pop_unsafe(out);
    irq_restore(flags);
    return ok;
}

unsigned int uart_rx_ring_count(void) {
    unsigned long flags = irq_save();
    unsigned int count;

    if (uart_rx_tail >= uart_rx_head) {
        count = uart_rx_tail - uart_rx_head;
    } else {
        count = UART_RX_RING_CAPACITY - uart_rx_head + uart_rx_tail;
    }

    irq_restore(flags);
    return count;
}

unsigned int uart_rx_ring_capacity(void) {
    return UART_RX_RING_CAPACITY - 1U;
}

unsigned int uart_rx_overflow_count(void) {
    unsigned long flags = irq_save();
    unsigned int count = uart_rx_overflows;
    irq_restore(flags);
    return count;
}
