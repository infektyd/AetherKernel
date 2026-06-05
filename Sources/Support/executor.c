#include <swift/ExecutorImpl.h>
#include "Support.h"

//===----------------------------------------------------------------------===//
// AetherKernel — cooperative global executor (Runtime V2).
//
// Single-threaded FIFO ready ring plus a fixed delayed-job queue. CNTP ownership
// is shared through the kernel timer arbiter: this executor owns the EXECUTOR
// client deadline, while TimerSleep.swift owns the SLEEP client deadline.
//===----------------------------------------------------------------------===//

#define READY_CAPACITY 64
#define DELAY_CAPACITY 32

#define UART0_BASE 0xFE201000UL
#define UART0_DR   0x00UL
#define UART0_FR   0x18UL
#define UART0_FR_TXFF (1U << 5)

static SwiftJob *ready[READY_CAPACITY];
static unsigned int ready_head;
static unsigned int ready_tail;

static struct {
    unsigned long long deadlineTicks;
    SwiftJob *job;
} delayed[DELAY_CAPACITY];
static unsigned int delayed_count;

//===----------------------------------------------------------------------===//
// UART panic — direct PL011 MMIO (no Swift/C shim dependency).
//===----------------------------------------------------------------------===//

static void uart_putc_panic(char c) {
    while (mmio_read32(UART0_BASE + UART0_FR) & UART0_FR_TXFF) {
        nop();
    }
    mmio_write32(UART0_BASE + UART0_DR, (unsigned int)(unsigned char)c);
}

static void uart_puts_panic(const char *s) {
    while (*s != '\0') {
        if (*s == '\n') {
            uart_putc_panic('\r');
        }
        uart_putc_panic(*s);
        s++;
    }
}

static void executor_panic(const char *msg) {
    uart_puts_panic("EXECUTOR PANIC: ");
    uart_puts_panic(msg);
    uart_puts_panic("\n");
    for (;;) {
        wait_for_interrupt();
    }
}

//===----------------------------------------------------------------------===//
// Generic timer helpers.
//===----------------------------------------------------------------------===//

// ns * cntfrq fits in u64 for delays up to ~170 s at ~54 MHz.
static unsigned long long ns_to_ticks(unsigned long long ns) {
    return (ns * (unsigned long long)read_cntfrq()) / 1000000000ULL;
}

static unsigned long long swift_time_to_ns(SwiftTime t) {
    return (unsigned long long)t.seconds * 1000000000ULL
         + (unsigned long long)t.nanoseconds;
}

static unsigned long long time_parts_to_ns(long long sec, long long nsec) {
    unsigned long long s = sec > 0 ? (unsigned long long)sec : 0;
    unsigned long long ns = nsec > 0 ? (unsigned long long)nsec : 0;
    return s * 1000000000ULL + ns;
}

//===----------------------------------------------------------------------===//
// Ready ring (FIFO). Caller must hold an irq_save() critical section.
//===----------------------------------------------------------------------===//

static void ready_push_unsafe(SwiftJob *job) {
    unsigned int next = (ready_tail + 1) % READY_CAPACITY;
    if (next == ready_head) {
        executor_panic("ready queue overflow");
    }
    ready[ready_tail] = job;
    ready_tail = next;
}

static SwiftJob *ready_pop_unsafe(void) {
    if (ready_head == ready_tail) {
        return NULL;
    }
    SwiftJob *job = ready[ready_head];
    ready_head = (ready_head + 1) % READY_CAPACITY;
    return job;
}

//===----------------------------------------------------------------------===//
// Delay queue helpers. The executor owns the KERNEL_TIMER_CLIENT_EXECUTOR
// deadline in the shared CNTP arbiter.
// Caller must hold an irq_save() critical section.
//===----------------------------------------------------------------------===//

static void delay_push_unsafe(unsigned long long deadlineTicks, SwiftJob *job) {
    if (delayed_count >= DELAY_CAPACITY) {
        executor_panic("delay queue overflow");
    }
    delayed[delayed_count].deadlineTicks = deadlineTicks;
    delayed[delayed_count].job = job;
    delayed_count++;
}

static unsigned long long delay_min_deadline_unsafe(void) {
    if (delayed_count == 0) {
        return 0;
    }
    unsigned long long min = delayed[0].deadlineTicks;
    unsigned int i;
    for (i = 1; i < delayed_count; i++) {
        if (delayed[i].deadlineTicks < min) {
            min = delayed[i].deadlineTicks;
        }
    }
    return min;
}

static void executor_rearm_timer_unsafe(void) {
    if (delayed_count == 0) {
        kernel_timer_clear_deadline(KERNEL_TIMER_CLIENT_EXECUTOR);
        return;
    }
    kernel_timer_set_deadline(KERNEL_TIMER_CLIENT_EXECUTOR,
                              (unsigned long)delay_min_deadline_unsafe());
}

static void delay_schedule_deadline(unsigned long long deadlineTicks,
                                    SwiftJob *job) {
    unsigned long flags = irq_save();
    delay_push_unsafe(deadlineTicks, job);
    executor_rearm_timer_unsafe();
    irq_restore(flags);
}

