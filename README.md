# AetherKernel

A bare-metal kernel for the Raspberry Pi 4B (BCM2711, Cortex-A72) written in
**Embedded Swift** — no OS, no SDK, no Node, boots straight from `kernel8.img`.

> Status: **async heartbeat previously hardware-verified on real Raspberry Pi 4B**
> (2026-06-04) — banner + padded `CurrentEL = 0x0000000000000004` (EL1) +
> `async tick N` over PL011 serial @ 115200. The current source should be
> re-flashed before claiming fresh hardware verification.

## What works (verified on the build side)

| Milestone | State | Verified how |
|-----------|-------|--------------|
| Toolchain → Mach-O `arm64-apple-none-macho` → `kernel8.img` | ✅ | `build.sh` uses Swift 6.3.2 + `macho2bin.py`; latest local build emits `kernel8.img` |
| PL011 UART0 driver + banner + `CurrentEL` readout | ✅ | **banner received over serial on real Pi 4** |
| EL2 → EL1 drop | ✅ | **`CurrentEL = 0x0000000000000004` read back over serial on hardware** |
| GPIO42 ACT-LED blink | historical ✅ | verified in earlier bring-up; current liveness is serial `async tick N` |
| GPIO14/15 → ALT0 in code (don't trust the overlay) | ✅ | disassembly `bfi w9,w8,#12,#6`; serial works on hardware |
| Generic timer (CNTP), polled 1 s tick | ✅ | `CNTFRQ = 54 MHz`; tick measured 1.0005 s mean on hardware |
| GIC-400 IRQ routing — CNTP (INTID 30) → EL1 vector → `wfi` idle | ✅ | **interrupt-driven `irq N` @ 1.0002 s mean on hardware; CPU idles in `wfi`** |
| Embedded Swift async heartbeat | ✅ | previous hardware run printed `async tick N`; TimerSleep owns CNTP in the current milestone |
| EL1 exception vectors | ✅ (IRQ) | IRQ slot `0x280` → `irq_entry` exercised on hardware; sync/fault slots still untriggered |

First hardware boot: 2026-06-04. The one trap worth recording — serial was
silent until the FT232 **RX** was moved to header **pin 8** (GPIO14/Pi-TXD); a
classic RX/TX crossover mistake, not a kernel bug.

## Toolchain reality (why the build looks unusual)

- Built with **`swift-6.3.2-RELEASE`** and the **`arm64-apple-none-macho`**
  triple because the Embedded `_Concurrency` archive exists there, not for
  `aarch64-none-none-elf`.
- **No swift-mmio.** Its macros pull in swift-syntax, which older Embedded toolchains
  can't compile against macOS SDK 26 (`_DarwinFoundation1` ABI break). MMIO is
  done through a tiny C `volatile` shim (`Sources/Support/include/Support.h`)
  instead — guaranteed correct peripheral semantics, zero macro fragility.
- SwiftPM uses `--toolset Toolsets/rpi4-macho.json`; the toolset pins the boot,
  text, and data segments and force-loads Embedded `_Concurrency`.
- `macho2bin.py` extracts `__BOOT,__TEXT,__DATA`, rejects unexpected runtime
  segments, and refuses images that would overlap the heap base at `0x400000`.

## Quickstart

```sh
./build.sh                 # -> kernel8.img
./flash.sh /Volumes/bootfs # copy kernel8.img + config.txt to the SD boot part
# then: screen /dev/cu.usbserial-XXXX 115200   (see RUNBOOK.md for wiring)
```

## Layout

```
Sources/Support/boot.S        _start: park cores, EL2->EL1 drop, VBAR, BSS, ->main
Sources/Support/vectors.S     16-entry EL1 vector table -> common syndrome handler
Sources/Support/include/      C volatile MMIO shim (mmio_read32/write32, nop, CurrentEL)
Sources/Application/UART.swift PL011 driver (init/putc/puts/puthex)
Sources/Application/GPIO.swift UART pin mux + historical ACT-LED helpers
Sources/Application/Exceptions.swift  prints ESR/ELR/FAR on fault
Sources/Application/TimerSleep.swift   one-slot CNTP-backed async sleep
Sources/Application/Application.swift  @main: banner, CurrentEL, async heartbeat
build.sh / flash.sh / macho2bin.py / config.txt / RUNBOOK.md
```

## Roadmap (next, once it boots)

1. ~~Confirm boot on hardware: banner + `CurrentEL = 0x0000000000000004` (EL1) over serial.~~ ✅ 2026-06-04
2. ~~Generic timer tick (CNTP) → a real periodic heartbeat instead of a busy delay.~~ ✅ 2026-06-04 (polled, 1 s @ 54 MHz)
3. ~~GIC-400 IRQ routing (turns the polled timer into a true interrupt; first use of the vector table).~~ ✅ 2026-06-04 (interrupt-driven, `wfi` idle)
4. **The Embedded-Swift concurrency experiment (custom executor).** 🏆 **`async`/`await`
   running on bare metal — hardware-verified 2026-06-04.** Swift `async Task`s scheduled by our own
   C cooperative executor on the real Pi 4: an `async` heartbeat prints `async tick N` ~1 s apart,
   the CNTP timer IRQ resuming the suspended continuation while the CPU idles in `wfi`.
   - Foundation: migrated ELF → `arm64-apple-none-macho` (swift-6.3.2) to get
     `_Concurrency` (not built for `aarch64-none-none-elf`); MS1–3 re-verified on hardware.
   - **Stage 1 — heap allocator** (`Sources/Support/alloc.c`): first-fit free list +
     boundary-tag coalescing. ✅ hardware-verified — freed-slot reuse (`c == a`),
     4096-aligned `posix_memalign`.
   - **Stage 2 — executor + runtime integration.** ✅ hardware-verified. Plain-C `…Impl` hooks
     (`SWIFT_CC(swift)`, `executor.c`), ready ring, NORETURN drain pump,
     `swift_slowAlloc/Dealloc` + libc shims (`libc_shims.c`), `-force_load libswift_Concurrency.a`
     (DefaultExecutor NOT linked). Two bring-up requirements the runtime forced, both in `boot.S`:
     **CPACR_EL1.FPEN** (the runtime uses FP/NEON) and **the MMU** (`mmu.c`, identity-mapped Normal
     cacheable RAM) — without the MMU, Cortex-A72 has no exclusive monitor on Device memory and
     `swift_task_create`'s `ldxr/stxr` CAS loop spins forever. Runtime delay/deadline hooks are
     intentionally unsupported in this milestone because `TimerSleep.swift` owns CNTP.
   - **Stage 3 — timer-backed async sleep + heartbeat.** ✅ hardware-verified. `Task.sleep` is
     unavailable in Embedded Swift, so suspension is hand-rolled with `withUnsafeContinuation`
     (`TimerSleep.swift`), resumed by the CNTP timer IRQ (INTID 30 → `serviceTimerSleeper`); the CNTP
     register ops live in non-inline C (`timersleep_hw.c`). `async tick N` ~1 s apart, CPU idle in
     `wfi` between ticks. Bonus: `watchdog.c` (BCM2711 PM reset) — hardware-verified self-reboot.
     See `CONCURRENCY_DESIGN.md` (GROUND TRUTH block) for the verified symbol/ABI contract.

## Provenance

Built from research by GPT-5.5 Pro (architecture dossiers) and Grok (iterative
skeleton), reconciled against Apple's `swift-embedded-examples/rpi-4b-blink`
(the verified base). The boot path was written and reviewed line-by-line rather
than transliterated — bare-metal punishes confident-but-wrong.
