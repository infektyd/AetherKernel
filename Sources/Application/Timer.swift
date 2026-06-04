//===----------------------------------------------------------------------===//
// ARM generic timer (EL1 physical timer), polled.
//
// This replaces the cycle-counted busy `delay()` with a real, frequency-based
// wait off CNTP. We poll CNTP_CTL_EL0.ISTATUS rather than take an interrupt —
// ISTATUS reflects the timer condition regardless of the IRQ mask, so we get an
// accurate hardware-timed tick with no GIC. (Interrupt-driven ticks come once
// the GIC-400 is up — the next milestone.)
//
// EL1 access to the physical timer was enabled in boot.S
// (CNTHCTL_EL2 = EL1PCTEN|EL1PCEN, CNTVOFF_EL2 = 0).
//===----------------------------------------------------------------------===//
import Support

private let CTL_ENABLE: UInt = 1 << 0   // start the timer
private let CTL_IMASK: UInt = 1 << 1    // mask the IRQ output (no GIC yet)
private let CTL_ISTATUS: UInt = 1 << 2  // read-only: condition met

// Tick frequency in Hz (CNTFRQ_EL0, firmware-programmed, ≈54 MHz on the Pi 4).
// A constant in practice; re-reading is a single cheap `mrs`, so we don't cache
// it in mutable global state (which Embedded Swift 6 would flag).
func timerFrequency() -> UInt { read_cntfrq() }

// Block for `secs` seconds using the hardware counter. Arm the down-counter,
// enable with the IRQ masked, spin on ISTATUS, then disable until re-armed.
func timerWaitSeconds(_ secs: UInt) {
  write_cntp_tval(timerFrequency() * secs)
  write_cntp_ctl(CTL_ENABLE | CTL_IMASK)
  while (read_cntp_ctl() & CTL_ISTATUS) == 0 { nop() }
  write_cntp_ctl(0)
}

// Arm the timer to fire an interrupt after `secs` seconds: load TVAL, enable with IMASK=0.
// (Polled mode used ENABLE|IMASK=0x3; IRQ mode MUST clear IMASK so the interrupt is delivered.)
func timerArmIRQ(_ secs: UInt) {
  write_cntp_tval(timerFrequency() * secs)
  write_cntp_ctl(1)   // ENABLE, IMASK=0
}

