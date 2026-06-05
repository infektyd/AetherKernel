# AetherKernel — Milestone 4 design: async/await on bare metal (cooperative executor)

**Status:** ✅ IMPLEMENTED. Stage 2/3 were hardware-verified 2026-06-04; Runtime V2 shared-CNTP
timer arbitration, Runtime V3 UART shell/control plane, Runtime V4 IRQ-backed UART RX, and
Runtime V5 diagnostics shell were hardware-verified 2026-06-05.
Runtime V6 retained panic/fault records were hardware-verified 2026-06-05.

> ## Runtime V6 ground truth (2026-06-05)
> Panic/fault paths now write a checksum-protected retained record before watchdog reset.
> The record lives at `0x003ff000`, outside the loaded image and just below `HEAP_BASE`.
> Because Runtime V2+ enables cacheable RAM, retained writes and clears must clean the record's
> D-cache lines (`dc cvac` over the struct, then `dsb sy`) before reset; a `dsb` alone was
> hardware-proven insufficient. Hardware proof: `panic-test` rebooted and `retained` reported
> `valid=1 kind=panic reason=panic-test`; `fault-test` rebooted through the sync vector and
> `retained` reported `valid=1 kind=fault esr=0xf20000a5 ... reason=sync-fault`.

> ## Runtime V5 ground truth (2026-06-05)
> The IRQ-backed UART shell now exposes diagnostics commands:
> `diag`, `irqs`, `timers`, `memcheck`, `faults`, `panic-test`, and `fault-test`.
> Safe command proof on hardware returned machine-checkable `diag version=v5`, `irqs total=`,
> `timers now=`, `memcheck ok=1`, and `faults seen=0` lines while `rtv2 fast/slow/long`
> cadences continued. `panic-test` and `fault-test` are destructive diagnostics and are not
> normal liveness checks; Runtime V6 routes them through watchdog reset plus retained records.

> ## Runtime V4 ground truth (2026-06-05)
> PL011 RX is IRQ-backed. UART0 receive/receive-timeout interrupts drain into a fixed C byte
> ring, route through GIC INTID 153 to CPU0, and wake one Swift async shell waiter. The shell no
> longer polls RX every 25 ms; it awaits `uartReadByteAsync()`.

> ## Runtime V3 ground truth (2026-06-05)
> A dedicated async UART shell task polls PL011 RX every 25 ms using `timerSleepMillis`.
> It accepts line commands (`help`, `status`, `heap`, `queues`, `tasks`, `reboot`) and prints
> machine-checkable `key=value` response lines. Runtime stats come from C support APIs for
> heap, executor queues, and timer active clients, plus Swift sleeper/task counters. `r`/`R`
> remain watchdog-reset aliases for the netboot iteration loop.

> ## Runtime V2 ground truth (2026-06-05)
> CNTP is now owned by a shared timer arbiter in `timersleep_hw.c`, with separate clients for
> continuation sleeps and executor delayed jobs. `TimerSleep.swift` uses an 8-slot continuation
> queue (`timerSleepMillis`/`timerSleepSeconds`), and `executor.c` implements
> `swift_task_enqueueGlobalWithDelayImpl` plus `swift_task_enqueueGlobalWithDeadlineImpl` against
> the same arbiter. The live hardware proof is a netbooted image printing independent
> `rtv2 fast`, `rtv2 slow`, and `rtv2 long` cadences.

