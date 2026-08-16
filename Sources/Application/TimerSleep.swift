//===----------------------------------------------------------------------===//
// Timer-backed async sleep. Task.sleep is unavailable in Embedded Swift, so we
// build suspension from withUnsafeContinuation + the CNTP timer IRQ.
//
// Runtime V2: this file owns the SLEEP client of the shared CNTP arbiter.
// TimerSleep owns all timed wakeups; executor delay/deadline hooks panic and
// the EXECUTOR CNTP slot is never armed. Swift stores/resumes continuations;
// all load-bearing CNTP register work stays in non-inline C.
//===----------------------------------------------------------------------===//
import Support
import _Concurrency

let TIMER_SLEEP_CAPACITY: Int = 8
private let TIMER_CLIENT_SLEEP: UInt32 = 0

nonisolated(unsafe) var sleeper0Cont: UnsafeContinuation<Void, Never>? = nil
nonisolated(unsafe) var sleeper1Cont: UnsafeContinuation<Void, Never>? = nil
nonisolated(unsafe) var sleeper2Cont: UnsafeContinuation<Void, Never>? = nil
nonisolated(unsafe) var sleeper3Cont: UnsafeContinuation<Void, Never>? = nil
nonisolated(unsafe) var sleeper4Cont: UnsafeContinuation<Void, Never>? = nil
nonisolated(unsafe) var sleeper5Cont: UnsafeContinuation<Void, Never>? = nil
nonisolated(unsafe) var sleeper6Cont: UnsafeContinuation<Void, Never>? = nil
nonisolated(unsafe) var sleeper7Cont: UnsafeContinuation<Void, Never>? = nil

nonisolated(unsafe) var sleeper0Deadline: UInt = 0
nonisolated(unsafe) var sleeper1Deadline: UInt = 0
nonisolated(unsafe) var sleeper2Deadline: UInt = 0
nonisolated(unsafe) var sleeper3Deadline: UInt = 0
nonisolated(unsafe) var sleeper4Deadline: UInt = 0
nonisolated(unsafe) var sleeper5Deadline: UInt = 0
nonisolated(unsafe) var sleeper6Deadline: UInt = 0
nonisolated(unsafe) var sleeper7Deadline: UInt = 0

func getSleeperCont(_ slot: Int) -> UnsafeContinuation<Void, Never>? {
  switch slot {
  case 0: return sleeper0Cont
  case 1: return sleeper1Cont
  case 2: return sleeper2Cont
  case 3: return sleeper3Cont
  case 4: return sleeper4Cont
  case 5: return sleeper5Cont
  case 6: return sleeper6Cont
  case 7: return sleeper7Cont
  default: return nil
  }
}

func getSleeperDeadline(_ slot: Int) -> UInt {
  switch slot {
  case 0: return sleeper0Deadline
  case 1: return sleeper1Deadline
  case 2: return sleeper2Deadline
  case 3: return sleeper3Deadline
  case 4: return sleeper4Deadline
  case 5: return sleeper5Deadline
  case 6: return sleeper6Deadline
  case 7: return sleeper7Deadline
  default: return 0
  }
}

func setSleeper(_ slot: Int, _ c: UnsafeContinuation<Void, Never>?, _ deadline: UInt) {
  switch slot {
  case 0: sleeper0Cont = c; sleeper0Deadline = deadline
  case 1: sleeper1Cont = c; sleeper1Deadline = deadline
  case 2: sleeper2Cont = c; sleeper2Deadline = deadline
  case 3: sleeper3Cont = c; sleeper3Deadline = deadline
  case 4: sleeper4Cont = c; sleeper4Deadline = deadline
  case 5: sleeper5Cont = c; sleeper5Deadline = deadline
  case 6: sleeper6Cont = c; sleeper6Deadline = deadline
  case 7: sleeper7Cont = c; sleeper7Deadline = deadline
  default: break
  }
}

func clearSleeper(_ slot: Int) {
  setSleeper(slot, nil, 0)
}

