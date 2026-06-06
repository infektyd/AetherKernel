# AetherKernel

A bare-metal kernel for the Raspberry Pi 4B (BCM2711, Cortex-A72) written in
**Embedded Swift** — no OS, no SDK, no Node, boots straight from `kernel8.img`.

> Status: **Runtime V18 hardware-verified on real Raspberry Pi 4B**
> (2026-06-05) — netbooted image fetched `kernel8.img`, printed banner +
> padded `CurrentEL = 0x0000000000000004` (EL1), `rtv2 fast/slow/long`
> async cadences, the IRQ-backed UART shell marker, the Runtime V5 diagnostics
> marker, the Runtime V6 retained-record marker, the Runtime V7 memory marker,
> the Runtime V8 allocator-guard marker, Runtime V9-V18 self-test markers, and
> UART shell command responses over PL011 serial @ 115200. Runtime V18 adds
> fixed cooperative cancellation tokens and folds them into the `bootcert`
> certificate; hardware proved `bootcert ok=1 version=18 ... cancellations=1
> ... events_lost=0` plus `canceltest ok=1 ... completed=1` across a 3-cycle
> netboot loop.

## What works (verified)

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
| Runtime V4 IRQ-backed UART RX shell | ✅ | hardware run printed `runtime v4: irq-backed uart shell`; `status`, `heap`, `queues`, and `tasks` returned over PL011 RX interrupts; `serial-reset.sh` rebooted back into netboot |
| Runtime V5 diagnostics shell | ✅ | hardware run printed `runtime v5: diagnostics shell`; `diag`, `irqs`, `timers`, `memcheck`, and `faults` returned machine-checkable lines; 3-cycle netboot loop passed |
| Runtime V6 retained panic/fault records | ✅ | hardware run printed `runtime v6: retained panic/fault records`; `panic-test` and `fault-test` watchdog-reset and the next boot reported `retained valid=1 kind=panic/fault` |
| Runtime V7 memory map + frame allocator | ✅ | hardware run printed `runtime v7: memory map + frame allocator`; `memmap` reported `valid=1 regions=7 page_size=4096`; `frames` reported `total=14336 free=14336 used=0 selftest=1`; 3-cycle netboot loop passed |
| Runtime V8 allocator/frame guardrails | ✅ | hardware run printed `runtime v8: allocator guardrails`; `heapcheck` reported `ok=1 error=0 invalid_frees=0 double_frees=0 corruptions=0`; `framecheck` reported `ok=1 total=14336 free=14336 used=0 stress=1`; 3-cycle netboot loop passed |
| Runtime V9 bounded memory pressure self-tests | ✅ | hardware run printed `runtime v9: bounded memory pressure self-tests`; `stress` reported `ok=1 heap=1 frames=1 heap_leak=0 frame_leak=0` |
| Runtime V10 explicit guard probes | ✅ | hardware run printed `runtime v10: explicit guard probes`; `frameprobe` reported `ok=1 last_ok=1`; destructive `heap-invalid-free-test` wrote retained `reason=heap-invalid-free` |
| Runtime V11 boot/soak invariants | ✅ | hardware run printed `runtime v11: boot and soak invariants`; `bootcheck` and `soak` reported `ok=1`; retained clear/readback survived after fixing 8-byte Swift heap-object dealloc |
| Runtime V12 kernel object/task registry | ✅ | hardware run printed `runtime v12: kernel object table + task registry`; `kobjects count=7 capacity=16 active=7 selftest=1`; `tasks2 count=4 capacity=8 selftest=1 task index=0 name=fast` |
| Runtime V13 bounded mailbox queues | ✅ | hardware run printed `runtime v13: bounded mailbox message queues`, `rtv13 mail tx/rx`, `mailboxes count=2 capacity=4 queue_capacity=8 selftest=1`, and `sendtest ok=1` |
| Runtime V14 deterministic task supervisor | ✅ | hardware run printed `runtime v14: deterministic task supervisor`; `supervisor count=6 capacity=8 unhealthy=0 total_missed=0 selftest=1`; `health ok=1 supervised=6 unhealthy=0` |
| Runtime V15 capability-tagged kernel handles | ✅ | hardware run printed `runtime v15: capability-tagged kernel handles`; `handlecheck ok=1`; `kobjects count=11 capacity=16 active=11 selftest=1 handle_selftest=1 cap_selftest=1`; `capcheck ok=1 inspect=1 denied=1 stale=1` |
| Runtime V16 fixed event log ring | ✅ | hardware run printed `runtime v16: kernel event log ring`; `events count=11 capacity=64 lost=0 sequence=11 selftest=1`; event kinds included boot, supervisor, handle, task, timer, mailbox, shell, and selftest |
| Runtime V17 deterministic boot certificate | ✅ | hardware run printed `runtime v17: deterministic boot certificate`; `bootcert ok=1 version=17 memmap=1 heap=1 frames=1 kobjects=1 tasks=1 mailboxes=1 supervisor=1 events=1 events_lost=0`; 3-cycle netboot loop passed |
| Runtime V18 cooperative cancellation tokens | ✅ | hardware run printed `runtime v18: cooperative cancellation tokens`; `bootcert ok=1 version=18 ... cancellations=1 ... events_lost=0`; `canceltest ok=1 capacity=16 active=0 requested=1 completed=1`; 3-cycle netboot loop passed |
| EL1 exception vectors | ✅ | IRQ slot `0x280` → `irq_entry` exercised on hardware; sync `brk` path captured ESR/ELR/FAR and rebooted through the retained fault record |

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
`netboot-eeprom-config.txt`. If Pi bootloader logs show repeated
`start4.elf` early-terminate or timeout failures, restart `serve-netboot.sh`
with `AETHER_TFTP_NO_BLOCKSIZE=1` as the first server-side A/B test.

