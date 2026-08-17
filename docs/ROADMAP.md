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

## Where we are now (V66)

A strong **microkernel-style runtime core**, with cold-boot metal floor through **Runtime V66** (`runtime v66: HID boot-protocol keyboard`; boot-only `xhci_run` / `usb_enum` / `kbd` markers gated by `net-iterate.sh`, with `usb_enum ok=[01] version=65 addr=.* vendor=.* product=.* class=.* stage=.* portsc=.* portscR=.* slot_raw=.* ad_raw=` and `kbd ok=[01] version=66 keycode=.* char=` on unattended boots):
- Boot → EL1, PL011 UART, GPIO mux, CNTP timer, GIC-400 IRQ routing, EL1 vectors.
- Embedded-Swift `async/await` on bare metal (custom C executor), timer-backed sleep.
- Memory: fixed memory map, 4 KiB frame allocator, guarded heap, typed pools,
  **dynamic virtual memory (page tables + TLB)**, **kernel/user address-space split (isolated page tables)**, **EL0 entry/exit and context save/restore**, **syscall ABI via SVC (versioned table + bootcert flag)**, **fault-safe `copy_from_user`/`copy_to_user`**, and **EPIC A capstone: EL0 syscall round-trip + deliberate user fault containment**.
- Kernel objects: registry, capability-tagged handles, mailboxes, async channels,
  supervisor, event-log ring, cancellation tokens, structured task spawn.
- SMP: 4-core bring-up, atomics/spinlocks, per-core runqueues, timer-driven dispatch,
  secondary workers, work-stealing, load-balancing, priority/preemption, bounded soak.
- Boot certificate + substrate certificate; panic/fault retained records; watchdog.
- **EPIC B**: **process abstraction** — address space (isolated page table + ASID) + lifecycle state (create/destroy), with a fixed process table and ASID bitmap allocator. **User binary loader** — flat blob loaded into a fresh address space, runs at EL0, calls `sys_write` syscall (UART output "Hi\n"), proves end-to-end: process create → binary load → EL0 execute → syscall dispatch → UART write. **Multi-process isolation** — per-core `_kernel_el1_saved_sp` and `uaccess_active_pt`; 3 independent user processes each run isolated and print "Hi\n", proving address-space and EL0 isolation between processes.
- **EPIC C** ✓: **BCM2711 EMMC2/SDHCI register probe** (V54) — `sdhci ok=1 version=54 host_version=2 cap=0x45ee6432`. **SD card identification** (V55) — `card ok=1 version=55 rca=0xaaaa`. **Single block read via CMD17** (V56) — `block ok=1 version=56 mbr=0xaa55`. **FAT32 file read** (V57) — walk root directory (case-insensitive, multi-cluster chain), read config.txt; `fat32 ok=1 version=57 file=config.txt bytes=558 checksum=0xb362`. Certificate v57 fat32=1. SHA 4bc4dc050d9827d33d87f62042c699e44c866871.
- **EPIC D ✓:** V58–V60 mailbox/framebuffer/text-console. HDMI live counter + UART→HDMI mirror proven on metal: boot `console ok=1 version=60 … counter=0 display=0` then shell `counter=64` (sha256 `6d1d24b2ac950e09070b5bbd5ec49bcc4e5ef8b6127485e13a293d1a48d4f426`); later `counter=63 display=0 mirror=37674` (sha256 `5547eaac2f7e317fdff0c76e39f7b95675bbfb4689e5ff7be8a24252122b5f37`). Tick-only in CNTP; one-glyph idle paint; no blit on IRQ/secondary/hot path.
- **EPIC E (in progress):** V61–V66 boot markers ship PCIe/VL805/xHCI capability (`pcie ok=1 version=61`, `vl805 ok=1 version=62`, `xhci ok=1 version=63`), xHCI controller init (`xhci_run ok=1 version=64 ports_connected=.*`), USB enumeration (`usb_enum ok=[01] version=65 addr=.* vendor=.* product=.* class=.* stage=.* portsc=.* portscR=.* slot_raw=.* ad_raw=`), hub downstream walk (`hubwalk ok=[01] version=66 ports=.* connected=.* hid=` — `ok=1` means ports were queried; `connected=0 hid=0` is honest with no device), and HID keyboard selftest (`kbd ok=[01] version=66 keycode=.* char=` — `ok=1` not required on unattended netboot); interactive keypress proof remains open.
- **EPIC F (in progress):** V67 GENET register probe (`genet ok=1 version=67 rev=.* mdio=.* link=`). V68 leftover `CMD_RX_EN` + MIB snapshot (`genet2 ok=1 version=68 mac=.* rx=.* frames=.* bytes=`) — firmware may leave `UMAC_MAC0/1` at 0; `ok=1` is live leftover RX + MIB, not a programmed station MAC. V69 firmware station MAC via mailbox `GET_BOARD_MAC_ADDRESS` (`genet3 ok=1 version=69 mac=.* mbox=.* umac=`) — no UMAC write, no DMA rings, no `CMD_RX_EN` write. V70 firmware board serial via mailbox `GET_BOARD_SERIAL` (`genet4 ok=1 version=70 serial=.* mbox=.* mac=`) — mailbox preferred over MDIO PHYID; no UMAC write, no DMA. ICMP first proof remains open.

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
- [x] `copy_from_user`/`copy_to_user` with fault-safe access.
- [x] *First proof:* a hand-built EL0 stub makes a syscall and returns; marker proves the
  round trip and that a user fault is contained, not fatal to the kernel.