static void delay_schedule_ns(unsigned long long delayNs, SwiftJob *job) {
    if (delayNs == 0) {
        swift_task_enqueueGlobalImpl(job);
        return;
    }

    unsigned long long ticks = ns_to_ticks(delayNs);
    if (ticks == 0) {
        ticks = 1;
    }

    delay_schedule_deadline((unsigned long long)kernel_timer_now() + ticks, job);
}

static void promote_due_jobs(void) {
    unsigned long flags = irq_save();
    unsigned long long now = kernel_timer_now();
    unsigned int i = 0;

    while (i < delayed_count) {
        if (delayed[i].deadlineTicks <= now) {
            ready_push_unsafe(delayed[i].job);
            delayed_count--;
            if (i < delayed_count) {
                delayed[i] = delayed[delayed_count];
            }
        } else {
            i++;
        }
    }

    executor_rearm_timer_unsafe();
    irq_restore(flags);
}

static void arm_next_deadline(void) {
    unsigned long flags = irq_save();
    executor_rearm_timer_unsafe();
    irq_restore(flags);
}

//===----------------------------------------------------------------------===//
// Swift executor Impl hooks (swiftcall — do NOT define the public trampolines).
//===----------------------------------------------------------------------===//

SWIFT_CC(swift) void swift_task_enqueueGlobalImpl(SwiftJob *job) {
    unsigned long flags = irq_save();
    ready_push_unsafe(job);
    irq_restore(flags);
}

SWIFT_CC(swift) void swift_task_enqueueMainExecutorImpl(SwiftJob *job) {
    swift_task_enqueueGlobalImpl(job);
}

SWIFT_CC(swift) void swift_task_enqueueGlobalWithDelayImpl(SwiftJobDelay delayNs,
                                                           SwiftJob *job) {
    delay_schedule_ns((unsigned long long)delayNs, job);
}

SWIFT_CC(swift) void swift_task_enqueueGlobalWithDeadlineImpl(long long sec,
                                                              long long nsec,
                                                              long long tsec,
                                                              long long tnsec,
                                                              int clock,
                                                              SwiftJob *job) {
    (void)tsec;
    (void)tnsec;

    SwiftTime now = swift_time_now((SwiftClockId)clock);
    unsigned long long nowNs = swift_time_to_ns(now);
    unsigned long long targetNs = time_parts_to_ns(sec, nsec);

    if (targetNs <= nowNs) {
        swift_task_enqueueGlobalImpl(job);
        return;
    }

    delay_schedule_ns(targetNs - nowNs, job);
}

SWIFT_CC(swift) SwiftExecutorRef swift_task_getMainExecutorImpl(void) {
    return swift_executor_generic();
}

SWIFT_CC(swift) bool swift_task_isMainExecutorImpl(SwiftExecutorRef executor) {
    (void)executor;
    return true;
}

SWIFT_CC(swift) void swift_task_checkIsolatedImpl(SwiftExecutorRef executor) {
    (void)executor;
}

SWIFT_CC(swift) int8_t
swift_task_isIsolatingCurrentContextImpl(SwiftExecutorRef executor) {
    (void)executor;
    return 1;
}

SWIFT_CC(swift) void
swift_task_donateThreadToGlobalExecutorUntilImpl(bool (*condition)(void *),
                                                 void *conditionContext) {
    for (;;) {
        promote_due_jobs();
        SwiftJob *job;
        unsigned long flags = irq_save();
        job = ready_pop_unsafe();
        irq_restore(flags);

        if (job != NULL) {
            swift_job_run(job, swift_executor_generic());
            continue;
        }
        if (condition(conditionContext)) {
            return;
        }
        arm_next_deadline();
        wait_for_interrupt();
    }
}

SWIFT_RUNTIME_ATTRIBUTE_NORETURN SWIFT_CC(swift) void
swift_task_asyncMainDrainQueueImpl(void) {
    for (;;) {
        promote_due_jobs();

        SwiftJob *job;
        unsigned long flags = irq_save();
        job = ready_pop_unsafe();
        irq_restore(flags);

        if (job != NULL) {
            swift_job_run(job, swift_executor_generic());
            continue;
        }

        arm_next_deadline();
        wait_for_interrupt();
    }
}

//===----------------------------------------------------------------------===//
// Timer IRQ entry (GIC ack/EOI stays in Swift; we only touch CNTP + queues).
//===----------------------------------------------------------------------===//

void executor_on_timer_irq(void) {
    promote_due_jobs();
}

unsigned int executor_ready_count(void) {
    unsigned long flags = irq_save();
    unsigned int count;
    if (ready_tail >= ready_head) {
        count = ready_tail - ready_head;
    } else {
        count = READY_CAPACITY - ready_head + ready_tail;
    }
    irq_restore(flags);
    return count;
}

unsigned int executor_ready_capacity(void) {
    return READY_CAPACITY - 1;
}

unsigned int executor_delayed_count(void) {
    unsigned long flags = irq_save();
    unsigned int count = delayed_count;
    irq_restore(flags);
    return count;
}

unsigned int executor_delayed_capacity(void) {
    return DELAY_CAPACITY;
}
