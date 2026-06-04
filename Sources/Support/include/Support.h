//===----------------------------------------------------------------------===//
// AetherKernel — low-level support shim.
//
// MMIO must go through `volatile` so the optimizer never elides or hoists a
// peripheral read/write (e.g. a polled "wait until TX FIFO has space" loop).
// Swift has no volatile, so these C accessors are the guaranteed-correct path.
//===----------------------------------------------------------------------===//
#pragma once

static inline __attribute__((always_inline)) void nop(void) {
    __asm__ volatile("nop");
}

static inline __attribute__((always_inline)) unsigned int mmio_read32(unsigned long addr) {
    return *(volatile unsigned int *)addr;
}

static inline __attribute__((always_inline)) void mmio_write32(unsigned long addr, unsigned int value) {
    *(volatile unsigned int *)addr = value;
}

// Read CurrentEL (bits [3:2] hold the exception level). Used to prove the
// EL2->EL1 drop actually happened.
static inline __attribute__((always_inline)) unsigned long read_currentel(void) {
    unsigned long v;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(v));
    return v;
}

//===----------------------------------------------------------------------===//
// ARM generic timer (EL1 physical timer). EL1 access is enabled by boot.S
// (CNTHCTL_EL2 = EL1PCTEN|EL1PCEN) with CNTVOFF_EL2 = 0.
//
//   CNTFRQ_EL0   : tick frequency in Hz (firmware-programmed; ~54 MHz on Pi 4)
//   CNTPCT_EL0   : current 64-bit physical count (monotonic up-counter)
//   CNTP_TVAL_EL0: write N -> fire after N ticks (CompareValue = CNTPCT + N)
//   CNTP_CTL_EL0 : bit0 ENABLE, bit1 IMASK (1=mask IRQ), bit2 ISTATUS (RO, set
//                  when the condition is met — independent of IMASK, so it is
//                  pollable without an interrupt controller).
//===----------------------------------------------------------------------===//
static inline __attribute__((always_inline)) unsigned long read_cntfrq(void) {
    unsigned long v;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v));
    return v;
}

static inline __attribute__((always_inline)) unsigned long read_cntpct(void) {
    unsigned long v;
    // isb so the read isn't speculated before prior instructions (ARM ARM).
    __asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(v) :: "memory");
    return v;
}

static inline __attribute__((always_inline)) void write_cntp_tval(unsigned long v) {
    __asm__ volatile("msr cntp_tval_el0, %0" :: "r"(v));
}

static inline __attribute__((always_inline)) void write_cntp_ctl(unsigned long v) {
    __asm__ volatile("msr cntp_ctl_el0, %0" :: "r"(v));
}

static inline __attribute__((always_inline)) unsigned long read_cntp_ctl(void) {
    unsigned long v;
    __asm__ volatile("mrs %0, cntp_ctl_el0" : "=r"(v));
    return v;
}
