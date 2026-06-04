//===----------------------------------------------------------------------===//
// Exception reporting. vectors.S routes every EL1 exception here with the
// syndrome registers already read, so a fault prints instead of hanging silent.
//
//   ESR_EL1 — Exception Syndrome (EC field [31:26] = the cause class)
//   ELR_EL1 — faulting instruction address
//   FAR_EL1 — faulting data/instruction virtual address (for aborts)
//===----------------------------------------------------------------------===//
import Support

@_cdecl("kernel_exception_handler")
func kernelExceptionHandler(_ esr: UInt64, _ elr: UInt64, _ far: UInt64) {
  uartPuts("\n*** AetherKernel EXCEPTION ***\n")
  uartPuts("ESR_EL1 = ")
  uartPutHex(esr)
  uartPuts("  (EC = ")
  uartPutHex((esr >> 26) & 0x3F)
  uartPuts(")\n")
  uartPuts("ELR_EL1 = ")
  uartPutHex(elr)
  uartPuts("\n")
  uartPuts("FAR_EL1 = ")
  uartPutHex(far)
  uartPuts("\nhalted.\n")
  while true {}
}
