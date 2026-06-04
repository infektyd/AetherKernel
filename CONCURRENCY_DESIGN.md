# AetherKernel — Milestone 4 design: async/await on bare metal (cooperative executor)

**Status:** BLOCKED on toolchain (empirically confirmed 2026-06-04). The rest of this design is sound
but cannot be built on `swift-6.0-RELEASE`.

> ## ⛔ BLOCKER: `_Concurrency` is not shipped for `aarch64-none-none-elf` in swift-6.0-RELEASE
> Probe (`import _Concurrency` / `Task {}`) fails: the toolchain's Embedded `_Concurrency.swiftmodule`
> only contains **macOS triples** (`arm64-apple-macos`, `arm64e-apple-macos`, `x86_64-apple-macos`) —
> **no `aarch64-none-none-elf` variant**. (`Swift` core and `Synchronization` *do* have it — that's why
> the kernel builds.) So `async/await`/`Task` cannot even be imported for our bare-metal target on 6.0;
> the executor-hook approach below is moot until the module exists for our triple.
>
> **Paths:** (A) upgrade to a newer toolchain (6.1+/main snapshot) where Embedded concurrency was ported
> to bare-metal triples — the real route to the async vision; requires a toolchain download + rebuilding
> the verified UART/timer/GIC kernel on it. (B) hand-rolled cooperative scheduler on 6.0 (our own task
> type + run loop, NOT Swift async/await) — achievable now but not "the Embedded-Swift concurrency
> experiment." (C) await research for a 6.0 workaround (unlikely — the module is simply absent).

The architecture below assumes a toolchain where Embedded `_Concurrency` targets our triple. `[VERIFY]`
items await the research report; once a toolchain provides the module, this becomes the Gemini spec.

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
