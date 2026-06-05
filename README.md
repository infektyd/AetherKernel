# AetherKernel

A bare-metal kernel for the Raspberry Pi 4B (BCM2711, Cortex-A72) written in
**Embedded Swift** — no OS, no SDK, no Node, boots straight from `kernel8.img`.

> Status: **Runtime V3 hardware-verified on real Raspberry Pi 4B**
> (2026-06-05) — netbooted image fetched `kernel8.img`, printed banner +
> padded `CurrentEL = 0x0000000000000004` (EL1), `rtv2 fast/slow/long`
> async cadences, and UART shell command responses over PL011 serial @ 115200.

## What works (verified on the build side)

| Milestone | State | Verified how |
|-----------|-------|--------------|
| Toolchain → Mach-O `arm64-apple-none-macho` → `kernel8.img` | ✅ | `build.sh` uses Swift 6.3.2 + `macho2bin.py`; latest local build emits `kernel8.img` |
| PL011 UART0 driver + banner + `CurrentEL` readout | ✅ | **banner received over serial on real Pi 4** |
| EL2 → EL1 drop | ✅ | **`CurrentEL = 0x0000000000000004` read back over serial on hardware** |
| GPIO42 ACT-LED blink | historical ✅ | verified in earlier bring-up; current liveness is serial `rtv2 fast/slow/long` |
| GPIO14/15 → ALT0 in code (don't trust the overlay) | ✅ | disassembly `bfi w9,w8,#12,#6`; serial works on hardware |
| Generic timer (CNTP), polled 1 s tick | ✅ | `CNTFRQ = 54 MHz`; tick measured 1.0005 s mean on hardware |
| GIC-400 IRQ routing — CNTP (INTID 30) → EL1 vector → `wfi` idle | ✅ | **interrupt-driven `irq N` @ 1.0002 s mean on hardware; CPU idles in `wfi`** |
| Embedded Swift Runtime V2 async scheduler | ✅ | hardware run printed independent `rtv2 fast/slow/long` cadences; shared CNTP arbiter drives continuation sleeps + executor delays |
| Runtime V3 UART shell/control plane | ✅ | hardware run printed `shell ready`; `status`, `heap`, `queues`, and `tasks` returned machine-checkable `key=value` lines |
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

For the current bench setup (USB-TTL serial already logging and Ethernet wired),
the faster iteration path is Pi 4 EEPROM netboot over the direct Mac-Pi Ethernet
link:

```sh
brew install dnsmasq        # one-time host dependency
./prepare-tftp.sh --download
./serve-netboot.sh en0      # foreground TFTP-only dnsmasq
./netboot-doctor.sh        # guided first netboot: prompts for one reset, verifies
./net-iterate.sh           # build, stage, serial-reset, verify TFTP + serial
```

See `RUNBOOK.md` for the required one-time EEPROM config. Keep `flash.sh` as the
SD recovery path. The exact Pi 4 bootloader settings live in
`netboot-eeprom-config.txt`. On the current bench, dnsmasq blocksize negotiation
must stay enabled; `AETHER_TFTP_NO_BLOCKSIZE=1` is only a diagnostic fallback.

## Layout

```
Sources/Support/boot.S        _start: park cores, EL2->EL1 drop, VBAR, BSS, ->main
Sources/Support/vectors.S     16-entry EL1 vector table -> common syndrome handler
Sources/Support/include/      C volatile MMIO shim (mmio_read32/write32, nop, CurrentEL)
Sources/Application/UART.swift PL011 driver (init/putc/puts/puthex)
Sources/Application/GPIO.swift UART pin mux + historical ACT-LED helpers
Sources/Application/Exceptions.swift  prints ESR/ELR/FAR on fault
Sources/Application/TimerSleep.swift   8-slot CNTP-backed async continuation sleep
Sources/Application/UARTShell.swift    Runtime V3 line command shell over UART RX
Sources/Application/Application.swift  @main: banner, CurrentEL, Runtime V3 async cadences + shell
build.sh / flash.sh / netboot-doctor.sh / netflash.sh / net-iterate.sh
prepare-tftp.sh / serve-netboot.sh / serial-reset.sh / serial-command.sh
macho2bin.py / config.txt / netboot-eeprom-config.txt / RUNBOOK.md
```

## Roadmap (next, once it boots)

1. ~~Confirm boot on hardware: banner + `CurrentEL = 0x0000000000000004` (EL1) over serial.~~ ✅ 2026-06-04
2. ~~Generic timer tick (CNTP) → a real periodic heartbeat instead of a busy delay.~~ ✅ 2026-06-04 (polled, 1 s @ 54 MHz)
3. ~~GIC-400 IRQ routing (turns the polled timer into a true interrupt; first use of the vector table).~~ ✅ 2026-06-04 (interrupt-driven, `wfi` idle)
4. **The Embedded-Swift concurrency experiment (custom executor).** 🏆 **`async`/`await`
   running on bare metal — hardware-verified 2026-06-04, expanded to Runtime V2 on 2026-06-05.**
   Swift `async Task`s are scheduled by our own C cooperative executor on the real Pi 4; Runtime V2
   prints independent `rtv2 fast/slow/long` cadences using one shared CNTP timer arbiter.
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
     `swift_task_create`'s `ldxr/stxr` CAS loop spins forever.
   - **Stage 3 — timer-backed async sleep + heartbeat.** ✅ hardware-verified. `Task.sleep` is
     unavailable in Embedded Swift, so suspension is hand-rolled with `withUnsafeContinuation`
     (`TimerSleep.swift`), resumed by the CNTP timer IRQ; the CNTP register ops live in non-inline C
     (`timersleep_hw.c`). Runtime V2 replaces the single-sleeper path with an 8-slot continuation
     sleep queue and routes executor delay/deadline hooks through the same timer arbiter. Hardware proof:
     fresh netboot printed `rtv2 fast 0`, `rtv2 slow 0`, and `rtv2 long 0`; CPU idles in `wfi` between jobs.
     Bonus: `watchdog.c` (BCM2711 PM reset) — hardware-verified self-reboot.
     See `CONCURRENCY_DESIGN.md` (GROUND TRUTH block) for the verified symbol/ABI contract.
   - **Runtime V3 — async UART control plane.** ✅ hardware-verified. A dedicated async shell task
     polls PL011 RX every 25 ms and accepts line commands: `help`, `status`, `heap`, `queues`,
     `tasks`, and `reboot`; `r`/`R` remain watchdog-reset aliases for the netboot loop. Hardware proof:
     fresh netboot printed `shell ready commands=help,status,heap,queues,tasks,reboot`, and
     `serial-command.sh` produced `status`, `heap`, `queues`, and `tasks` response lines.

## Provenance

Built from research by GPT-5.5 Pro (architecture dossiers) and Grok (iterative
skeleton), reconciled against Apple's `swift-embedded-examples/rpi-4b-blink`
(the verified base). The boot path was written and reviewed line-by-line rather
than transliterated — bare-metal punishes confident-but-wrong.
