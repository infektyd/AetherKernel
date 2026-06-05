//===----------------------------------------------------------------------===//
// Minimal GPIO for the UART pin mux plus historical green ACT LED (GPIO42)
// helpers. Current liveness is the serial Runtime V2 cadence output.
//===----------------------------------------------------------------------===//
import Support

private let GPIO_BASE: UInt = 0xFE00_0000
private let GPFSEL1: UInt = 0x200004
private let GPFSEL4: UInt = 0x200010
private let GPSET1: UInt = 0x200020
private let GPCLR1: UInt = 0x20002C

// Route GPIO14 (TXD0) and GPIO15 (RXD0) to ALT0 = PL011 UART0 so the kernel's
// serial output reaches header pins 8/10. We do this in code rather than trust
// `dtoverlay=disable-bt` to have done it — verified on hardware: the kernel ran
// but no UART bytes reached the header until the pins were muxed here.
//
// GPFSEL1 holds GPIO10..19, 3 bits each. GPIO14 -> shift (14-10)*3 = 12,
// GPIO15 -> shift 15. ALT0 = 0b100.
func uartPinsInit() {
  var v = mmio_read32(GPIO_BASE + GPFSEL1)
  v &= ~((UInt32(0x7) << 12) | (UInt32(0x7) << 15))  // clear FSEL14, FSEL15
  v |= (UInt32(0x4) << 12) | (UInt32(0x4) << 15)     // ALT0 for both
  mmio_write32(GPIO_BASE + GPFSEL1, v)
}

func ledInit() {
  // GPIO42 function select is bits 6..8 of GPFSEL4; 0b001 = output.
  var v = mmio_read32(GPIO_BASE + GPFSEL4)
  v &= ~(UInt32(0x7) << 6)
  v |= (UInt32(0x1) << 6)
  mmio_write32(GPIO_BASE + GPFSEL4, v)
}

func ledOn() { mmio_write32(GPIO_BASE + GPSET1, UInt32(1) << 10) }
func ledOff() { mmio_write32(GPIO_BASE + GPCLR1, UInt32(1) << 10) }

func delay(_ count: Int = 500_000) {
  var i = 0
  while i < count { nop(); i += 1 }
}
