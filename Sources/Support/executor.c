#include <swift/ExecutorImpl.h>
#include "Support.h"

//===----------------------------------------------------------------------===//
// AetherKernel — cooperative global executor (Runtime V2).
//
// Thin swiftcall trampolines into Swift-owned queue state (KernelExecutor.swift).
// The embedded runtime dispatches only through these ExecutorImpl.h hooks.
//===----------------------------------------------------------------------===//

#define READY_CAPACITY 64

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
// Swift executor Impl hooks (swiftcall — do NOT define the public trampolines).
//===----------------------------------------------------------------------===//

SWIFT_CC(swift) void swift_task_enqueueGlobalImpl(SwiftJob *job) {
    kernel_executor_enqueue((void *)job);
}

SWIFT_CC(swift) void swift_task_enqueueMainExecutorImpl(SwiftJob *job) {
    kernel_executor_enqueue((void *)job);
}

SWIFT_CC(swift) void swift_task_enqueueGlobalWithDelayImpl(SwiftJobDelay delayNs,
                                                           SwiftJob *job) {
    kernel_executor_enqueue_delay_ns((unsigned long long)delayNs, (void *)job);
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

    kernel_executor_enqueue_deadline_ns(targetNs, nowNs, (void *)job);
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

struct executor_donate_ctx {
    bool (*condition)(void *);
    void *conditionContext;
};

static int executor_donate_condition_trampoline(void *context) {
    struct executor_donate_ctx *ctx = (struct executor_donate_ctx *)context;
    return ctx->condition(ctx->conditionContext) ? 1 : 0;
}

SWIFT_CC(swift) void
swift_task_donateThreadToGlobalExecutorUntilImpl(bool (*condition)(void *),
                                                 void *conditionContext) {
    struct executor_donate_ctx ctx = { condition, conditionContext };
    kernel_executor_donate_until(executor_donate_condition_trampoline, &ctx);
}

SWIFT_RUNTIME_ATTRIBUTE_NORETURN SWIFT_CC(swift) void
swift_task_asyncMainDrainQueueImpl(void) {
    kernel_executor_drain_main();
}

//===----------------------------------------------------------------------===//
// Timer IRQ entry (GIC ack/EOI stays in Swift; we only touch CNTP + queues).
//===----------------------------------------------------------------------===//

void executor_on_timer_irq(void) {
    kernel_executor_on_timer_irq();
}

unsigned int executor_ready_count(void) {
    return (unsigned int)kernel_executor_ready_count();
}

unsigned int executor_ready_capacity(void) {
    return READY_CAPACITY - 1;
}

unsigned int executor_delayed_count(void) {
    return (unsigned int)kernel_executor_delayed_count();
}

unsigned int executor_delayed_capacity(void) {
    return 0;
}