> ## Resolution: build on the Mach-O path, which ships `_Concurrency`
> `_Concurrency` is NOT built for `aarch64-none-none-elf` (true in both 6.0 and 6.3.2), but IT IS
> built for **`arm64-apple-none-macho`**. We migrated the build to that triple/object-format
> (commit 07de443, branch `feat/macho-concurrency`) and **hardware-re-verified the full MS1–3
> foundation** (boot/UART/timer/GIC IRQ all identical to the ELF kernel) on the real Pi 4. Empirically
> confirmed `import _Concurrency` + `Task{}` compiles for `arm64-apple-none-macho` on swift-6.3.2.
> So async/await is available on a proven base; the executor-hook design below is now buildable.
>
> Build recipe + Mach-O gotchas: see `BRINGUP_PLAYBOOK.md` + the Minni notes. The `[VERIFY]` tags below
> are resolved by the concurrency research (Agent recipe): hooks are `@_cdecl` define-the-symbol (not
> `*_hook` pointers); run jobs via `UnownedJob.runSynchronously(on:)` with a dummy `SerialExecutor`;
> heap is mandatory — provide `malloc`/`free`/`posix_memalign` (a bump allocator is INSUFFICIENT, since
> `Task.sleep` continuations are freed); `Task.sleep(nanoseconds:)` → `enqueueGlobalWithDelay` (ns) →
> `CNTP_CVAL_EL0`; `wfi` wakes on a pending IRQ even with `PSTATE.I` masked (race-free drain).
>
> CORRECTIONS to fold in when implementing: prefer `Task.sleep(nanoseconds:)` for the demo (routes
> through the delay hook we implement; the research's deadline-hook "enqueue immediately" fallback would
> NOT actually delay a `ContinuousClock` sleep). Watch the executor's own allocations — the research used
> Swift `Array` queues (which allocate); keep enqueue paths simple and the allocator reentrancy-safe
> (enqueue runs in task context, the timer IRQ only matures the delay queue → wakes `wfi`).
>
> ## GROUND TRUTH (2026-06-04) — `nm` + `ExecutorImpl.h` from our actual 6.3.2 toolchain
> Verified empirically against `usr/lib/swift/embedded/arm64-apple-none-macho/libswift_Concurrency.a`,
> `libswift_ConcurrencyDefaultExecutor.a`, and `usr/include/swift/ExecutorImpl.h`. **Three corrections to
> the original plan below — the §2/§3b text under them is superseded by this block:**
> 1. **Define the `…Impl` symbols, NOT the public trampolines.** `swift_task_enqueueGlobal` /
>    `…WithDelay` / `asyncMainDrainQueue` / `enqueueMainExecutor` are already **defined (T)** in
>    `libswift_Concurrency.a`; redefining them = duplicate-symbol link error. The real seam is the
>    `…Impl` set, which `libswift_ConcurrencyDefaultExecutor.a` defines. So: **do NOT link
>    DefaultExecutor.a**, and provide the `…Impl` functions ourselves.
> 2. **Write the executor in C, not Swift `@_cdecl`.** The Impl functions are `SWIFT_CC(swift)`
>    (`__attribute__((swiftcall))`); `@_cdecl` emits the C convention → ABI mismatch. `ExecutorImpl.h`
>    is explicitly "the declarations you need to write a custom global executor in plain C." → new file
>    `Sources/Support/executor.c` that `#include <swift/ExecutorImpl.h>` and implements the contract with
>    the header's own macros. Run jobs via the header's inline `swift_job_run(job, swift_executor_generic())`
>    (→ `_swift_job_run_c`, defined in the archive — we call it, don't provide it).
> 3. **`swift_slowAlloc`/`swift_slowDealloc` are also required** (true externals alongside `malloc`/`free`).
>    Stage 1's `malloc`/`free` is the right base; add thin `swift_slowAlloc`(→`posix_memalign`/`malloc`)
>    / `swift_slowDealloc`(→`free`) shims.
>
> **Contract to implement (exact signatures from `ExecutorImpl.h`):**
> ```c
> #include <swift/ExecutorImpl.h>
> SWIFT_CC(swift) void swift_task_enqueueGlobalImpl(SwiftJob *job);                       // push ready ring
> SWIFT_CC(swift) void swift_task_enqueueGlobalWithDelayImpl(SwiftJobDelay delayNs, SwiftJob *job); // delay FIRST, ns
> SWIFT_CC(swift) void swift_task_enqueueMainExecutorImpl(SwiftJob *job);                 // == enqueueGlobal (1 thread)
> SWIFT_CC(swift) SwiftExecutorRef swift_task_getMainExecutorImpl(void);                  // return swift_executor_generic()
> SWIFT_CC(swift) bool  swift_task_isMainExecutorImpl(SwiftExecutorRef e);                // return true
> SWIFT_CC(swift) void  swift_task_checkIsolatedImpl(SwiftExecutorRef e);                 // no-op
> SWIFT_CC(swift) int8_t swift_task_isIsolatingCurrentContextImpl(SwiftExecutorRef e);    // return 1 (isolated)
> SWIFT_RUNTIME_ATTRIBUTE_NORETURN SWIFT_CC(swift) void swift_task_asyncMainDrainQueueImpl(void); // THE PUMP
> SWIFT_CC(swift) void swift_task_enqueueGlobalWithDeadlineImpl(long long s,long long ns,long long ts,long long tns,int clk,SwiftJob*); // route to delay queue or enqueue if due
> SWIFT_CC(swift) void swift_task_donateThreadToGlobalExecutorUntilImpl(bool(*cond)(void*),void*ctx);      // dummy/assert (optional)
> // run a job: swift_job_run(job, swift_executor_generic());   // inline in the header → _swift_job_run_c
> // SwiftJobDelay = unsigned long long (ns). Job priority via swift_job_getPriority(job) if we want priority ordering.
> ```
> The pump (`asyncMainDrainQueueImpl`): loop { pop a ready job → `swift_job_run(job, generic)`; when ready
> ring empty → promote delay-queue jobs whose deadline ≤ now into ready (also done from the timer IRQ),
> arm `CNTP` for the next deadline, `wfi` if nothing due }. Ready/delay queues are fixed C arrays guarded by
> `irq_save()/irq_restore()` (IRQ matures the delay queue → wakes `wfi`). Still prefer `Task.sleep(nanoseconds:)`
> for the demo so we hit `WithDelayImpl` (ns, no clock dependency) rather than the deadline/clock path.

## 0. Goal
Run real Swift `async/await` on the metal: a single-threaded **cooperative executor** whose time
source is the GIC-400 timer IRQ we already have. Historical Stage 3 payoff demo — a one-task heartbeat:
```swift
func asyncMain() async {
  var n: UInt64 = 0
  while true {
    uartPuts("rtv2 slow ")             // StaticString — NOT interpolation (Embedded)
    uartPutHex(n)
    uartPuts("\n")
    try? await Task.sleep(for: .seconds(1))   // suspends; CPU sleeps in wfi
    n &+= 1
  }
}
```
Current Runtime V2 success on hardware = independent `rtv2 fast/slow/long` cadences with the CPU
**idle in `wfi` between jobs**, woken when the timer IRQ matures continuation sleeps or executor delays.
Structured concurrency, hardware-timer-backed.

## 1. Why this is more than the polled/IRQ heartbeat
Milestone 3 was an IRQ that prints. This is the IRQ **resuming a suspended `async` task** — i.e. the
Swift concurrency runtime actually scheduling work on bare metal. Requires us to supply the runtime's
executor + allocator hooks that an OS would normally provide.

## 2. The contract (from the Embedded `_Concurrency` module — names confirmed, signatures `[VERIFY]`)
The stdlib leaves these to us; with `--unresolved-symbols=ignore-in-object-files` they link to nothing
unless we define them, then crash on first use. We MUST provide:
- `swift_task_enqueueGlobal(job)` — push a ready job onto the run queue.
- `swift_task_enqueueGlobalWithDelay(ns, job)` — schedule after a delay (backs `Task.sleep`). `[VERIFY units = ns]`
- `swift_task_enqueueGlobalWithDeadline(...)` — absolute-deadline variant (ContinuousClock). `[VERIFY if needed]`
- `swift_task_asyncMainDrainQueue()` — the main pump. `[VERIFY: do we implement it, or write our own loop + bootstrap a Task?]`

`[VERIFY]` (research): exact symbol spelling + C/Swift signatures; **define-the-function** vs
**set a `*_hook` pointer**; how to *run* a job (`UnownedJob.runSynchronously(on:)` — and which
`UnownedSerialExecutor` ref with no real executor object — vs a C `swift_job_run`); whether `@main
static func main() async` works in 6.0 Embedded or we bootstrap a `Task{}` from sync `main` then drain;
whether `SerialExecutor`/`TaskExecutor` conformance (the Grok stub approach) is valid in Embedded or the
C-hook path is the only one (suspected: C-hooks only).

## 3. Components

### 3a. Minimal allocator — `Sources/Support/alloc.c` (NEW) `[VERIFY which symbols]`
Swift `Task`s allocate. With no OS heap we provide one over a static arena (in BSS):
```c
static unsigned char heap_arena[256 * 1024] __attribute__((aligned(16)));
// provide whichever the runtime calls: posix_memalign/free, malloc/free, OR swift_slowAlloc/Dealloc
```
Start with a **bump allocator** (fast, never frees) to get a demo booting; if task slab churn needs it,
upgrade to a fixed-block free-list. Single-allocation-context (tasks allocate in task context, the IRQ
only pushes job pointers — no alloc in IRQ), so no locking needed for the bump path. `[VERIFY task
alloc/dealloc pattern + required symbol set + arena size]`

### 3b. Executor — `Sources/Application/Executor.swift` (NEW)
- **Run queue:** fixed ring buffer of job handles (e.g. 64), no alloc.
- `@_cdecl("swift_task_enqueueGlobal")` (or hook): push job. **Called from both task and IRQ context →
  wrap push/pop in a DAIF critical section** (`irq_save()`/`irq_restore()` helpers, below).
- **Drain/pump:** pop → run job → repeat; when run queue empty AND no due timers → `wfi`.
- **Timer queue:** small array of `(deadlineTicks, job)` for delayed jobs. `enqueueGlobalWithDelay`
  converts ns→CNTP ticks (`ticks = ns * CNTFRQ / 1_000_000_000`, CNTFRQ=54 MHz) and inserts; arm
  `CNTP_TVAL` for the nearest deadline.

### 3c. Timer IRQ becomes the scheduler tick — edit `IRQHandler.swift`
On INTID 30: move every due `(deadline ≤ now)` job from the timer queue into the run queue
(`enqueueGlobal`), then re-arm `CNTP` for the next-nearest deadline (or leave disabled if none).
Re-arm BEFORE EOI (level-triggered, as established). This wakes the `wfi` in the drain loop.

### 3d. Critical-section helpers — `Support.h` (append)
```c
static inline unsigned long irq_save(void){ unsigned long f; __asm__ volatile("mrs %0,daif; msr daifset,#2":"=r"(f)::"memory"); return f; }
static inline void irq_restore(unsigned long f){ __asm__ volatile("msr daif,%0"::"r"(f):"memory"); }
```
(Replaces bare `irq_enable` for queue ops so an IRQ can't corrupt a half-updated ring.)

### 3e. Bootstrap — edit `Application.swift`
Sync `main`: `uartInit` → banner → `gicInitTimerIRQ` → allocator init → start the first task
(`Task { await asyncMain() }` or detached) → `irq_enable()` → enter drain pump.
`[VERIFY exact bootstrap shape depending on async-main support]`

## 4. Embedded Swift gotchas (carry forward + new)
- **No string interpolation** in the demo — `uartPuts(StaticString)` + `uartPutHex`. (Interpolation
  allocates / may be unavailable.)
- Handlers/executor entry stay **integer-only** (no FP/SIMD → CPACR). Disassembly-check.
- Globals: `nonisolated(unsafe)` or encapsulate queue state in a C struct.
- `swift_beginAccess` = no-op `ret` stub (already confirmed).

## 5. Ranked risks
1. **Heap/allocator**: wrong symbol set or too-small arena ⇒ Task creation traps/hangs. Prime unknown.
2. **Job-run ABI**: calling the wrong run primitive / bad executor ref ⇒ crash on first job.
3. **async-main support in 6.0 Embedded**: if absent, need the manual bootstrap+drain (have a plan).
4. **Queue reentrancy**: IRQ enqueues while task pops ⇒ must use the DAIF critical section.
5. **Task.sleep/Clock availability** in Embedded 6.0 ⇒ may need a custom suspension primitive instead.

## 6. Verification plan
- Independent rebuild; `git diff` additive; **disassembly-verify** the hook symbols are defined
  (`swift_task_enqueueGlobal` etc. present, not unresolved), the run-queue critical section masks IRQ,
  the timer→run-queue handoff, allocator integer-only, no FP in the executor.
- Hardware: `rtv2 fast/slow/long` cadences, `wfi` idle between jobs (serial cadence is the liveness signal).
- Fallback if `Task.sleep` is unavailable in 6.0 Embedded: implement a custom awaitable backed directly
  by the timer IRQ (a continuation the IRQ resumes), proving the executor without depending on Clock.
