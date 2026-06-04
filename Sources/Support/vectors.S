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
    VECTOR exc_common   //                   IRQ
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
