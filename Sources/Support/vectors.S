//===----------------------------------------------------------------------===//
// AArch64 EL1 exception vector table.
//
// VBAR_EL1 must be 2048-byte aligned; the table is 16 entries of 0x80 bytes:
//   {Current EL SP0, Current EL SPx, Lower EL AArch64, Lower EL AArch32}
//   x {Synchronous, IRQ, FIQ, SError}.
//
// For bring-up, every entry routes to one common handler that reads the
// syndrome registers and calls Swift to print them, then halts. ESR already
// encodes the cause, so we don't yet need per-vector specialization.
//===----------------------------------------------------------------------===//

.macro VECTOR label
.balign 0x80
    b \label
.endm

.section ".text"
.balign 0x800
.global vectors
vectors:
    VECTOR exc_common   // Current EL, SP0:  Synchronous
    VECTOR exc_common   //                   IRQ
    VECTOR exc_common   //                   FIQ
    VECTOR exc_common   //                   SError
    VECTOR exc_common   // Current EL, SPx:  Synchronous
    VECTOR irq_entry    //                   IRQ
    VECTOR exc_common   //                   FIQ
    VECTOR exc_common   //                   SError
    VECTOR exc_common   // Lower EL, AArch64: Synchronous
    VECTOR exc_common   //                   IRQ
    VECTOR exc_common   //                   FIQ
    VECTOR exc_common   //                   SError
    VECTOR exc_common   // Lower EL, AArch32: Synchronous
    VECTOR exc_common   //                   IRQ
    VECTOR exc_common   //                   FIQ
    VECTOR exc_common   //                   SError

// Common handler: gather syndrome and hand to Swift. We are halting, so
// clobbering caller-saved registers is fine.
exc_common:
    mrs     x0, esr_el1
    mrs     x1, elr_el1
    mrs     x2, far_el1
    bl      kernel_exception_handler
exc_hang:
    wfe
    b       exc_hang

.global irq_entry
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

