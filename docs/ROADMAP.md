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
- **EPIC F (in progress):** V67–V79 GENET through bounded TCP echo (`genet12`). Standing free-running 256-BD ring is not used (UART stall). EL0 socket syscall after genet12 I-aborts and is deferred. **EPIC G:** V71–V84 peripheral probes + V91–V95 GPIO/timer/PWM + V96–V99 mailbox/watchdog + V100 RNG200 + V101 DMA memcpy. **EPIC H:** V85–V90 SD reload, root list, second file, `overlays/` walk, one overlay file load, named `issue.txt`, V102 free-cluster CMD24 write, V103 `AETHER.TMP` create/link, V104 named re-read, V105 CMD13 card status, V106 ACMD51 SCR, V107 ACMD13 SD_STATUS, V108 ACMD6 4-bit bus, V109 CMD18 multi-block, V110 CMD6 switch-check, and V111 CMD25 multi-block write (no EL0).

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
- [x] V105 SDHCI CMD13 SEND_STATUS after GENET (`sdst ok=1 version=105 state=4 ready=1 rca=`). Fail-closed TRAN + READY_FOR_DATA; no EL0.
- [x] V106 SDHCI ACMD51 SEND_SCR after GENET (`sdscr ok=1 version=106 spec=`). Fail-closed structure 0 + 4-bit bus; restore 512-byte blocks; no EL0.
- [x] V107 SDHCI ACMD13 SD_STATUS after GENET (`sdss ok=1 version=107 type=`). Fail-closed SD/SDHC type; restore 512-byte blocks; no EL0.
- [x] V108 SDHCI ACMD6 SET_BUS_WIDTH after GENET (`sdbus ok=1 version=108 bits=`). Fail-closed 4-bit host readback + MBR; restore 1-bit; no EL0.
- [x] V109 SDHCI CMD18 multi-block read after GENET (`sdmb ok=1 version=109 blocks=`). Fail-closed 2 blocks + MBR; AUTO_CMD12; restore 512-byte single-block; no EL0.
- [x] V110 SDHCI CMD6 SWITCH_FUNC check after GENET (`sdsw ok=1 version=110 grp1=`). Fail-closed group-1 default access mode; restore 512-byte blocks; no EL0.
- [x] V111 SDHCI CMD25 multi-block write after GENET (`sdmw ok=1 version=111 match=`). Fail-closed 2-block readback into a free FAT span; FAT/dir unchanged; no EL0.
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
- [x] V69 mailbox station MAC (`GET_BOARD_MAC_ADDRESS`). No UMAC write.
- [x] V72 leftover-RX stop + system NC RX ring (`genet5 ok=1 version=72 stop=.* ring=.* rx=.* frames=`).
- [x] V73 mailbox MAC into UMAC + own TX ring + one ARP (`genet6 ok=1 version=73 mac=.* tx=.* frames=`).
- [x] V74 Linux ring-16 BD count + leftover RBUF reset + fail-closed TX CONS (`genet7 ok=1 version=74 ring=.* tx=.* cons=.* prod=.* frames=`).
- [x] V75 v4/v5 TDMA PROD at 0x0C (`genet8 ok=1 version=75 prod=.* cons=.* tx=.* frames=`).
- [x] V76 parse one RX ARP request or ICMP echo-request and reply (`genet9 ok=1 version=76 rx=.* tx=.* kind=`).
- [x] V77 bounded multi-BD unpark/poll/park (`genet10 ok=1 version=77 rx=.* tx=.* replies=.* kind=`). Free-running 256-BD ring stalls UART; poll is bounded. Host `ping -c 2` after shell-ready proven.
- [x] V78 bounded UDP echo on port 7 (`genet11 ok=1 version=78 rx=.* tx=.* replies=.* kind=`). Same unpark/poll/park. Host `SOCK_DGRAM` to `10.42.0.2:7` after shell-ready proven.
- [x] V79 bounded TCP echo on port 7 (`genet12 ok=1 version=79 rx=.* tx=.* replies=.* kind=`). 3-way + one payload in the same unpark/poll/park window. Host `SOCK_STREAM` to `10.42.0.2:7` is the proof target.
- Minimal TCP/IP: ARP, IPv4, ICMP (ping), UDP, then TCP.
- A tiny socket API exposed as syscalls. (EL0 `sys_socket` after genet12 I-aborts `esr=0xbf000002` and watchdog-resets; deferred.)
- *First proof:* the kernel answers an ICMP ping from the host over its OWN driver
  (independent of the firmware netboot path); later, a UDP/TCP echo.

