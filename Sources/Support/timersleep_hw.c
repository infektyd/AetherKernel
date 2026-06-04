#include "Support.h"

//===----------------------------------------------------------------------===//
// Timer-sleep hardware ops, in non-inline C.
//
// The CNTP register helpers (read_cntpct / write_cntp_tval / write_cntp_ctl) are
// `static inline` asm. When they were inlined into the @_cdecl IRQ-path Swift
// function (serviceTimerSleeper), the assembler reads/writes got dropped — e.g.
// `read_cntpct()` emitted its `isb` but NOT the `mrs cntpct_el0`, so `now` was
// garbage and the sleeper never matured. The same helpers compile correctly in
// ordinary (task-context) Swift and in C. So we do ALL of the sleeper's register
// work here in plain, non-inline C and let Swift handle only the continuation.
//===----------------------------------------------------------------------===//

// Absolute CNTP deadline (counter ticks) of the one outstanding sleeper.
static unsigned long g_sleep_deadline;

// Arm CNTP to fire after `secs` seconds and record the absolute deadline.
void timer_sleep_arm(unsigned long secs) {
    unsigned long flags = irq_save();
    unsigned long now = read_cntpct();
    unsigned long ticks = read_cntfrq() * secs;
    g_sleep_deadline = now + ticks;
    write_cntp_tval(ticks);     // CVAL = now + ticks
    write_cntp_ctl(1);          // ENABLE, IMASK=0 -> IRQ delivered when it fires
    irq_restore(flags);
}

// Called from the timer IRQ. Returns 1 if the sleeper's deadline has passed
// (and disables CNTP, de-asserting the level IRQ before EOI); otherwise re-arms
// CNTP to the remaining time and returns 0.
int timer_sleep_due(void) {
    unsigned long now = read_cntpct();
    if (now >= g_sleep_deadline) {
        write_cntp_ctl(0);      // disable -> de-assert the IRQ line
        return 1;
    }
    write_cntp_tval(g_sleep_deadline - now);
    write_cntp_ctl(1);
    return 0;
}
