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

.section __TEXT,__text,regular,pure_instructions
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
    VECTOR el0_sync_entry   // Lower EL, AArch64: Synchronous
    VECTOR el0_irq_entry    //                   IRQ
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
    bl      _kernel_exception_handler
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
    bl  _irq_handler
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

.global el0_sync_entry
el0_sync_entry:
    sub     sp, sp, #272
    stp     x0, x1,   [sp, #0]
    stp     x2, x3,   [sp, #16]
    stp     x4, x5,   [sp, #32]
    stp     x6, x7,   [sp, #48]
    stp     x8, x9,   [sp, #64]
    stp     x10, x11, [sp, #80]
    stp     x12, x13, [sp, #96]
    stp     x14, x15, [sp, #112]
    stp     x16, x17, [sp, #128]
    stp     x18, x19, [sp, #144]
    stp     x20, x21, [sp, #160]
    stp     x22, x23, [sp, #176]
    stp     x24, x25, [sp, #192]
    stp     x26, x27, [sp, #208]
    stp     x28, x29, [sp, #224]
    mrs     x0, sp_el0
    stp     x30, x0,  [sp, #240]
    mrs     x0, elr_el1
    mrs     x1, spsr_el1
    stp     x0, x1,   [sp, #256]

    mov     x0, sp
    bl      _kernel_el0_sync_handler

    ldr     x2, =_kernel_el1_saved_sp
    ldr     x3, [x2]
    cbz     x3, el0_sync_return_to_user

    str     xzr, [x2]
    mov     sp, x3
    ldp     x29, x30, [sp], #16
    ldp     x27, x28, [sp], #16
    ldp     x25, x26, [sp], #16
    ldp     x23, x24, [sp], #16
    ldp     x21, x22, [sp], #16
    ldp     x19, x20, [sp], #16
    mov     x0, #1
    ret

el0_sync_return_to_user:
    ldp     x0, x1,   [sp, #256]
    msr     elr_el1,  x0
    msr     spsr_el1, x1
    ldp     x30, x0,  [sp, #240]
    msr     sp_el0,   x0
    ldp     x28, x29, [sp, #224]
    ldp     x26, x27, [sp, #208]
    ldp     x24, x25, [sp, #192]
    ldp     x22, x23, [sp, #176]
    ldp     x20, x21, [sp, #160]
    ldp     x18, x19, [sp, #144]
    ldp     x16, x17, [sp, #128]
    ldp     x14, x15, [sp, #112]
    ldp     x12, x13, [sp, #96]
    ldp     x10, x11, [sp, #80]
    ldp     x8, x9,   [sp, #64]
    ldp     x6, x7,   [sp, #48]
    ldp     x4, x5,   [sp, #32]
    ldp     x2, x3,   [sp, #16]
    ldp     x0, x1,   [sp, #0]
    add     sp, sp, #272
    eret

.global el0_irq_entry
el0_irq_entry:
    sub     sp, sp, #272
    stp     x0, x1,   [sp, #0]
    stp     x2, x3,   [sp, #16]
    stp     x4, x5,   [sp, #32]
    stp     x6, x7,   [sp, #48]
    stp     x8, x9,   [sp, #64]
    stp     x10, x11, [sp, #80]
    stp     x12, x13, [sp, #96]
    stp     x14, x15, [sp, #112]
    stp     x16, x17, [sp, #128]
    stp     x18, x19, [sp, #144]
    stp     x20, x21, [sp, #160]
    stp     x22, x23, [sp, #176]
    stp     x24, x25, [sp, #192]
    stp     x26, x27, [sp, #208]
    stp     x28, x29, [sp, #224]
    mrs     x0, sp_el0
    stp     x30, x0,  [sp, #240]
    mrs     x0, elr_el1
    mrs     x1, spsr_el1
    stp     x0, x1,   [sp, #256]

    bl      _irq_handler

    ldp     x0, x1,   [sp, #256]
    msr     elr_el1,  x0
    msr     spsr_el1, x1
    ldp     x30, x0,  [sp, #240]
    msr     sp_el0,   x0
    ldp     x28, x29, [sp, #224]
    ldp     x26, x27, [sp, #208]
    ldp     x24, x25, [sp, #192]
    ldp     x22, x23, [sp, #176]
    ldp     x20, x21, [sp, #160]
    ldp     x18, x19, [sp, #144]
    ldp     x16, x17, [sp, #128]
    ldp     x14, x15, [sp, #112]
    ldp     x12, x13, [sp, #96]
    ldp     x10, x11, [sp, #80]
    ldp     x8, x9,   [sp, #64]
    ldp     x6, x7,   [sp, #48]
    ldp     x4, x5,   [sp, #32]
    ldp     x2, x3,   [sp, #16]
    ldp     x0, x1,   [sp, #0]
    add     sp, sp, #272
    eret

.global _kernel_enter_el0_and_wait
_kernel_enter_el0_and_wait:
    stp     x19, x20, [sp, #-16]!
    stp     x21, x22, [sp, #-16]!
    stp     x23, x24, [sp, #-16]!
    stp     x25, x26, [sp, #-16]!
    stp     x27, x28, [sp, #-16]!
    stp     x29, x30, [sp, #-16]!
    
    mov     x2, sp
    ldr     x3, =_kernel_el1_saved_sp
    str     x2, [x3]
    
    msr     sp_el0, x1
    msr     elr_el1, x0
    mov     x0, #0
    msr     spsr_el1, x0
    
    mov     x0, xzr
    mov     x1, xzr
    mov     x2, xzr
    mov     x3, xzr
    mov     x4, xzr
    mov     x5, xzr
    mov     x6, xzr
    mov     x7, xzr
    mov     x8, xzr
    mov     x9, xzr
    mov     x10, xzr
    mov     x11, xzr
    mov     x12, xzr
    mov     x13, xzr
    mov     x14, xzr
    mov     x15, xzr
    mov     x16, xzr
    mov     x17, xzr
    mov     x18, xzr
    mov     x19, xzr
    mov     x20, xzr
    mov     x21, xzr
    mov     x22, xzr
    mov     x23, xzr
    mov     x24, xzr
    mov     x25, xzr
    mov     x26, xzr
    mov     x27, xzr
    mov     x28, xzr
    mov     x29, xzr
    mov     x30, xzr
    eret

.global _el0_test_stub
.global _el0_test_stub_end
_el0_test_stub:
    mov     x0, #0x47
    mov     x1, #0x2026
    svc     #0
1:  b       1b
_el0_test_stub_end:

.global _syscall_test_stub
.global _syscall_test_stub_end
_syscall_test_stub:
    mov     x0, #1
    mov     x1, #0x48
    svc     #0
1:  b       1b
_syscall_test_stub_end:

.section __DATA,__data
.balign 8
.global _kernel_el1_saved_sp
_kernel_el1_saved_sp:
    .quad 0