func firstFreeSleeperSlot() -> Int {
  var i = 0
  while i < TIMER_SLEEP_CAPACITY {
    if getSleeperCont(i) == nil {
      return i
    }
    i += 1
  }
  return -1
}

func timerSleepPendingCount() -> UInt {
  let flags = irq_save()
  var count: UInt = 0
  var i = 0
  while i < TIMER_SLEEP_CAPACITY {
    if getSleeperCont(i) != nil {
      count += 1
    }
    i += 1
  }
  irq_restore(flags)
  return count
}

func timerSleepCapacity() -> UInt {
  UInt(TIMER_SLEEP_CAPACITY)
}

func rearmSleepClientFromQueue() {
  var haveDeadline = false
  var minDeadline: UInt = 0
  var i = 0

  while i < TIMER_SLEEP_CAPACITY {
    if getSleeperCont(i) != nil {
      let d = getSleeperDeadline(i)
      if !haveDeadline || d < minDeadline {
        haveDeadline = true
        minDeadline = d
      }
    }
    i += 1
  }

  if haveDeadline {
    kernel_timer_set_deadline(TIMER_CLIENT_SLEEP, minDeadline)
  } else {
    kernel_timer_clear_deadline(TIMER_CLIENT_SLEEP)
  }
}

func sleepTicksForMillis(_ ms: UInt64) -> UInt {
  var ticks = (UInt64(timerFrequency()) &* ms) / 1000
  if ticks == 0 {
    ticks = 1
  }
  return UInt(ticks)
}

// Suspend the current async task for `ms` milliseconds of CNTP time.
func timerSleepMillis(_ ms: UInt64) async {
  await withUnsafeContinuation { (c: UnsafeContinuation<Void, Never>) in
    let flags = irq_save()
    let slot = firstFreeSleeperSlot()

    if slot < 0 {
      irq_restore(flags)
      uartPuts("TIMER SLEEP PANIC: sleep queue overflow\n")
      while true { wait_for_interrupt() }
    }

    let deadline = kernel_timer_now() &+ sleepTicksForMillis(ms)
    setSleeper(slot, c, deadline)
    rearmSleepClientFromQueue()
    irq_restore(flags)
  }
}

// Suspend the current async task for `secs` seconds of CNTP time.
func timerSleepSeconds(_ secs: UInt64) async {
  await timerSleepMillis(secs &* 1000)
}

// Called from the CNTP timer IRQ (INTID 30). Clear/re-arm the shared timer before
// resuming continuations so the level IRQ is de-asserted before GIC EOI.
func serviceTimerSleepers() {
  var due0: UnsafeContinuation<Void, Never>? = nil
  var due1: UnsafeContinuation<Void, Never>? = nil
  var due2: UnsafeContinuation<Void, Never>? = nil
  var due3: UnsafeContinuation<Void, Never>? = nil
  var due4: UnsafeContinuation<Void, Never>? = nil
  var due5: UnsafeContinuation<Void, Never>? = nil
  var due6: UnsafeContinuation<Void, Never>? = nil
  var due7: UnsafeContinuation<Void, Never>? = nil

  let flags = irq_save()
  let now = kernel_timer_now()
  var i = 0

  while i < TIMER_SLEEP_CAPACITY {
    if let c = getSleeperCont(i) {
      if getSleeperDeadline(i) <= now {
        clearSleeper(i)
        switch i {
        case 0: due0 = c
        case 1: due1 = c
        case 2: due2 = c
        case 3: due3 = c
        case 4: due4 = c
        case 5: due5 = c
        case 6: due6 = c
        case 7: due7 = c
        default: break
        }
      }
    }
    i += 1
  }

  rearmSleepClientFromQueue()
  irq_restore(flags)

  if let c = due0 { c.resume() }
  if let c = due1 { c.resume() }
  if let c = due2 { c.resume() }
  if let c = due3 { c.resume() }
  if let c = due4 { c.resume() }
  if let c = due5 { c.resume() }
  if let c = due6 { c.resume() }
  if let c = due7 { c.resume() }
}