## Layout

```
Sources/Support/boot.S        _start: park cores, EL2->EL1 drop, VBAR, BSS, ->main
Sources/Support/vectors.S     16-entry EL1 vector table -> common syndrome handler
Sources/Support/include/      C volatile MMIO shim (mmio_read32/write32, nop, CurrentEL)
Sources/Application/UART.swift PL011 driver (init/putc/puts/puthex)
Sources/Application/GPIO.swift UART pin mux + historical ACT-LED helpers
Sources/Application/Exceptions.swift  prints machine-checkable sync fault lines + ESR/ELR/FAR
Sources/Application/TimerSleep.swift   8-slot CNTP-backed async continuation sleep
Sources/Application/UARTRX.swift       Runtime V4 IRQ-backed UART RX async byte bridge
Sources/Application/UARTShell.swift    Runtime V18 line command shell over UART RX
Sources/Application/Application.swift  @main: banner, CurrentEL, Runtime V18 async cadences + shell
Sources/Support/kernel_registry.c     Runtime V12 fixed object/task registry
Sources/Support/kernel_mailbox.c      Runtime V13 fixed mailbox queues
Sources/Support/kernel_supervisor.c   Runtime V14 fixed task supervisor
Sources/Support/kernel_event_log.c    Runtime V16 fixed event log ring
Sources/Support/kernel_cancel.c       Runtime V18 fixed cancellation token table
Sources/Support/alloc.c               Runtime V11 fixed heap allocator + guard/pressure checks
Sources/Support/diagnostics.c         Runtime V6 IRQ/fault/panic counters + retained reset record
Sources/Support/memory_map.c          Runtime V11 fixed memory map + guarded 4 KiB frame allocator
build.sh / flash.sh / netboot-doctor.sh / netflash.sh / net-iterate.sh
prepare-tftp.sh / serve-netboot.sh / serial-reset.sh / serial-command.sh / serial-probe.sh
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
   - **Runtime V4 — IRQ-backed UART RX.** ✅ hardware-verified. PL011 RX/receive-timeout interrupts
     drain into a fixed C byte ring, route through GIC INTID 153 to CPU0, and wake a single Swift
     async shell waiter. Hardware proof: fresh netboot printed `runtime v4: irq-backed uart shell`;
     `serial-command.sh status`, `heap`, `queues`, and `tasks` returned response lines while
     `rtv2 fast/slow/long` cadences continued; `serial-reset.sh` rebooted back into netboot.
   - **Runtime V5 — diagnostics and fault/IRQ self-inspection.** ✅ hardware-verified. The UART
     shell now accepts `diag`, `irqs`, `timers`, `memcheck`, `faults`, `panic-test`, and
     `fault-test` in addition to the V3 commands. Safe proof commands returned `diag version=v5`,
     `irqs total=`, `timers now=`, `memcheck ok=1`, and `faults seen=0` while async cadences
     continued. `panic-test` and `fault-test` are intentionally destructive and are not part of
     the normal liveness proof.
   - **Runtime V6 — retained panic/fault records across watchdog reset.** ✅ hardware-verified.
     A fixed retained record page below the heap stores panic/fault kind, sequence, ESR/ELR/FAR,
     and a short reason. Because RAM is cacheable after MMU bring-up, retained writes explicitly
     clean their D-cache lines (`dc cvac` + `dsb sy`) before watchdog reset. Hardware proof:
     `panic-test` rebooted and the next boot reported `retained valid=1 kind=panic ... reason=panic-test`;
     `fault-test` rebooted and the next boot reported `retained valid=1 kind=fault esr=0xf20000a5 ... reason=sync-fault`.
   - **Runtime V7 — memory map and frame allocator invariants.** ✅ hardware-verified.
     The kernel now exposes a fixed low-memory ownership map, reserves the retained page and existing
     heap window explicitly, and manages a conservative 4 KiB physical-frame window from `0x00800000`
     to `0x04000000` with fixed bitmap storage. Hardware proof: fresh netboot printed
     `runtime v7: memory map + frame allocator`; `memmap` returned `valid=1 regions=7 page_size=4096`;
     `frames` returned `total=14336 free=14336 used=0 reserved=0 base=0x800000 limit=0x4000000 selftest=1`;
     a 3-cycle `net-iterate.sh` loop passed.
   - **Runtime V8 — allocator and frame guardrails.** ✅ hardware-verified.
     Real allocator misuse now fails loudly before metadata mutation: `free`/`realloc` validate
     heap pointers, detect double frees, preserve stable `HEAP_GUARD_*` reason codes, count
     invalid/double/corruption events, and poison freed payloads. The frame allocator tracks
     bad frees and double frees and exposes a fixed-storage stress selftest that allocates and
     returns four frames. Hardware proof: fresh netboot printed `runtime v8: allocator guardrails`;
     `heapcheck` returned `ok=1 error=0 invalid_frees=0 double_frees=0 corruptions=0`;
   `framecheck` returned `ok=1 total=14336 free=14336 used=0 bad_frees=0 double_frees=0 error=0 stress=1`;
   a 3-cycle `net-iterate.sh` loop passed.
  - **Runtime V9 — bounded memory pressure self-tests.** ✅ hardware-verified.
    The shell `stress` command runs fixed-size heap and frame pressure loops, records peak/leak counters,
    and avoids dynamic allocation in the test harness. Hardware proof: `stress ok=1 heap=1 frames=1
    heap_peak=62928 frame_peak=16 heap_leak=0 frame_leak=0`.
  - **Runtime V10 — explicit guard probes.** ✅ hardware-verified.
    Non-destructive `frameprobe` verifies bad-frame and double-frame frees are counted without changing
    final frame ownership. Destructive heap guard commands intentionally panic so retained records prove
    the allocator fails loudly. Hardware proof: `frameprobe ok=1 last_ok=1 ...`; `heap-invalid-free-test`
    rebooted and retained `reason=heap-invalid-free`.
  - **Runtime V11 — boot and soak invariants.** ✅ hardware-verified.
    Startup and shell `bootcheck` report memory-map, heap, frame, and retained-record health; `soak` runs
    repeated bounded pressure rounds. `net-iterate.sh` now probes `status`, `bootcheck`, `stress`, and
    `soak` by default. During V11 proof, retained read/clear exposed a Swift embedded heap-object
    deallocation mismatch: this toolchain's `_swift_allocObject` calls `posix_memalign` with an 8-byte
    floor and later calls `free(object)` directly. The allocator now accepts 8-byte-aligned
    `posix_memalign` payloads while keeping header/footer validation strict. Hardware proof after the fix:
    `retained clear ok=1`, `retained valid=0`, and `bootcheck ok=1 ... retained_valid=0`.
  - **Runtime V12 — kernel object table and task registry.** ✅ hardware-verified.
    A fixed C-owned object table names runtime/driver/task records, and a fixed cooperative task
    registry tracks demo task state, period, object id, and tick counters. Hardware proof:
    `kobjects count=7 capacity=16 active=7 selftest=1` and
    `tasks2 count=4 capacity=8 selftest=1 task index=0 name=fast`.
  - **Runtime V13 — bounded mailbox message queues.** ✅ hardware-verified.
    Fixed C-owned UInt64 mailbox queues register as kernel objects and expose queue depth, sent,
    received, drop, and error counters. Two Swift async demo tasks exchange values through the
    demo mailbox and print `rtv13 mail tx/rx`; shell commands `mailboxes` and `sendtest` provide
    machine-checkable proof. Hardware proof: `mailboxes count=2 capacity=4 queue_capacity=8
    selftest=1` and `sendtest ok=1 mailbox=1 sent=1 received=1`.
  - **Runtime V14 — deterministic task supervisor.** ✅ hardware-verified.
    A fixed C-owned supervisor table watches V12 task IDs, tracks heartbeat deadlines/misses, and
    exposes observe/panic policy fields. The normal proof loop uses observe-mode records and checks
    `supervisor` plus `health`; panic policy is available for future destructive tests. Hardware
    proof: `supervisor count=6 capacity=8 unhealthy=0 total_missed=0 selftest=1` and
    `health ok=1 supervised=6 unhealthy=0 total_missed=0`.
  - **Runtime V15 — capability-tagged kernel handles.** ✅ hardware-verified.
    Kernel objects now expose raw 64-bit handles encoding slot, generation, kind, and granted
    capability mask. Lookups reject stale generations and denied capabilities with stable error
    codes; `kobjects` prints handles/generations/cap masks and `capcheck` proves inspect,
    denied-control, and stale-handle paths. Hardware proof: `handlecheck ok=1`,
    `kobjects count=11 capacity=16 active=11 selftest=1 handle_selftest=1 cap_selftest=1`,
    `object index=0 ... handle=0x0000000103000101 generation=1`, and
    `capcheck ok=1 inspect=1 denied=1 stale=1 last_error=2`.
  - **Runtime V16 — fixed event log ring.** ✅ hardware-verified.
    A fixed 64-record C ring stores coarse subsystem events with monotonic sequence,
    CNTP ticks, stable kind names, three raw args, and an overwrite lost counter.
    The shell `events` command exposes recent boot, supervisor, handle, task, timer,
    mailbox, shell, and selftest events. Hardware proof: `events count=11 capacity=64
    lost=0 sequence=11 selftest=1`.
  - **Runtime V17 — deterministic boot certificate.** ✅ hardware-verified.
    The shell `bootcert` command aggregates the live memory-map, heap guard, frame
    allocator, object/task registry, mailbox, supervisor, and event-log selftests
    into one machine-checkable line. Retained-record validity is reported but does
    not fail the certificate, because destructive retained diagnostics are allowed
    to leave a prior reset record. Hardware proof: `bootcert ok=1 version=17
    memmap=1 heap=1 frames=1 retained_valid=0 kobjects=1 tasks=1 mailboxes=1
    supervisor=1 events=1 events_lost=0`, and a 3-cycle `net-iterate.sh` loop
    passed with events still reporting `lost=0`.
  - **Runtime V18 — cooperative cancellation tokens.** ✅ hardware-verified.
    A fixed 16-record C-owned token table exposes generation-tagged cancellation
    tokens with active, cancelled, and completed states. The normal proof path
    runs a deterministic `canceltest` selftest without heap allocation, registers
    the cancellation subsystem in the task/supervisor surfaces, and extends
    `bootcert` with `cancellations=1`. Hardware proof: `bootcert ok=1 version=18
    ... cancellations=1 ... events_lost=0`, `canceltest ok=1 capacity=16 active=0
    requested=1 completed=1`, supervisor count `7`, `events count=15 capacity=64
    lost=0 sequence=15 selftest=1`, and a 3-cycle `net-iterate.sh` loop passed.

## Provenance

Built from research by GPT-5.5 Pro (architecture dossiers) and Grok (iterative
skeleton), reconciled against Apple's `swift-embedded-examples/rpi-4b-blink`
(the verified base). The boot path was written and reviewed line-by-line rather
than transliterated — bare-metal punishes confident-but-wrong.
