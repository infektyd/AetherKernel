//===----------------------------------------------------------------------===//
// AetherKernel boot stub.
//
// The stock Pi 4 armstub hands control to kernel8.img at EL2 on the primary
// core, secondaries parked. We: park secondaries, drop EL2->EL1, set the
// stack, install vectors, and branch to Swift `main`.
//
// Every system-register write in the EL drop is deliberate; see comments.
//
// Mach-O build: this lives in a dedicated __BOOT segment pinned to 0x80000
// (the Pi firmware's kernel8.img entry point) via the linker's
// `-segaddr __BOOT 0x80000`, so _start is the first byte of the image. BSS is
// pre-zeroed in the image by `-no_zero_fill_sections`, so there is no runtime
// BSS-zeroing loop (and no linker-script __bss_start/__bss_size symbols).
//===----------------------------------------------------------------------===//

.section __BOOT,__text,regular,pure_instructions
.global _start

_start:
    // Only the primary core (affinity 0) proceeds; park the others.
    mrs     x1, mpidr_el1
    and     x1, x1, #3
    cbz     x1, primary
park:
    wfe
    b       park

primary:
    // If firmware already left us in EL1, skip the drop entirely.
    mrs     x0, CurrentEL
    lsr     x0, x0, #2           // CurrentEL holds the level in bits [3:2]
    cmp     x0, #2               // EL2?
    b.ne    el1_entry

    // --- EL2 -> EL1 transition ---

    // EL1 executes in AArch64 (HCR_EL2.RW, bit 31).
    mov     x0, #(1 << 31)
    msr     hcr_el2, x0

    // Known-good SCTLR_EL1: MMU and caches OFF, architectural RES1 bits set
    // (11, 20, 22, 23, 28, 29). 0x30d00800.
    ldr     x0, =0x30d00800
    msr     sctlr_el1, x0

    // Let EL1 read the physical counter/timer (CNTHCTL_EL2.EL1PCTEN|EL1PCEN),
    // and zero the virtual offset.
    mov     x0, #3
    msr     cnthctl_el2, x0
    msr     cntvoff_el2, xzr

    // Return state: EL1h (M[3:0]=0b0101) with D,A,I,F masked. 0x3c5.
    mov     x0, #0x3c5
    msr     spsr_el2, x0

    // Resume in EL1 at el1_entry.
    adr     x0, el1_entry
    msr     elr_el2, x0
    eret

el1_entry:
    // Stack grows down from our load address (_start == 0x80000).
    ldr     x1, =_start
    mov     sp, x1

    // Install the EL1 exception vectors so faults report instead of hanging.
    ldr     x0, =vectors
    msr     vbar_el1, x0

    // Enable FP/SIMD access at EL1/EL0 (CPACR_EL1.FPEN = 0b11, bits [21:20]).
    // The precompiled Embedded _Concurrency runtime contains FP/NEON; without
    // this the first such instruction traps to the sync vector. (Our own kernel
    // code is integer-only, which is why MS1-3 didn't need this.)
    mov     x0, #(3 << 20)
    msr     cpacr_el1, x0
    isb

    // Enable the MMU with Normal Inner-Shareable cacheable RAM (identity-mapped).
    // The Embedded _Concurrency runtime uses ldxr/stxr atomics, which have no
    // exclusive monitor on Device memory (MMU off) — swift_task_create's CAS loop
    // would spin forever. Must run before any Swift concurrency code.
    bl      _mmu_enable

    // BSS is already zero in the loaded image (-no_zero_fill_sections), so jump
    // straight into Swift. (Mach-O: Swift's @main entry is the C symbol `_main`.)
    bl      _main
halt:
    wfe
    b       halt
