# AetherKernel

A bare-metal kernel for the Raspberry Pi 4B (BCM2711, Cortex-A72) written in
**Embedded Swift** — no OS, no SDK, no Node, boots straight from `kernel8.img`.

> Status: **boots on real Raspberry Pi 4B hardware** (2026-06-04) — banner +
> `CurrentEL = 0x4` (EL1) + heartbeat confirmed over PL011 serial @ 115200.
> Boot stub, EL2→EL1 drop, UART, and GPIO are now hardware-verified, not just
> compile-/disassembly-verified. Honest labels only: anything not
> hardware-confirmed says so.

## What works (verified on the build side)

| Milestone | State | Verified how |
|-----------|-------|--------------|
| Toolchain → bare AArch64 ELF → `kernel8.img` | ✅ | boots on hardware; `elf2bin` emits 2 PT_LOAD segments at `0x80000` |
| PL011 UART0 driver + banner + `CurrentEL` readout | ✅ | **banner received over serial on real Pi 4** |
| EL2 → EL1 drop | ✅ | **`CurrentEL = 0x4` read back over serial on hardware** |
| GPIO42 ACT-LED heartbeat | ✅ | **LED blinks + `beat N` counter on hardware** |
| GPIO14/15 → ALT0 in code (don't trust the overlay) | ✅ | disassembly `bfi w9,w8,#12,#6`; serial works on hardware |
| Generic timer (CNTP), polled 1 s tick | ✅ | **`CNTFRQ = 54 MHz`; tick measured 1.0005 s mean on hardware** |
| EL1 exception vectors (`ESR/ELR/FAR` on fault) | ☐ | `VBAR_EL1` set, `<vectors>` 2048-aligned at `0x80800`; **not yet triggered** |

First hardware boot: 2026-06-04. The one trap worth recording — serial was
silent until the FT232 **RX** was moved to header **pin 8** (GPIO14/Pi-TXD); a
classic RX/TX crossover mistake, not a kernel bug.

## Toolchain reality (why the build looks unusual)

- Built with **`swift-6.0-RELEASE`** (Xcode's 6.3.2 has no Embedded stdlib for
  `aarch64-none-none-elf`; the 6.0 swift.org toolchain does).
- **No swift-mmio.** Its macros pull in swift-syntax, which the 6.0 toolchain
  can't compile against macOS SDK 26 (`_DarwinFoundation1` ABI break). MMIO is
  done through a tiny C `volatile` shim (`Sources/Support/include/Support.h`)
  instead — guaranteed correct peripheral semantics, zero macro fragility.
- This toolchain's SwiftPM has no `--toolset`, so the Embedded flags + linker
  script are passed as `-Xswiftc`/`-Xlinker` (see `build.sh`).
- `llvm-objcopy` isn't installed (and `brew llvm` is too big for the disk), so a
  ~30-line `elf2bin.py` extracts the raw image from the ELF.

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
Sources/Application/GPIO.swift ACT-LED heartbeat
Sources/Application/Exceptions.swift  prints ESR/ELR/FAR on fault
Sources/Application/Application.swift  @main: banner, CurrentEL, heartbeat loop
build.sh / flash.sh / elf2bin.py / config.txt / RUNBOOK.md
```

## Roadmap (next, once it boots)

1. ~~Confirm boot on hardware: banner + `CurrentEL = 0x4` (EL1) over serial.~~ ✅ 2026-06-04
2. ~~Generic timer tick (CNTP) → a real periodic heartbeat instead of a busy delay.~~ ✅ 2026-06-04 (polled, 1 s @ 54 MHz)
3. GIC-400 IRQ routing (turns the polled timer into a true interrupt; first use of the vector table). ← **next**
4. Only then: the Embedded-Swift concurrency experiment (custom executor) — as a
   deliberate later phase, not the foundation.

## Provenance

Built from research by GPT-5.5 Pro (architecture dossiers) and Grok (iterative
skeleton), reconciled against Apple's `swift-embedded-examples/rpi-4b-blink`
(the verified base). The boot path was written and reviewed line-by-line rather
than transliterated — bare-metal punishes confident-but-wrong.
