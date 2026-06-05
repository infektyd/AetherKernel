#include "Support.h"

//===----------------------------------------------------------------------===//
// Shared CNTP timer arbiter, in non-inline C.
//
// The CNTP register helpers (read_cntpct / write_cntp_tval / write_cntp_ctl) are
// `static inline` asm. When they were inlined into the Swift IRQ path, the
// assembler reads/writes were previously dropped. Keep the load-bearing timer
// register work here and let clients own their own queues.
//===----------------------------------------------------------------------===//

static unsigned long g_deadline[KERNEL_TIMER_CLIENT_COUNT];
static unsigned int g_active[KERNEL_TIMER_CLIENT_COUNT];

static int valid_client(unsigned int client) {
    return client < KERNEL_TIMER_CLIENT_COUNT;
}

static void timer_panic(void) {
    write_cntp_ctl(0);
    for (;;) {
        wait_for_interrupt();
    }
}

unsigned long kernel_timer_now(void) {
    return read_cntpct();
}

static void kernel_timer_rearm_unsafe(void) {
    unsigned int have_deadline = 0;
    unsigned long min_deadline = 0;

    for (unsigned int i = 0; i < KERNEL_TIMER_CLIENT_COUNT; i++) {
        if (!g_active[i]) {
            continue;
        }
        if (!have_deadline || g_deadline[i] < min_deadline) {
            have_deadline = 1;
            min_deadline = g_deadline[i];
        }
    }

    if (!have_deadline) {
        write_cntp_ctl(0);
        return;
    }

    unsigned long now = read_cntpct();
    unsigned long tval = min_deadline > now ? min_deadline - now : 1;
    write_cntp_tval(tval);
    write_cntp_ctl(1);          // ENABLE, IMASK=0 -> IRQ delivered when it fires
}

void kernel_timer_set_deadline(unsigned int client, unsigned long deadlineTicks) {
    if (!valid_client(client)) {
        timer_panic();
    }

    unsigned long flags = irq_save();
    g_deadline[client] = deadlineTicks;
    g_active[client] = 1;
    kernel_timer_rearm_unsafe();
    irq_restore(flags);
}

void kernel_timer_clear_deadline(unsigned int client) {
    if (!valid_client(client)) {
        timer_panic();
    }

    unsigned long flags = irq_save();
    g_active[client] = 0;
    g_deadline[client] = 0;
    kernel_timer_rearm_unsafe();
    irq_restore(flags);
}

void kernel_timer_rearm(void) {
    unsigned long flags = irq_save();
    kernel_timer_rearm_unsafe();
    irq_restore(flags);
}

unsigned int kernel_timer_active_mask(void) {
    unsigned long flags = irq_save();
    unsigned int mask = 0;

    for (unsigned int i = 0; i < KERNEL_TIMER_CLIENT_COUNT; i++) {
        if (g_active[i]) {
            mask |= 1U << i;
        }
    }

    irq_restore(flags);
    return mask;
}
