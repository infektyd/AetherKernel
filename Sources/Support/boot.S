//===----------------------------------------------------------------------===//
// AetherKernel boot stub.
//
// The stock Pi 4 armstub hands control to kernel8.img at EL2 on the primary
// core, with the other cores also entering this stub on the Pi 4. We: release
// secondaries into fixed C accounting loops, drop EL2->EL1, set per-core stacks,
// install vectors, and branch only the primary core to Swift `main`.
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
.equ KERNEL_SMP_CORE_CAPACITY, 4
.equ KERNEL_SMP_STACK_BYTES, 4096
.equ ARMSTUB_SPIN_CPU1, 0xe0
.equ ARMSTUB_SPIN_CPU2, 0xe8
.equ ARMSTUB_SPIN_CPU3, 0xf0

.global _start

_start:
    // Runtime V32 secondary-core release. Affinity level 0 gives the Pi 4 core.
    mrs     x20, mpidr_el1
    and     x19, x20, #3
    cbz     x19, primary

secondary_wait:
    ldr     x2, =_kernel_smp_release_flag
    ldr     w3, [x2]
    cbnz    w3, secondary
    wfe
    b       secondary_wait

primary:
    // If firmware already left us in EL1, skip the drop entirely.
    mrs     x0, CurrentEL
    lsr     x0, x0, #2           // CurrentEL holds the level in bits [3:2]
    cmp     x0, #2               // EL2?
    b.ne    primary_el1_entry

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

    // Resume in EL1 at primary_el1_entry.
    adr     x0, primary_el1_entry
    msr     elr_el2, x0
    eret

primary_el1_entry:
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

    bl      _kernel_smp_init
    mov     x0, x20
    bl      _kernel_smp_note_primary

    // Release default Raspberry Pi armstub8 secondaries from its 64-bit
    // spin-table slots, then let them re-enter our _start and take the V32
    // secondary path. Clean the slots and our release flag because parked cores
    // are polling with MMU/caches off.
    ldr     x0, =_start
    mov     x1, #ARMSTUB_SPIN_CPU1
    str     x0, [x1]
    dc      cvac, x1
    mov     x1, #ARMSTUB_SPIN_CPU2
    str     x0, [x1]
    dc      cvac, x1
    mov     x1, #ARMSTUB_SPIN_CPU3
    str     x0, [x1]
    dc      cvac, x1

    ldr     x1, =_kernel_smp_release_flag
    mov     w0, #1
    str     w0, [x1]
    dc      cvac, x1
    dsb     sy
    sev

    // BSS is already zero in the loaded image (-no_zero_fill_sections), so jump
    // straight into Swift. (Mach-O: Swift's @main entry is the C symbol `_main`.)
    bl      _main
halt:
    wfe
    b       halt

secondary:
    // If firmware already left us in EL1, skip the drop entirely.
    mrs     x0, CurrentEL
    lsr     x0, x0, #2
    cmp     x0, #2
    b.ne    secondary_el1_entry

    // --- EL2 -> EL1 transition for secondary cores ---
    mov     x0, #(1 << 31)
    msr     hcr_el2, x0

    ldr     x0, =0x30d00800
    msr     sctlr_el1, x0

    mov     x0, #3
    msr     cnthctl_el2, x0
    msr     cntvoff_el2, xzr

    mov     x0, #0x3c5
    msr     spsr_el2, x0

    adr     x0, secondary_el1_entry
    msr     elr_el2, x0
    eret

secondary_el1_entry:
    // Private 4 KiB stack per core, indexed by affinity-0 core ID.
    ldr     x2, =_kernel_smp_core_stacks
    mov     x3, #KERNEL_SMP_STACK_BYTES
    mul     x4, x19, x3
    add     x2, x2, x4
    add     x2, x2, x3
    mov     sp, x2

    ldr     x0, =vectors
    msr     vbar_el1, x0

    mov     x0, #(3 << 20)
    msr     cpacr_el1, x0
    isb

    bl      _mmu_enable

    mov     x0, x19
    mov     x1, x20
    bl      _kernel_smp_secondary_entry
secondary_halt:
    wfe
    b       secondary_halt

.section __DATA,__data
.balign 16
.global _kernel_smp_release_flag
_kernel_smp_release_flag:
    .word 0
    .word 0
    .word 0
    .word 0

.balign 16
.global _kernel_smp_core_stacks
_kernel_smp_core_stacks:
    .space (KERNEL_SMP_CORE_CAPACITY * KERNEL_SMP_STACK_BYTES)