### EPIC G — Peripherals & userland maturity  (round out the board)
- [x] V71 GPIO register probe: GPFSEL0 + 2711 `PUP_PDN_CNTRL_REG0` + UART0 ALT0 readback (`gpio ok=1 version=71 fsel=.* pup=.* uart=`). Read-only; no boot event emit.
- [x] V80 I2C/SPI register probe: BSC1 C/DIV + SPI0 CS/CLK (`i2c ok=1 version=80 bsc=.* div=.* spi=`). Read-only; no extra hardware; no boot event emit.
- [x] V81 PWM register probe: PWM0 CTL/STA + 2711 PWM1 CTL (`pwm ok=1 version=81 ctl=.* sta=.* pwm1=`). Read-only; idle STA EMPT1 required; no boot event emit.
- [x] V82 I2C no-ACK transfer: one bounded BSC1 write to vacant `0x7F` (`i2c2 ok=1 version=82 nack=1 addr=.* sta=`). GPIO2/3 muxed then restored; timeout/ACK fail-closes; no boot event emit.
- [x] V83 SPI0 bounded byte: one FIFO write, DONE required (`spi2 ok=1 version=83 done=1 loop=.* rx=`). `loop=1` only if RX==TX; no jumper expected. GPIO7-11 muxed then restored; no boot event emit.
- [x] V84 system timer register probe: CLO/CHI advance + four compare slots (`stimer ok=1 version=84 clo=.* chi=.* chans=`). Read-only; no CS/C0-C3 writes (GPU owns C0/C2); no boot event emit.
- [x] V91 GPIO42 output + GPLEV readback after GENET (`gpio2 ok=1 version=91 pin=42 set=1 clr=1`). Kernel ACT LED; restore FSEL; no jumper; no EL0.
- [x] V92 system timer C1 match after GENET (`stimer2 ok=1 version=92 chan=1 match=1`). ARM C1 only; no C0/C2 writes; park after match; no EL0.
- [x] V93 PWM clock enable + CTL poke after GENET (`pwm2 ok=1 version=93 clk=1 en=1`). CM_PWM OSC + PWM0 PWEN1 readback; restore; no pin-mux; no output claim; no EL0.
- [x] V94 GPIO26 PUP_PDN write+readback after GENET (`gpio3 ok=1 version=94 pin=26 up=1 dn=1`). REG1 only; restore; no UART; no EL0.
- [x] V95 system timer C3 match after GENET (`stimer3 ok=1 version=95 chan=3 match=1`). ARM C3 only; no C0/C1/C2 writes; park after match; no EL0.
- [x] V96 mailbox GET_TEMPERATURE after GENET (`mboxt ok=1 version=96 temp=`). Millidegrees; fail-closed range; no EL0.
- [x] V97 mailbox GET_CLOCK_RATE (ARM) after GENET (`mboxc ok=1 version=97 clk=3 hz=`). Hz; fail-closed range; no EL0.
- [x] V98 PM watchdog remaining-tick readback after GENET (`wdog2 ok=1 version=98 armed=1 off=1 remain=`). Arm/read/disable; never reset_now; no EL0.
- [x] V99 mailbox GET_VOLTAGE (core) after GENET (`mboxv ok=1 version=99 id=1 uv=`). Microvolts; fail-closed range; no EL0.
- [x] V100 BCM2711 RNG200 word after GENET (`rng ok=1 version=100 ready=1 data=`). FIFO ready; fail-closed warmup; no EL0.
- [x] V101 BCM2711 DMA engine memcpy after GENET (`dma2 ok=1 version=101 chan=4 match=1 bytes=32`). Channel 4; fail-closed END/!ERR/src==dst; no EL0.
- Driver surface: full GPIO, SPI, I2C, PWM, system timer channels.
- A real **interactive shell running as a user process** (not the in-kernel UART shell).
- A minimal libc/runtime for user programs; a couple of bundled utilities.

