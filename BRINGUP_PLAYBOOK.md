# Bare-metal Embedded-Swift bring-up playbook (BCM2711 / Pi 4, generalizable)

Distilled from AetherKernel's hardware-verified milestones (2026-06-04): boot → UART → polled
timer → GIC IRQ. Every step here was confirmed on real silicon, not just compiled. Pi-4 specifics
are tagged **[BCM2711]**; everything else generalizes to other AArch64 bare-metal targets.

---

## 0. The dev loop that worked (use this for any hard milestone)

1. **Draft a precise spec** yourself; tag uncertain facts `[VERIFY]`.
2. **Run a parallel research report** on the `[VERIFY]` items. (Bare-metal research agents are
   confidently wrong on *narrative root-cause* but reliable on *cited register facts* — extract the
   facts, ignore the story, always confirm against the datasheet/DT.) Our best agent caught two real
   spec bugs (ICFGR1-not-ICFGR2; IPRIORITYR needs word-RMW) before they cost a hardware round-trip.
3. **Reconcile** the report into the spec → finalize.
4. **Offload the draft** (Gemini via `agy --print … --add-dir <repo> --dangerously-skip-permissions`):
   it writes the code + runs the build.
5. **Review + verify yourself** — NEVER trust the agent's "it built": re-run the build, `git diff`
   (additive? forbidden files untouched?), and **disassembly-verify every load-bearing path**.
6. **Flash + serial-verify** on hardware. Commit with an honest "hardware-verified" message only after.

