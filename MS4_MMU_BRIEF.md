# AetherKernel — MMU enable (Stage 2 unblock): Sources/Support/mmu.c

**Why:** the Embedded _Concurrency runtime uses ldxr/stxr exclusive atomics. With the MMU off
(current state) RAM is Device memory with no exclusive monitor on Cortex-A72 → stxr always fails →
swift_task_create's CAS loop spins forever. Enabling the MMU with Normal Inner-Shareable cacheable RAM
fixes it. Identity map (VA==PA). Plain C, integer-only, AArch64 / Cortex-A72 / BCM2711 (Pi 4B).

## File to CREATE: Sources/Support/mmu.c
Begin with `#include "Support.h"`. Provide exactly one public function: `void mmu_enable(void)`.
Everything else static. No other files — I (the reviewer) wire boot.S + Support.h myself.

### Page table
A single L1 table, 4KB granule, 39-bit VA (T0SZ=25). At this config each L1 entry is a 1GB block.
```c
static unsigned long l1_table[512] __attribute__((aligned(4096)));
```
Identity-map the low 4GB with 1GB blocks; entries 4..511 = 0 (invalid):
- l1_table[0] = PA 0x00000000 (0–1GB): NORMAL  (covers stack@0x80000, image@0x80000–0x90000, heap@0x400000–0x800000, and this table)
- l1_table[1] = PA 0x40000000 (1–2GB): NORMAL
- l1_table[2] = PA 0x80000000 (2–3GB): NORMAL
- l1_table[3] = PA 0xC0000000 (3–4GB): DEVICE  (Pi4 peripherals: UART 0xFE201000, GPIO 0xFE200000, GIC 0xFF840000 — all in 0xFC000000+)

### Block descriptor bit layout (AArch64 stage-1, L1 block)
`descriptor = PA_base | bits`, where PA_base is the 1GB-aligned physical address. Bits:
- bit[0]=1, bit[1]=0  → valid BLOCK at L1  (value 0b01 = 0x1)
- bit[10]=1           → AF (Access Flag); without it the first access faults
- bits[4:2]           → AttrIndx into MAIR (0 = Normal, 1 = Device)
- bits[9:8]           → SH (shareability): 0b11 = Inner Shareable (use for NORMAL); 0b00 for DEVICE
So:
```c
#define DESC_BLOCK   (1UL << 0)
#define DESC_AF      (1UL << 10)
#define SH_INNER     (3UL << 8)
#define ATTRIDX_NORMAL (0UL << 2)
#define ATTRIDX_DEVICE (1UL << 2)
// NORMAL block: PA | DESC_BLOCK | DESC_AF | SH_INNER | ATTRIDX_NORMAL
// DEVICE block: PA | DESC_BLOCK | DESC_AF | ATTRIDX_DEVICE   (SH=0)
```

### Registers
- MAIR_EL1: Attr0 = 0xFF (Normal, Outer+Inner Write-Back non-transient, RW-allocate); Attr1 = 0x00 (Device-nGnRnE).
  `mair = (0xFFUL << 0) | (0x00UL << 8);`
- TCR_EL1 (EL1, TTBR0 only):
  - T0SZ = 25            → bits[5:0]
  - IRGN0 = 0b01 (WB)    → bits[9:8]
  - ORGN0 = 0b01 (WB)    → bits[11:10]
  - SH0 = 0b11 (inner)   → bits[13:12]
  - TG0 = 0b00 (4KB)     → bits[15:14]
  - EPD1 = 1 (no TTBR1)  → bit[23]
  - IPS = 0b000 (32-bit PA, 4GB — enough) → bits[34:32]
  `tcr = 25UL | (1UL<<8) | (1UL<<10) | (3UL<<12) | (0UL<<14) | (1UL<<23) | (0UL<<32);`
- TTBR0_EL1 = (unsigned long)&l1_table[0]
- SCTLR_EL1: read-modify-write to SET M (bit0, MMU), C (bit2, data cache), I (bit12, instr cache).

### Enable sequence (order matters)
```c
void mmu_enable(void) {
    // 1. fill l1_table (loop zero 0..511, then set [0],[1],[2] NORMAL, [3] DEVICE as above)
    // 2. msr mair_el1, mair
    //    msr tcr_el1, tcr
    //    msr ttbr0_el1, &l1_table
    //    isb
    // 3. invalidate TLB + caches barriers:
    //      dsb ishst
    //      tlbi vmalle1
    //      dsb ish
    //      isb
    // 4. read sctlr_el1, set bits 0|2|12, then:
    //      dsb ish
    //      msr sctlr_el1, <new>
    //      isb
}
```
Use `__asm__ volatile("msr <reg>, %0" :: "r"(val) : "memory")` and `"mrs %0, <reg>"`. Barriers via
`__asm__ volatile("dsb ish" ::: "memory")`, `"isb"`, `"tlbi vmalle1"`.

## Constraints / deliverable
- Integer-only, no FP. Match Support.h comment style. Do NOT modify any other file (not boot.S, not
  Support.h, not build.sh, not any .swift) — only CREATE mmu.c.
- Run git status before/after; confirm only Sources/Support/mmu.c was created.
- Print a short CHANGELOG: confirm the exact TCR/MAIR/SCTLR bit values you computed (as hex), and flag
  anything you were unsure of. Do NOT claim it builds — the reviewer builds + disassembly-verifies.
