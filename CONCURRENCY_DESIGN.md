# AetherKernel — Milestone 4 design: async/await on bare metal (cooperative executor)

**Status:** ✅ UNBLOCKED — ready to implement. (Resolved 2026-06-04.)

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

## 0. Goal
Run real Swift `async/await` on the metal: a single-threaded **cooperative executor** whose time
source is the GIC-400 timer IRQ we already have. Payoff demo — an async heartbeat:
```swift
func asyncMain() async {
  var n: UInt64 = 0
  while true {
    uartPuts("async tick ")            // StaticString — NOT interpolation (Embedded)
    uartPutHex(n)
    uartPuts("\n")
    try? await Task.sleep(for: .seconds(1))   // suspends; CPU sleeps in wfi
    n &+= 1
  }
}
```
Success on hardware = `async tick N` ~1 s apart with the CPU **idle in `wfi` between ticks**, woken
only when the timer IRQ fires the suspended continuation. Structured concurrency, hardware-timer-backed.

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
- Hardware: `async tick N` ~1.0 s apart, `wfi` idle between (scope the LED / serial cadence).
- Fallback if `Task.sleep` is unavailable in 6.0 Embedded: implement a custom awaitable backed directly
  by the timer IRQ (a continuation the IRQ resumes), proving the executor without depending on Clock.
