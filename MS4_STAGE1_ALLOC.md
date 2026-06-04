# AetherKernel — MS4 Stage 1 handoff: heap allocator

**Implementer:** Gemini drafts; claude-code reviews + disassembly-verifies + hardware-tests.
**Branch:** `feat/macho-concurrency` (Mach-O foundation hardware-verified). **Build:** `./build.sh`
(swift-6.3.2, `arm64-apple-none-macho`, `--toolset Toolsets/rpi4-macho.json`, `macho2bin.py`).

## 0. Goal
Provide a minimal but **correct** C heap (`malloc`/`free`/`posix_memalign`/`calloc`/`realloc`) so the
Embedded Swift concurrency runtime can allocate `Task`s and continuations in later stages. A bump
allocator is INSUFFICIENT — `Task.sleep` frees continuations, so `free` must actually reclaim.
This stage ends with a **hardware self-test**: allocate/free a few blocks over UART and prove it works,
before any executor is layered on.

## 1. Heap location — a fixed RAM window (NOT a BSS array)
Do NOT put the arena in a static array: `-no_zero_fill_sections` would materialize it as zeros in
`kernel8.img`, bloating the image by the arena size. Instead use a fixed RAM window above the kernel:
```
HEAP_BASE = 0x0040_0000   // 4 MiB mark — well above the kernel image (0x80000..~0x8C000)
                          // and the stack (grows down from 0x80000); below any GPU reserve.
HEAP_SIZE = 0x0040_0000   // 4 MiB — ample for tasks + continuations
```
No image cost (it's just RAM the firmware isn't using). On Pi 4, low RAM from ~0x80000 up is free.

## 2. Files
- **NEW `Sources/Support/alloc.c`** — the allocator (plain C; on Mach-O these become `_malloc` etc.,
  which is exactly what the Swift runtime references). Include `"Support.h"` if it needs the IRQ helpers.
- **`Sources/Support/include/Support.h`** (append) — DAIF critical-section helpers (used to guard the
  allocator so a future IRQ can't corrupt a half-updated free list):
  ```c
  static inline __attribute__((always_inline)) unsigned long irq_save(void) {
      unsigned long f;
      __asm__ volatile("mrs %0, daif; msr daifset, #2" : "=r"(f) :: "memory");
      return f;
  }
  static inline __attribute__((always_inline)) void irq_restore(unsigned long f) {
      __asm__ volatile("msr daif, %0" :: "r"(f) : "memory");
  }
  ```
- **`Sources/Application/Application.swift`** (temporary self-test — clearly marked, removed after verify).

## 3. Allocator algorithm — first-fit free list with boundary-tag coalescing
A standard minimal heap. Each block has an 8-byte header `{ size_t size; }` where `size` is the payload
size and the low bit is the in-use flag (sizes are 16-byte aligned so the low bits are free for flags).
Maintain a singly-linked free list. On `free`, coalesce with the physically-adjacent next/prev free
block (boundary tags) to avoid fragmentation across sleep/wake cycles.

Requirements:
- **16-byte minimum alignment** for all returns (`malloc`). Swift task slabs want 16; honor it.
- `posix_memalign(void** p, size_t align, size_t size)`: `align` is a power of two ≥ sizeof(void*).
  Over-allocate, round the payload up to `align`, and store enough back-pointer/size info that `free`
  on the returned (aligned) pointer still finds the true block header. (A common trick: store the real
  block pointer in the word immediately before the aligned pointer.) Return 0 on success, `EINVAL`/`ENOMEM`
  on failure.
- `calloc(n, sz)`: `malloc(n*sz)` (check overflow) then zero.
- `realloc(p, sz)`: malloc+copy+free (simple is fine); `realloc(NULL,sz)==malloc`, `realloc(p,0)==free`.
- `free(NULL)` is a no-op; `malloc(0)` may return NULL or a minimal block (be consistent).
- **Wrap the list mutations in `irq_save()`/`irq_restore()`** (forward-safety; harmless now).
- Initialize lazily on first `malloc` (one free block spanning the whole window) or via an explicit
  `heap_init()` called from `main` before the self-test — your choice; document it.

## 4. Self-test (temporary, in `Application.swift` main, BEFORE the GIC/wfi loop)
After the banner, exercise the allocator and print results so we can verify on hardware:
```
// PROBE (temporary): prove the allocator before the executor depends on it.
let a = malloc(64); let b = malloc(64)
uartPuts("malloc a="); uartPutHex(UInt64(UInt(bitPattern: a)))   // expect ~0x400000+
uartPuts(" b=");        uartPutHex(UInt64(UInt(bitPattern: b)))   // expect != a, in-window
free(a)
let c = malloc(64)                                               // expect to reuse a's slot
uartPuts(" c(after free a)="); uartPutHex(UInt64(UInt(bitPattern: c))); uartPuts("\n")
// posix_memalign 4096-aligned:
var p: UnsafeMutableRawPointer? = nil
let r = posix_memalign(&p, 4096, 256)
uartPuts("memalign r="); uartPutHex(UInt64(UInt(r))); uartPuts(" p=")
uartPutHex(UInt64(UInt(bitPattern: p))); uartPuts(" (low12 must be 0)\n")
```
(You'll need to declare the C functions visible to Swift — they come in via the `Support` module since
`alloc.c` is in the `Support` target with the header. If Swift can't see `malloc`/`posix_memalign`,
add prototypes to `Support.h`.)

Expected on serial: `a`,`b` distinct pointers in `0x400000..0x800000`; `c == a` (slot reused after free);
`memalign r=0x0`, `p` with low 12 bits zero.

## 5. Rules for the implementer
- Additive: NEW `alloc.c`; append to `Support.h`; the temporary probe in `Application.swift` (keep the
  existing GIC/timer/wfi code AFTER it — the probe runs before `irq_enable()`). Do not touch boot.S,
  vectors.S, UART/GPIO/Timer/GIC/IRQHandler.
- Match the existing C-shim style in `Support.h`. Integer-only (no FP).
- Run `./build.sh`; report build result + the kernel8.img size. **Do NOT claim it works on hardware.**
- Output complete file contents + a changelog + any deviations. Flag if any libc symbol you didn't
  expect comes up undefined at link (e.g. `memset`/`memcpy` builtins) — note it; don't guess-stub silently.
