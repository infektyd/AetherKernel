# AetherKernel — MS4 Stage 3 (REVISED): custom timer-backed async sleep

**Why revised:** `Task.sleep(nanoseconds:)` is UNAVAILABLE in Embedded Swift (compiler error
"unavailable in embedded Swift"). So we build our own timer-backed suspension using
`withUnsafeContinuation` (confirmed to compile in Embedded), resumed from the CNTP timer IRQ.

**Goal unchanged:** `async tick N` printed ~1 s apart, CPU idle in `wfi` between ticks, the timer IRQ
resuming a suspended async continuation. Single concurrent sleeper is fine (one heartbeat task).

Working dir: the AetherKernel repo. Make THREE changes: create one file, edit two.

## Mechanism
`timerSleepSeconds(s)` suspends via `withUnsafeContinuation`, stashing the continuation + an absolute
CNTP deadline in single-slot globals and arming CNTP. When CNTP fires, the Swift IRQ handler calls
`serviceTimerSleeper()`, which disables CNTP (de-asserting the level IRQ before EOI), then `resume()`s
the continuation. `resume()` enqueues the continuation on our executor (the existing
`swift_task_enqueueGlobalImpl` → ready ring), so the drain pump runs it on the next loop.

## File 1 — CREATE `Sources/Application/TimerSleep.swift`
```swift
//===----------------------------------------------------------------------===//
// Timer-backed async sleep. Task.sleep is unavailable in Embedded Swift, so we
// build suspension from withUnsafeContinuation + the CNTP timer IRQ. Single slot:
// supports one concurrent sleeper (the heartbeat task), which is all we need.
//===----------------------------------------------------------------------===//
import Support

// Touched in task context (under irq_save) and in IRQ context (serviceTimerSleeper).
nonisolated(unsafe) var sleeperCont: UnsafeContinuation<Void, Never>? = nil
nonisolated(unsafe) var sleeperDeadline: UInt64 = 0

// Suspend the current async task for `secs` seconds of CNTP time.
func timerSleepSeconds(_ secs: UInt64) async {
  await withUnsafeContinuation { (c: UnsafeContinuation<Void, Never>) in
    let flags = irq_save()
    let now = read_cntpct()
    sleeperCont = c
    sleeperDeadline = now &+ (UInt64(read_cntfrq()) &* secs)
    let tval = sleeperDeadline > now ? sleeperDeadline &- now : 1
    write_cntp_tval(UInt(tval))
    write_cntp_ctl(1)            // ENABLE, IMASK=0 -> IRQ delivered when it fires
    irq_restore(flags)
  }
}

// Called from the CNTP timer IRQ (INTID 30) in IRQ context. Disables the timer
// (which de-asserts the level IRQ before the handler's EOI), then resumes the
// sleeper if its deadline has passed.
func serviceTimerSleeper() {
  let now = read_cntpct()
  if let c = sleeperCont, now >= sleeperDeadline {
    sleeperCont = nil
    write_cntp_ctl(0)            // disable CNTP -> de-assert IRQ line
    c.resume()                   // enqueues the continuation on our executor
  } else if sleeperCont != nil {
    // Not yet due (shouldn't happen — we armed to the exact deadline): re-arm.
    let tval = sleeperDeadline > now ? sleeperDeadline &- now : 1
    write_cntp_tval(UInt(tval))
    write_cntp_ctl(1)
  } else {
    write_cntp_ctl(0)            // nothing waiting
  }
}
```
Note the Support.h C helpers used: `irq_save`/`irq_restore`, `read_cntpct`/`read_cntfrq`,
`write_cntp_tval`/`write_cntp_ctl` (all already declared; `read_cntfrq`/`read_cntpct` return `UInt`,
`write_cntp_tval` takes `UInt`). Use `UInt64` for the deadline math and convert as shown.

## File 2 — EDIT `Sources/Application/IRQHandler.swift`
In the `intid == 30` branch, call `serviceTimerSleeper()` (NOT `executor_on_timer_irq()`). Everything
else (gicAck, the 1022/1023 spurious check, gicEoi) stays exactly as-is:
```swift
  if intid == 30 {                         // CNTP timer
    serviceTimerSleeper()                  // resume the due sleeper; de-asserts the IRQ before EOI
  }
```

## File 3 — EDIT `Sources/Application/Application.swift`
The heartbeat task currently has a placeholder line
`await withUnsafeContinuation { (c: UnsafeContinuation<Void, Never>) in c.resume() }`.
Replace THAT one line with:
```swift
        await timerSleepSeconds(1)
```
Leave the rest of the Stage-3 bootstrap (gicInitTimerIRQ, the Task with uartPuts/uartPutHex, irq_enable,
swift_task_asyncMainDrainQueue) exactly as it is. Also fix the banner string on the line above the Task
from "(Task.sleep 1s, wfi idle)" to "(timer-backed sleep 1s, wfi idle)".

## Constraints / deliverable
- Create `Sources/Application/TimerSleep.swift`; edit ONLY `IRQHandler.swift` and `Application.swift`.
  Do NOT touch any .c file, boot.S, vectors.S, Support.h, GIC.swift, Timer.swift, UART.swift, GPIO.swift,
  build.sh, or Toolsets/.
- Embedded Swift: no string interpolation; integer-only; use `&+`/`&*`/`&-` for the wrapping arithmetic
  as shown.
- Run `git status` before/after. Print a short CHANGELOG. Do NOT claim it builds — the reviewer builds,
  disassembly-verifies, and flashes.
