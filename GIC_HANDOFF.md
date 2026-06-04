# AetherKernel — Milestone 3 handoff: GIC-400 timer IRQ routing

**Implementer:** Gemini (drafts), claude-code (reviews + disassembly-verifies + hardware-tests).
**Status:** FINAL — reconciled against the GIC-400 research (Agent 1), cross-checked vs the GICv2
spec. All prior `[VERIFY]` items resolved; two register bugs corrected (ICFGR1, IPRIORITYR word-access).

---

## 0. Goal

Convert the working **polled** 1-second timer heartbeat into an **interrupt-driven** one via the
BCM2711 **GIC-400** (GICv2). The non-secure EL1 physical timer (CNTP, INTID 30) fires every
second; an EL1 IRQ handler re-arms the timer, toggles the ACT LED, prints `irq N`, and EOIs.
The main loop becomes `wfi` — proving interrupts drive the system, not polling. This is also the
**first real use of the EL1 exception vector table**.

Success on hardware = serial shows `irq 0x0`, `irq 0x1`, … ~1 s apart, LED toggling, with the CPU
otherwise idle in `wfi`.

---

## 1. Current state (do not break)

Toolchain: `swift-6.0-RELEASE`, triple `aarch64-none-none-elf`, Embedded mode. Build: `./build.sh`
→ `kernel8.img` (elf2bin). Swift **cannot** emit `mrs`/`msr` — every system-register access goes
through a C inline-asm helper in `Sources/Support/include/Support.h` (see `read_currentel`,
`read_cntfrq`, etc.). Swift 6 **rejects mutable global `var`** ("nonisolated global shared mutable
state"); for a counter only ever touched from the single-core IRQ handler, use
`nonisolated(unsafe) var` (legitimate here — no concurrency on one core).

Files & roles:
- `Sources/Support/boot.S` — EL2→EL1 drop; sets **EL1h** (uses SP_EL1), `CNTHCTL_EL2=EL1PCTEN|EL1PCEN`,
  `CNTVOFF_EL2=0`, `VBAR_EL1=vectors`, **DAIF all masked**, `bl main`. **DO NOT MODIFY.**
- `Sources/Support/vectors.S` — 16-entry table, every entry currently `b exc_common`; `exc_common`
  reads ESR/ELR/FAR → `kernel_exception_handler` → print + halt. **Edit only the one IRQ slot (below).**
- `Sources/Support/include/Support.h` — MMIO + sysreg C helpers. **Add to it; don't change existing.**
- `Sources/Application/UART.swift` — PL011 driver. **DO NOT MODIFY.**
- `Sources/Application/GPIO.swift` — `ledInit/ledOn/ledOff`, `delay`. Reuse `ledOn/ledOff`.
- `Sources/Application/Timer.swift` — `timerFrequency()`, `timerWaitSeconds()` (polled). **Keep; add a new arm fn.**
- `Sources/Application/Exceptions.swift` — `kernel_exception_handler`. Keep; add the IRQ handler nearby or in a new file.
- `Sources/Application/Application.swift` — `@main`; banner, CurrentEL, CNTFRQ, then the heartbeat loop.

---

## 2. Hard facts (settled unless tagged `[VERIFY]`)

- **GIC-400 base = `0xFF840000`** (ARM low-peripheral view, same view as UART0 `0xFE201000`). CONFIRMED.
  - **GICD (distributor) = `0xFF841000`**
  - **GICC (CPU interface) = `0xFF842000`**
  - (35-bit CPU phys is `0x4_C0041000`; DT shows `@40041000` via a `ranges` translation back to this view.)
- **CNTP (non-secure EL1 physical timer) = PPI 14 = INTID 30.** Level-sensitive (active-low source, inverter integrated; treat as level to the GIC).
- **EL1 IRQ vector = `VBAR_EL1 + 0x280`** (group "Current EL, SPx", IRQ slot) — because boot.S set **EL1h**.
  NOT `0x080` (that's the SP0 group). Wrong slot ⇒ handler never called.

GICD register **absolute addresses** (32-bit MMIO; PPIs are banked per-core, we run core 0 only):
| reg | addr | use |
|---|---|---|
| `GICD_CTLR` | `0xFF841000` | enable distributor (NS bit0 = EnableGrp1) |
| `GICD_ISENABLER0` | `0xFF841100` | enable INTID0–31 → write `(1<<30)` |
| `GICD_IPRIORITYR7` | `0xFF84141C` | **word**; INTID 30 = byte 2 → bits `[23:16]` (RMW, set `0xA0`) |
| `GICD_ICFGR1` | `0xFF841C04` | INTID16–31 cfg; INTID 30 → bits `[29:28]`, clear to `0b00` (level) |
| ~~`GICD_IGROUPR0`~~ | `0xFF841080` | **SKIP** — RAZ/WI from NS; stub already set all to Group 1 |

GICC register **absolute addresses** (from `0xFF842000`):

GICC register offsets (from `0xFF842000`):
| reg | offset | use |
|---|---|---|
| `GICC_CTLR` | `0x000` | enable CPU interface |
| `GICC_PMR` | `0x004` | priority mask |
| `GICC_IAR` | `0x00C` | acknowledge → returns INTID (low 10 bits) |
| `GICC_EOIR` | `0x010` | end-of-interrupt (write the value read from IAR) |

---

## 3. Deliverables

### 3a. `Support.h` additions (C helpers)
```c
// Unmask IRQs at the PE (boot.S left DAIF masked). DAIFClr bit1 = I.
static inline __attribute__((always_inline)) void irq_enable(void)  { __asm__ volatile("msr daifclr, #2" ::: "memory"); }
static inline __attribute__((always_inline)) void irq_disable(void) { __asm__ volatile("msr daifset, #2" ::: "memory"); }
// Idle until an interrupt arrives.
static inline __attribute__((always_inline)) void wait_for_interrupt(void) { __asm__ volatile("wfi"); }
```
(`write_cntp_ctl` for the timer already exists.)

### 3b. `GIC.swift` (new)
- Base constants `GICD = 0xFF84_1000`, `GICC = 0xFF84_2000` and the offsets above.
- `gicInitTimerIRQ()` — the ordered init in §4.
- `func gicAck() -> UInt32 { mmio_read32(GICC + IAR) }`
- `func gicEoi(_ iar: UInt32) { mmio_write32(GICC + EOIR, iar) }`
- Helpers must use **read-modify-write with shifted masks** (clear `(0x...)`, OR shifted value) —
  NOT a raw value into a partial field. (Same trap that bit the GPIO mux: a literal into a wide
  field truncates; verified our compiler emitted the correct 6-bit pattern there. Don't reintroduce it.)

### 3c. `Timer.swift` addition
```swift
// Arm the timer to fire an interrupt after `secs` seconds: load TVAL, enable with IMASK=0.
// (Polled mode used ENABLE|IMASK=0x3; IRQ mode MUST clear IMASK so the interrupt is delivered.)
func timerArmIRQ(_ secs: UInt) {
  write_cntp_tval(timerFrequency() * secs)
  write_cntp_ctl(0x1)   // ENABLE, IMASK=0
}
```

### 3d. `vectors.S` edit — exactly one slot
The table entries in order are: {Cur SP0: Sync,IRQ,FIQ,SError}, {Cur SPx: Sync,**IRQ**,FIQ,SError},
{Lower AArch64 ×4}, {Lower AArch32 ×4}. Change **the 6th entry** (Current EL SPx IRQ, offset `0x280`)
from `VECTOR exc_common` to `VECTOR irq_entry`. Leave the other 15 as `exc_common`.

Add `irq_entry` (save caller-saved GPRs + lr + ELR/SPSR, call Swift, restore, `eret`):
```asm
irq_entry:
    stp x0, x1,  [sp, #-16]!
    stp x2, x3,  [sp, #-16]!
    stp x4, x5,  [sp, #-16]!
    stp x6, x7,  [sp, #-16]!
    stp x8, x9,  [sp, #-16]!
    stp x10, x11, [sp, #-16]!
    stp x12, x13, [sp, #-16]!
    stp x14, x15, [sp, #-16]!
    stp x16, x17, [sp, #-16]!
    stp x18, x30, [sp, #-16]!
    mrs x0, elr_el1
    mrs x1, spsr_el1
    stp x0, x1,  [sp, #-16]!
    bl  irq_handler
    ldp x0, x1,  [sp], #16
    msr spsr_el1, x1
    msr elr_el1, x0
    ldp x18, x30, [sp], #16
    ldp x16, x17, [sp], #16
    ldp x14, x15, [sp], #16
    ldp x12, x13, [sp], #16
    ldp x10, x11, [sp], #16
    ldp x8, x9,  [sp], #16
    ldp x6, x7,  [sp], #16
    ldp x4, x5,  [sp], #16
    ldp x2, x3,  [sp], #16
    ldp x0, x1,  [sp], #16
    eret
```
(Callee-saved x19–x28 are preserved by the Swift handler per AAPCS, so we don't save them. FP/SIMD
not saved: the interrupted context is `wfi`, no FP state to clobber, and the handler is integer-only.)

### 3e. IRQ handler (Swift, `@_cdecl`)
```swift
nonisolated(unsafe) var irqTicks: UInt64 = 0   // only touched in IRQ context (single core)

@_cdecl("irq_handler")
func irqHandler() {
  let iar = gicAck()
  let intid = iar & 0x3FF                  // low 10 bits = INTID
  if intid == 1022 || intid == 1023 { return }  // spurious (1023) / secure-we-can't-ack (1022): NO EOI
  if intid == 30 {                         // CNTP timer
    timerArmIRQ(1)                          // re-arm FIRST — de-asserts the level IRQ before EOI
    if (irqTicks & 1) == 0 { ledOn() } else { ledOff() }
    uartPuts("irq ")
    uartPutHex(irqTicks)
    uartPuts("\n")
    irqTicks &+= 1
  }
  gicEoi(iar)                              // EOI with the exact IAR value (AFTER clearing the timer)
}
```

### 3f. `Application.swift` edit
Keep banner + `CurrentEL` + `CNTFRQ`. Replace the polled heartbeat loop with:
```swift
    gicInitTimerIRQ()
    timerArmIRQ(1)
    uartPuts("IRQ mode: GIC-400 routing CNTP (INTID 30). Idling in wfi.\n")
    irq_enable()
    while true { wait_for_interrupt() }
```

### 3g. `config.txt`
Add `enable_gic=1`. (Auto-enabled under `arm_64bit=1`, but declare it to guarantee the legacy BCM2836
router is off. No special armstub needed — firmware injects `armstub8-2711.bin` natively.)

---

## 4. GIC init sequence (ordered, non-secure EL1, core 0) — `gicInitTimerIRQ()`
All accesses are 32-bit (`mmio_read32`/`mmio_write32`). Read-modify-write where noted.
```
mmio_write32(0xFF841000, 0)                       // GICD_CTLR = 0  (quiesce)
//  IGROUPR0: SKIP — RAZ/WI from NS; armstub already put INTID 30 in Group 1.
//  GICD_ICFGR1: ensure INTID 30 level-sensitive (bits [29:28] = 0)
c = mmio_read32(0xFF841C04); c &= ~(0x3 << 28); mmio_write32(0xFF841C04, c)
//  GICD_IPRIORITYR7: INTID 30 priority = 0xA0 in bits [23:16] (word RMW)
p = mmio_read32(0xFF84141C); p &= ~(0xFF << 16); p |= (0xA0 << 16); mmio_write32(0xFF84141C, p)
mmio_write32(0xFF841100, (1 << 30))               // GICD_ISENABLER0: enable INTID 30
mmio_write32(0xFF841000, 1)                       // GICD_CTLR = 1  (EnableGrp1, NS view)
mmio_write32(0xFF842004, 0xFF)                    // GICC_PMR = 0xFF (open gate; reset value 0 = masks ALL)
mmio_write32(0xFF842000, 1)                       // GICC_CTLR = 1  (EnableGrp1)
```

---

## 5. Failure modes to pre-empt (ranked, reconciled with research)
1. **`GICC_PMR` left at reset `0x00`** ⇒ masks ALL priorities ⇒ silent. Must write `0xFF`.
2. **`DAIF.I` not cleared** (`irq_enable()` / `msr daifclr,#2` missing) ⇒ IRQ never taken at the PE.
3. **IMASK left set** in `timerArmIRQ` (CTL=`0x3` not `0x1`) ⇒ timer fires internally but IRQ masked ⇒ silent.
4. **Re-pend lockup** — EOI written *before* re-arming the (level-triggered) timer ⇒ instant re-assert ⇒ infinite ISR. Re-arm BEFORE `gicEoi`.
5. **Wrong vector slot** (`0x080`/SP0 instead of `0x280`/SPx) ⇒ handler never runs.
6. **EOI omitted / wrong value** ⇒ first IRQ handled, none after.
7. **Re-arm omitted** in handler ⇒ exactly one `irq 0x0` then silence.
- *Already clean (verified, no action):* `HCR_EL2 = 1<<31` only ⇒ `IMO=0`, so IRQs go to EL1 not trapped to EL2; secondaries parked so no core-banked mismatch; `enable_gic` auto + stub GIC-aware.

## 5a. FP/SIMD (CPACR_EL1) — reviewer checkpoint
The IRQ handler must be **integer-only**. If the compiler emits SIMD (`q0–q31`) for a struct/memcpy,
either it traps (if `CPACR_EL1.FPEN` disabled — our default) and halts, or it corrupts FP state.
Our interrupted context is `wfi` (no live FP), so corruption is moot here, but a *trap* would hang.
claude-code will **disassemble the handler and check for FP/SIMD ops**; if any appear, add
`-mgeneral-regs-only` (via `-Xcc`) to the build. Implementer: keep the handler plain integer Swift.

---

## 6. Rules for the implementer (Gemini)
- **Additive & minimal.** New files: `GIC.swift` (+ optionally the IRQ handler file). Edits: `Support.h`
  (append), `Timer.swift` (append `timerArmIRQ`), `vectors.S` (one slot + `irq_entry`),
  `Application.swift` (loop). **Do not touch** `boot.S`, `UART.swift`, `GPIO.swift` existing fns,
  `exc_common`, or the polled timer fns.
- **Output the COMPLETE contents of every new/changed file** as separate fenced code blocks, plus a
  short changelog and any deviation/assumption you made.
- Match existing **code style** (comment density, the C-helper pattern, naming).
- Run `./build.sh`; report the result. **Do NOT claim it works on hardware** — it's untested until
  claude-code flashes it. Flag anything you're unsure of.
- Respect the `[VERIFY]` tags — implement the stated default, but call out that it's pending confirmation.