### EPIC H — "Like Pi OS" capstone  (usable standalone)
- [x] V85 reload `config.txt` from FAT32 after GENET (`sdload ok=1 version=85 file=config.txt bytes=.* checksum=.* match=1`). Load only; no EL0 execute (I-abort after GENET DMA).
- [x] V86 list FAT32 root after GENET (`sdls ok=1 version=86 files=.* config=1 other=`). Short-name count; ok requires files>=2 and CONFIG.TXT plus one other 8.3 name. No EL0.
- [x] V87 load a second FAT32 root file after GENET (`sdfile ok=1 version=87 name=.* bytes=.* checksum=`). Prefer cmdline.txt / issue.txt; skip directories; not CONFIG.TXT. No EL0.
- [x] V88 walk FAT32 `overlays/` after GENET (`sdovl ok=1 version=88 files=.* name=`). Directory cluster walk; ok requires files>=1. No EL0.
- [x] V89 load one file from FAT32 `overlays/` after GENET (`sdovf ok=1 version=89 name=.* bytes=.* checksum=`). First regular file with size<=65536. No EL0.
- [x] V90 load FAT32 root `issue.txt` by name after GENET (`sdiss ok=[01] version=90 present=[01] name=.* bytes=.* checksum=`). Fail-closed if missing. No EL0. Do not execute.
- [x] V102 SDHCI CMD24 write of a free FAT32 cluster after GENET (`sdwr ok=1 version=102 match=1 bytes=512 clus=`). FAT/dir unchanged; fail-closed readback; no EL0. Do not execute.
- [x] V103 FAT32 create/link of `AETHER.TMP` after GENET (`sdmk ok=1 version=103 match=1 created=`). Dedicated scratch only; fail-closed if name is foreign; no EL0. Do not execute.
- [x] V104 re-read `AETHER.TMP` by name after GENET (`sdrd ok=1 version=104 match=1 present=1 name=`). Read-only; fail-closed if missing/foreign; no EL0. Do not execute.
- [x] V105 SDHCI CMD13 SEND_STATUS after GENET (`sdst ok=1 version=105 state=4 ready=1 rca=`). Fail-closed TRAN + READY_FOR_DATA; no named-file write; no EL0.
- [x] V106 SDHCI ACMD51 SEND_SCR after GENET (`sdscr ok=1 version=106 spec=`). Fail-closed structure 0 + 4-bit bus; no named-file write; no EL0.
- [x] V107 SDHCI ACMD13 SD_STATUS after GENET (`sdss ok=1 version=107 type=`). Fail-closed SD/SDHC type; no named-file write; no EL0.
- [x] V108 SDHCI ACMD6 SET_BUS_WIDTH after GENET (`sdbus ok=1 version=108 bits=`). Fail-closed 4-bit host readback + MBR; restore 1-bit; no named-file write; no EL0.
- [x] V109 SDHCI CMD18 multi-block read after GENET (`sdmb ok=1 version=109 blocks=`). Fail-closed 2 blocks + MBR; AUTO_CMD12; no named-file write; no EL0.
- [x] V110 SDHCI CMD6 SWITCH_FUNC check after GENET (`sdsw ok=1 version=110 grp1=`). Fail-closed group-1 default access mode; no named-file write; no EL0.
- [x] V111 SDHCI CMD25 multi-block write after GENET (`sdmw ok=1 version=111 match=`). Fail-closed 2-block readback into a free FAT span; FAT/dir unchanged; no named-file write; no EL0.
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