### EPIC B — Processes & program loading  (run something that isn't compiled in)
- [x] Process abstraction: address space + one or more threads + lifecycle state.
- [x] Loader for a user binary (flat blob) into a fresh address space; `sys_write` syscall
  proves EL0 output.
- [x] Schedule user threads on the existing SMP scheduler (reuse V31–V44 machinery).
- [x] *First proof:* load a tiny user program from an in-image blob, run it at EL0, it
  prints via a `write` syscall; multiple processes run concurrently and are isolated.
  *(v52 + v53: loader + per-core EL0 + 3-process isolation proved on Pi4)*

### EPIC C — Storage  (persistence; read the card you booted from)
- [x] BCM2711 **EMMC2 / SDHCI** driver (the SD card controller).
- [x] **FAT32 read** — mount the firmware boot partition, walk root directory, read config.txt.
- *First proof:* `fat32 ok=1 version=57 file=config.txt bytes=558 checksum=0xb362` on Pi4 metal. ✓

### EPIC D — Console & display  (see it without a laptop)
- [x] VideoCore **mailbox property interface** (clocks, power domains, framebuffer alloc).
- [x] HDMI **framebuffer** → a text console (font blit) mirroring the UART console.
- [x] *First proof:* live counter + UART glyph mirror on HDMI, reported on UART.
  `console ok=1 version=60 rows=78 cols=148 glyphs=16 counter=64 display=0`
  (sha256 `6d1d24b2…`); `counter=63 display=0 mirror=37674` (sha256 `5547eaac…`).

### EPIC E — Input & USB  (the hard one — real interactivity)
- USB host stack for Pi 4: **xHCI behind the VL805** (PCIe) — or DWC2 for the simpler
  path first if it's reachable. Expect this to be the largest epic; split aggressively.
- **USB HID keyboard** → feed keystrokes into the console input path.
- USB mass storage (optional, after HID).
- *First proof:* a keypress on a real USB keyboard appears on the (HDMI or serial)
  console; marker reports the HID report decode.

### EPIC F — Networking  (the Pi talks on its own)
- [ ] **GENET (bcmgenet)** Ethernet driver (the Pi 4 onboard NIC).
- [x] V67 register probe: SYS_REV_CTRL + bounded MDIO BMSR / RGMII OOB link.
- [x] V68 leftover `CMD_RX_EN` + MIB `rx.pok`/`rx.bytes` (read-only; no DMA). `UMAC_MAC0/1` may be 0.
- [x] V69 mailbox station MAC (`GET_BOARD_MAC_ADDRESS`). No UMAC write. DMA still parked.
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
