# AetherKernel — MS4 Stage 2 handoff: cooperative executor (C)

**Implementer:** Gemini drafts `executor.c` + the alloc shims. claude-code integrates the build/link,
runs the link-probe, wires the IRQ handler + bootstrap, disassembly-verifies, and hardware-tests.
**Branch:** `feat/macho-concurrency`. **Build:** `./build.sh` (swift-6.3.2, `arm64-apple-none-macho`,
`--toolset Toolsets/rpi4-macho.json`, `macho2bin.py`). **Stage 1 (allocator) is hardware-verified.**

## 0. Goal of Stage 2
Provide the Swift concurrency runtime's *global executor* so a single `Task {}` can be created and run
to completion on bare metal. NOT YET the timer/sleep demo (that's Stage 3) — Stage 2 just proves the
executor drains one ready job. The executor is a **single-threaded cooperative** scheduler whose only
"thread" is the boot CPU spinning the drain pump.

## 1. GROUND TRUTH — verified contract (do not deviate; this was checked against the real toolchain)
Source of truth: `$TOOLCHAIN/usr/include/swift/ExecutorImpl.h` (include it directly) and `nm` of
`libswift_Concurrency.a` / `libswift_ConcurrencyDefaultExecutor.a`.

- We **do NOT link** `libswift_ConcurrencyDefaultExecutor.a`. We provide the `…Impl` functions it would.
- We **do NOT** define the public trampolines (`swift_task_enqueueGlobal`, etc.) — they are already
  defined in `libswift_Concurrency.a`. Define ONLY the `…Impl` variants.
- The `…Impl` functions are **`SWIFT_CC(swift)`** (swiftcall). **Write them in C** using the header's
  macros — NOT Swift `@_cdecl` (that emits the C convention = ABI mismatch).
- Run a job via the header's inline helper: `swift_job_run(job, swift_executor_generic());`
  (it forwards to `_swift_job_run_c`, which IS defined in the archive — we only call it).
- `SwiftJobDelay` is `unsigned long long` **nanoseconds**.

## 2. File to create: `Sources/Support/executor.c`
Plain C, matching the existing `Support.h` shim style. Begin with:
```c
#include <swift/ExecutorImpl.h>   // SwiftJob, SwiftExecutorRef, SWIFT_CC, swift_job_run, swift_executor_generic
#include "Support.h"              // irq_save/irq_restore, read_cntfrq/read_cntpct, write_cntp_tval/_ctl, wait_for_interrupt
```
`size_t`/`uint64_t` etc. come from `<stdlib.h>`/`<inttypes.h>` (pulled in by the header). Integer-only,
no FP/SIMD.

### 2a. Queues (fixed, no heap — these run in IRQ context too)
- **Ready ring:** `static SwiftJob *ready[64];` with head/tail indices, FIFO. (Capacity 64 is plenty for
  one task; document that overflow is a fatal condition — call the panic in §2e, don't silently drop.)
- **Delay queue:** `static struct { unsigned long long deadlineTicks; SwiftJob *job; } delayed[32];`
  plus a count. Unordered array is fine (we linear-scan for the minimum). Document overflow = panic.
- **All push/pop/scan must be wrapped in `irq_save()` / `irq_restore()`** — the timer IRQ (§2d) mutates
  the delay queue and pushes to the ready ring concurrently with the pump popping.

### 2b. The `…Impl` functions to define (exact signatures from ExecutorImpl.h)
```c
SWIFT_CC(swift) void swift_task_enqueueGlobalImpl(SwiftJob *job);                 // push job to ready ring
SWIFT_CC(swift) void swift_task_enqueueMainExecutorImpl(SwiftJob *job);           // single-threaded -> same as enqueueGlobal
SWIFT_CC(swift) void swift_task_enqueueGlobalWithDelayImpl(SwiftJobDelay delayNs, SwiftJob *job);
                  // deadlineTicks = read_cntpct() + ns_to_ticks(delayNs); push to delay queue; re-arm timer (§2d arm helper)
SWIFT_CC(swift) void swift_task_enqueueGlobalWithDeadlineImpl(long long sec,long long nsec,
                  long long tsec,long long tnsec,int clock,SwiftJob *job);        // Stage 2: convert (sec,nsec) absolute-ish
                  // to a delay vs swift_time_now and route to the delay queue; OR for now call the panic — we will use
                  // Task.sleep(nanoseconds:) which hits WithDelayImpl. Implement as a thin "treat as delay of max(0, target-now)" if easy, else panic with a clear message.
SWIFT_CC(swift) SwiftExecutorRef swift_task_getMainExecutorImpl(void);            // return swift_executor_generic()
SWIFT_CC(swift) bool   swift_task_isMainExecutorImpl(SwiftExecutorRef e);         // return true
SWIFT_CC(swift) void   swift_task_checkIsolatedImpl(SwiftExecutorRef e);          // no-op (always isolated, single thread)
SWIFT_CC(swift) int8_t swift_task_isIsolatingCurrentContextImpl(SwiftExecutorRef e); // return 1 (isolated)
SWIFT_CC(swift) void   swift_task_donateThreadToGlobalExecutorUntilImpl(bool (*cond)(void*), void *ctx);
                  // optional: simplest correct impl = run the drain loop body until cond(ctx) returns true, then return.
                  // If that's awkward, panic with a clear message (we don't expect to hit it in the demo).
SWIFT_RUNTIME_ATTRIBUTE_NORETURN SWIFT_CC(swift) void swift_task_asyncMainDrainQueueImpl(void); // THE PUMP, see §2c
```

### 2c. The pump — `swift_task_asyncMainDrainQueueImpl` (NORETURN)
```
for (;;) {
    promote_due_jobs();                 // move delayed[] with deadlineTicks <= read_cntpct() into ready ring (irq_save guarded)
    SwiftJob *job = ready_pop();        // irq_save guarded; NULL if empty
    if (job) { swift_job_run(job, swift_executor_generic()); continue; }
    arm_next_deadline();                // if delay queue non-empty, set CNTP TVAL to (min_deadline - now); else leave timer masked
    wait_for_interrupt();               // wfi: wakes on the timer IRQ (or any pending IRQ) even with I masked
}
```
Never returns (mark with the header's NORETURN). The CPU idles in `wfi` between jobs.

### 2d. Timer interface for the IRQ handler (claude wires the Swift side)
Expose **one** C entry the existing Swift IRQ handler will call on INTID 30:
```c
void executor_on_timer_irq(void);   // promote due delayed jobs into ready ring; re-arm CNTP TVAL to next deadline (or mask if none)
```
Plus the arming helper used by both the pump and enqueueWithDelay:
- `ns_to_ticks(ns)`: `(ns * (unsigned long long)read_cntfrq()) / 1000000000ULL`. Note the overflow bound:
  ns·54e6 fits in u64 for delays up to ~170 s — fine; add a one-line comment.
- Arm via the existing `write_cntp_tval(tval)` + `write_cntp_ctl(1)` (enable, IMASK clear) from Support.h.
  When no delayed jobs remain, mask the timer (`write_cntp_ctl(0)` or set IMASK) so we don't get spurious ticks.
Do **not** call `gicAck`/`gicEoi` from C — the Swift handler owns GIC ack/EOI; `executor_on_timer_irq`
only touches CNTP + the queues.

### 2e. Panic helper
`static void executor_panic(const char *msg)` — print `msg` over UART then spin (`for(;;) wait_for_interrupt();`).
Use `uart_puts`-equivalent. If there's no C-visible UART symbol, declare `extern void uart_puts(const char*);`
and NOTE it in your changelog so claude can confirm the symbol name (the Swift UART is `uartPuts`; a C shim
may be needed). Prefer to FLAG this rather than guess.

## 3. Allocator shims — append to `Sources/Support/alloc.c` (NEW functions, additive)
The runtime calls these (verified undefined externals). Wrap our Stage-1 heap:
```c
void *swift_slowAlloc(size_t size, size_t alignMask) {
    // alignMask is (alignment-1), or ~0 / 0 meaning "default". Default align = 16 (our malloc already 16-aligns).
    if (alignMask == (size_t)-1 || alignMask <= 15) return malloc(size);
    void *p = NULL; if (posix_memalign(&p, alignMask + 1, size) != 0) return NULL; return p;
}
void swift_slowDealloc(void *ptr, size_t size, size_t alignMask) { (void)size; (void)alignMask; free(ptr); }
```
Confirm the exact signature against `ExecutorImpl.h`/runtime headers if visible; the above is the standard
Embedded shim. Do NOT add `swift_task_alloc`/`swift_job_alloc` — those are defined by the archive.

## 4. What you do NOT touch (claude owns these)
- `build.sh` / `Toolsets/rpi4-macho.json` (adding `libswift_Concurrency.a`, the `-undefined`/dead-strip flags, excluding DefaultExecutor) — claude does the link integration + a **link-probe** to find the exact residual unresolved symbols. Do not guess at libc stubs (`memset`/`puts`/`clock_gettime`/etc.) — claude resolves those from the linker output.
- `Sources/Application/IRQHandler.swift` (calling `executor_on_timer_irq`) — claude wires it.
- `Sources/Application/Application.swift` bootstrap (`Task {}` + entering the drain) — claude owns the Embedded-specific incantation.
- `boot.S`, `vectors.S`, UART/GPIO/Timer/GIC drivers.

## 5. Deliverable
- Complete contents of `Sources/Support/executor.c`.
- The two new functions appended to `alloc.c` (show them as an additive diff/block).
- A changelog: every symbol you reference as `extern` that isn't in `ExecutorImpl.h`/`Support.h` (esp. the
  UART symbol for the panic), and any signature you were unsure about. **Do NOT claim it builds or links** —
  you cannot (the build integration is claude's). Just produce correct, well-commented C against the contract above.
- Integer-only, no FP. Match the existing `Support.h` comment density and style.
