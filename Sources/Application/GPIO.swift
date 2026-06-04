//===----------------------------------------------------------------------===//
// Minimal GPIO for the green ACT LED (GPIO42) — a secondary liveness signal
// alongside the UART. Same register math the verified blink used.
//===----------------------------------------------------------------------===//
import Support

private let GPIO_BASE: UInt = 0xFE00_0000
private let GPFSEL4: UInt = 0x200010
private let GPSET1: UInt = 0x200020
private let GPCLR1: UInt = 0x20002C

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