Two oracles save you when serial is dark: the **ACT LED** (kernel-alive, independent of UART) and
**FIFO-drain reasoning** (a TXFF busy-wait that doesn't stall ⇒ the peripheral is clocking bytes out).

---

## 1. Toolchain (no new installs needed)

- **`swift-6.0-RELEASE`** swift.org toolchain (Xcode's Swift can't cross-compile Embedded → bare
  AArch64 — no Embedded stdlib for `aarch64-none-none-elf`).
- Build flags: `--triple aarch64-none-none-elf -enable-experimental-feature Embedded
  -disable-stack-protector -function-sections -fuse-ld=lld -nostdlib -T <linker.ld>
  --unresolved-symbols=ignore-in-object-files`. (6.0 SwiftPM has no `--toolset`; pass via `-Xswiftc`/`-Xlinker`.)
- **No swift-mmio** (its macros pull swift-syntax which crashes 6.0 vs macOS SDK 26). MMIO via a tiny
  C `volatile` shim instead.
- **No `llvm-objcopy`** needed: a ~30-line `elf2bin.py` extracts PT_LOAD segments → `kernel8.img`.
- **Disassembler for verification:** Xcode ships `llvm-objdump`
  (`/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/llvm-objdump`),
  reads ELF/AArch64.

## 2. The bring-up ladder (order matters)

Each rung gives you the tool to verify the next. Don't skip.
1. **Boot stub (asm)** → `bl main`. 2. **UART** (your output channel). 3. **`CurrentEL`** readout
(proves the EL drop). 4. **LED** (independent liveness oracle). 5. **Polled timer** (real time).
6. **GIC IRQ** (interrupts + first vector-table use). Only then: higher-level work.

## 3. Boot stub — the irreducible asm (Swift can't do this)

Park secondary cores (`mpidr_el1 & 3`), drop **EL2→EL1**, set SP, install `VBAR_EL1`, zero BSS, `bl main`.
EL drop essentials: `HCR_EL2 = 1<<31` (RW; **leave IMO/FMO=0** so IRQs stay at EL1, not trapped to EL2),
`SCTLR_EL1 = 0x30d00800` (MMU/caches off, RES1 bits), `CNTHCTL_EL2 = 3` (**EL1PCTEN|EL1PCEN — required
for EL1 timer access**), `CNTVOFF_EL2 = 0`, `SPSR_EL2 = 0x3c5` (EL1h, DAIF masked), `ELR_EL2`, `eret`.

## 4. Serial / PL011 UART **[BCM2711]**

- UART0 (PL011) `0xFE201000`; GPIO `0xFE200000`. `config.txt`: `arm_64bit=1 enable_uart=1
  dtoverlay=disable-bt init_uart_clock=48000000`.
- **Mux GPIO14/15 → ALT0 in code** (`GPFSEL1`=`0xFE200004`, fields `[14:12]`/`[17:15]` = `0b100`);
  don't trust the overlay alone.
- Init: wait `FR.BUSY`(bit3)=0 → `CR=0` → `ICR=0x7FF` → `IBRD=26,FBRD=3` (115200@48MHz) → `LCRH=0x70`
  (8N1+FIFO) → `CR=0x301` (UARTEN|TXE|RXE).
- **Wiring is a crossover** (the #1 trap — cost us hours): adapter **RX → Pi pin 8** (GPIO14/TXD),
  adapter TX → pin 10 (GPIO15/RXD), GND → pin 6. 3V3 jumper. Power Pi by USB-C only, never adapter VCC.
- **Loopback-test the adapter first** (short its TX↔RX, echo a string) to prove Mac/driver/cable before
  blaming the kernel.
- Symptom map: *silence* (line static) = pin not driven / wrong wire; *continuous garbage* = baud
  mismatch; *kernel alive but silent* = UART routing/wiring (LED still blinks).

## 5. Generic timer (CNTP), polled — accurate tick, no GIC

C helpers (Swift can't `mrs`/`msr`): `read_cntfrq` (`CNTFRQ_EL0`, **[BCM2711]** = 54 MHz),
`write_cntp_tval`, `write_cntp_ctl`, `read_cntp_ctl`. Wait one second: `TVAL = freq`; `CTL =
ENABLE|IMASK` (`0x3`); spin until `CTL.ISTATUS` (bit2) — **ISTATUS is pollable regardless of IMASK**;
`CTL = 0`. Needs the boot stub's `CNTHCTL_EL2` bits.

## 6. GIC-400 (GICv2) IRQ routing **[BCM2711]**

- **GICD = `0xFF841000`, GICC = `0xFF842000`** (GICD+0x1000 = GICC). CNTP NS EL1 timer = **PPI 14 =
  INTID 30**, level-sensitive.
- **Security:** stock armstub hands off NS with everything already **Group 1**; `IGROUPR` is RAZ/WI
  from NS → don't set it. NS `GICD_CTLR`/`GICC_CTLR` bit0 = EnableGrp1.
- **Init:** `GICD_CTLR=0` → `ICFGR1`(`0xFF841C04`) clear bits`[29:28]` (level) → `IPRIORITYR7`
  (`0xFF84141C`) bits`[23:16]`=`0xA0` (**word-RMW — 32-bit MMIO can't byte-write**) → `ISENABLER0`
  (`0xFF841100`)=`1<<30` → `GICD_CTLR=1` → `GICC_PMR`(`0xFF842004`)=`0xFF` (**reset 0 masks ALL**) →
  `GICC_CTLR`(`0xFF842000`)=1.
- **Timer for IRQ:** `CNTP_CTL = 0x1` (ENABLE, **IMASK=0** — differs from polled's `0x3`). Then
  `msr daifclr,#2` to unmask the PE, and idle in `wfi`.
- **Handler:** read `GICC_IAR`(`0xFF84200C`); `intid = iar & 0x3FF`; **skip EOI on 1022/1023**
  (spurious/secure); for the timer **re-arm BEFORE `GICC_EOIR`**(`0xFF842010`)=iar (level-triggered →
  EOI-before-clear = infinite re-pend lockup).
- **Vector:** EL1h ⇒ IRQ lands at `VBAR_EL1 + 0x280` (Current-EL/SPx IRQ), NOT `0x080`. `irq_entry`
  saves `x0–x18,x30` + `ELR_EL1`/`SPSR_EL1`, `bl` the handler, restores, `eret`.

## 7. Embedded Swift gotchas

- **No mutable global `var`** (Swift 6 "nonisolated global shared mutable state"). Use `let`, recompute,
  or `nonisolated(unsafe) var` for single-core IRQ-only state.
- **`swift_beginAccess`** (emitted for `nonisolated(unsafe)` globals) links to a **no-op `ret` stub** —
  safe in IRQ context.
- **Interrupt/exception handlers must be integer-only.** If the compiler emits FP/SIMD (`q*`) it traps
  (CPACR) or corrupts state. Disassembly-check the handler for `q/v` regs; add `-mgeneral-regs-only` if any.
- `@_cdecl("name")` exposes a Swift fn with a C symbol for asm to `bl`; `@main` produces `main`.

## 8. Verification toolkit

- **Re-run the build yourself** — don't trust an agent's claim.
- **`llvm-objdump -d`** + check the actual register addresses/values/order. Note functions get inlined
  into specialized `main` (`…Tf4d_n`); search by address, not just symbol.
- **Flash-verify by md5** (`src` vs on-card), not "cp returned 0".
- **Serial capture:** a Python `termios` reader at 115200 8N1 → one raw log + one host-timestamped log
  (to measure tick/IRQ cadence). Single sink — don't `os.write(1,…)` to a stdout that's also redirected
  to the same file (double-logging).
- `diskutil eject` can fail "in use" with zero user-`lsof` holders (system process); `diskutil unmount
  force` is safe after `sync` with no pending writes.
