# AetherKernel — Roadmap & Far Horizon

This file is the **far-horizon anchor** for autonomous work. When an agent finishes
the active milestone (or no Minni plan is active), it consults THIS file to decide
what to build next, then creates a new Minni plan and decomposes it into slices.
See `AGENTS.md` → "Autonomous mode."

---

## North Star (the far horizon)

> **Turn AetherKernel from a bare-metal concurrency substrate into a general-purpose
> operating system that drives the Raspberry Pi 4's real hardware — storage, display,
> keyboard, and network — so it can eventually be *used* standalone, the way the Pi
> runs Raspberry Pi OS: boot the board, see a console on the screen, type on a USB
> keyboard, read/write files on the SD card, and run programs loaded at runtime.**

This is a multi-year-class goal. It is reached one **hardware-proven increment** at a
time — the same cadence that produced V1–V44. Never "boil the ocean": each increment
must boot on the real Pi 4 and emit a machine-checkable serial marker + bootcert flag.

## Where we are now (V48)

A strong **microkernel-style runtime core**, all hardware-verified:
- Boot → EL1, PL011 UART, GPIO mux, CNTP timer, GIC-400 IRQ routing, EL1 vectors.
- Embedded-Swift `async/await` on bare metal (custom C executor), timer-backed sleep.
- Memory: fixed memory map, 4 KiB frame allocator, guarded heap, typed pools,
  **dynamic virtual memory (page tables + TLB)**, **kernel/user address-space split (isolated page tables)**, **EL0 entry/exit and context save/restore**, and **syscall ABI via SVC (versioned table + bootcert flag)**.
- Kernel objects: registry, capability-tagged handles, mailboxes, async channels,
  supervisor, event-log ring, cancellation tokens, structured task spawn.
- SMP: 4-core bring-up, atomics/spinlocks, per-core runqueues, timer-driven dispatch,
  secondary workers, work-stealing, load-balancing, priority/preemption, bounded soak.
- Boot certificate + substrate certificate; panic/fault retained records; watchdog.

**The gap to the North Star** is everything that makes an OS *general-purpose*: user
mode, processes, storage, display, input, and networking. The epics below close it.

---

## The Epic Ladder (dependency-ordered)

Work epics roughly top-to-bottom; later epics depend on earlier ones. Each epic is a
band of V-increments. Version numbers are indicative, not contractual — the agent
assigns the next integer and may split/insert increments as reality demands. Every
increment ends in a `bootcert`/`certificate` flag + a dedicated `check`/`sched`-style
command, proven on metal, exactly like V1–V44.

### EPIC A — Memory isolation & user mode  (foundation; unlocks "real OS")
Turn the static identity-map MMU into a real virtual-memory system and run code at EL0.
- [x] Dynamic page-table management (allocate/map/unmap, TLB maintenance).
- [x] Kernel/user address-space split; per-address-space page tables.
- [x] EL0 entry/exit; save/restore user context through the existing vectors.
- [x] Syscall ABI via `SVC` (a tiny, versioned syscall table with a bootcert flag).
- `copy_from_user`/`copy_to_user` with fault-safe access.
- *First proof:* a hand-built EL0 stub makes a syscall and returns; marker proves the
  round trip and that a user fault is contained, not fatal to the kernel.

### EPIC B — Processes & program loading  (run something that isn't compiled in)
- Process abstraction: address space + one or more threads + lifecycle state.
- Loader for a user binary (Mach-O `arm64-apple-none-macho`, or ELF — pick the simplest
  to produce from the existing toolchain) into a fresh address space.
- Schedule user threads on the existing SMP scheduler (reuse V31–V44 machinery).
- *First proof:* load a tiny user program from an in-image blob, run it at EL0, it
  prints via a `write` syscall; multiple processes run concurrently and are isolated.

### EPIC C — Storage  (persistence; read the card you booted from)
- BCM2711 **EMMC2 / SDHCI** driver (the SD card controller).
- Block-device abstraction + a small buffer cache.
- **FAT32 read** first (mount the firmware boot partition), then write.
- Minimal VFS layer so files have a uniform API.
- *First proof:* read a known file off the SD card over the kernel's own driver (not
  firmware) and checksum it against the host; marker reports bytes + checksum.

### EPIC D — Console & display  (see it without a laptop)
- VideoCore **mailbox property interface** (clocks, power domains, framebuffer alloc).
- HDMI **framebuffer** → a text console (font blit) mirroring the UART console.
- *First proof:* a banner + live counter rendered on an attached HDMI screen; marker
  reports framebuffer geometry/pitch obtained from the mailbox.

### EPIC E — Input & USB  (the hard one — real interactivity)
- USB host stack for Pi 4: **xHCI behind the VL805** (PCIe) — or DWC2 for the simpler
  path first if it's reachable. Expect this to be the largest epic; split aggressively.
- **USB HID keyboard** → feed keystrokes into the console input path.
- USB mass storage (optional, after HID).
- *First proof:* a keypress on a real USB keyboard appears on the (HDMI or serial)
  console; marker reports the HID report decode.

### EPIC F — Networking  (the Pi talks on its own)
- **GENET (bcmgenet)** Ethernet driver (the Pi 4 onboard NIC).
- Minimal TCP/IP: ARP, IPv4, ICMP (ping), UDP, then TCP.
- A tiny socket API exposed as syscalls.
- *First proof:* the kernel answers an ICMP ping from the host over its OWN driver
  (independent of the firmware netboot path); later, a UDP/TCP echo.

### EPIC G — Peripherals & userland maturity  (round out the board)
- Driver surface: full GPIO, SPI, I2C, PWM, system timer channels.
- A real **interactive shell running as a user process** (not the in-kernel UART shell).
- A minimal libc/runtime for user programs; a couple of bundled utilities.

### EPIC H — "Like Pi OS" capstone  (usable standalone)
- Load and run programs from the SD card at runtime (Epic C + B + G together).
- Display + USB keyboard + storage + shell = a self-contained, interactive system you
  can use at the bench with no host attached.
- Stability: long-run soak across all subsystems; clean shutdown/reboot.

---

## Working agreement for autonomous milestone selection

1. Pick the **lowest-epic, next unbuilt increment** that has its dependencies met.
   Do not jump ahead to a flashy epic (USB, networking) before its foundations
   (user mode, processes) exist — that path dead-ends.
2. Keep increments **small enough to prove in one milestone**. If an increment can't
   be proven on metal in one plan, split it (the USB epic especially).
3. Each increment: implement → build → flash/prove on real Pi 4 (cold-cycle as needed)
   → add a `bootcert`/`certificate` flag + a dedicated check command → bump the
   runtime version integer → record the kernel8.img sha256 + serial marker → commit.
4. When a milestone completes, update this file's "Where we are now" line and check the
   increment off, then select the next one.
5. **Honesty gate:** a green build is not proof. Only a captured on-metal serial marker
   counts. If hardware is unreachable, stop and report — never fabricate a marker.
