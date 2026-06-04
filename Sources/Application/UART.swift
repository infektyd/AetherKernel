//===----------------------------------------------------------------------===//
// PL011 UART0 driver for BCM2711 (Raspberry Pi 4B).
//
// Polled, transmit-focused. Firmware (enable_uart=1 + dtoverlay=disable-bt)
// already muxes GPIO14/15 to PL011 and brings the clock up; we still program a
// known-good config so we don't depend on firmware defaults.
//===----------------------------------------------------------------------===//
import Support

let UART0_BASE: UInt = 0xFE20_1000

// Register offsets (ARM PL011 TRM).
private let DR: UInt = 0x00    // Data register
private let FR: UInt = 0x18    // Flag register
private let IBRD: UInt = 0x24  // Integer baud divisor
private let FBRD: UInt = 0x28  // Fractional baud divisor
private let LCRH: UInt = 0x2C  // Line control
private let CR: UInt = 0x30    // Control register
private let IMSC: UInt = 0x38  // Interrupt mask set/clear
private let ICR: UInt = 0x44   // Interrupt clear

private let FR_TXFF: UInt32 = 1 << 5  // Transmit FIFO full

func uartInit() {
  // Route GPIO14/15 to ALT0 (PL011) ourselves — do not depend on the firmware
  // overlay. (Hardware-verified: without this the kernel ran but sent nothing
  // to the header.)
  uartPinsInit()
  // Disable the UART while we reconfigure it.
  mmio_write32(UART0_BASE + CR, 0)
  // Clear all pending interrupts.
  mmio_write32(UART0_BASE + ICR, 0x7FF)
  // Baud 115200 against the firmware's 48 MHz PL011 reference clock:
  //   divisor = 48_000_000 / (16 * 115200) = 26.0417
  //   IBRD = 26 ; FBRD = round(0.0417 * 64) = 3
  mmio_write32(UART0_BASE + IBRD, 26)
  mmio_write32(UART0_BASE + FBRD, 3)
  // 8 data bits, FIFO enabled: WLEN(0b11 << 5) | FEN(1 << 4) = 0x70.
  mmio_write32(UART0_BASE + LCRH, 0x70)
  // Mask all interrupts (we poll).
  mmio_write32(UART0_BASE + IMSC, 0)
  // Enable UART + TX + RX: UARTEN(1<<0) | TXE(1<<8) | RXE(1<<9) = 0x301.
  mmio_write32(UART0_BASE + CR, 0x301)
}

@inline(__always)
func uartPutc(_ c: UInt8) {
  // Spin until the transmit FIFO has room.
  while (mmio_read32(UART0_BASE + FR) & FR_TXFF) != 0 { nop() }
  mmio_write32(UART0_BASE + DR, UInt32(c))
}

func uartPuts(_ s: StaticString) {
  let p = s.utf8Start
  let n = s.utf8CodeUnitCount
  var i = 0
  while i < n {
    let b = p[i]
    if b == 0x0A { uartPutc(0x0D) }  // LF -> CR LF for terminals
    uartPutc(b)
    i += 1
  }
}

// Print a 64-bit value as 0x-prefixed hex. Handy for CurrentEL / ESR / FAR.
func uartPutHex(_ value: UInt64) {
  uartPutc(0x30); uartPutc(0x78)  // "0x"
  let digits: StaticString = "0123456789abcdef"
  let dp = digits.utf8Start
  var shift = 60
  while shift >= 0 {
    let nibble = Int((value >> UInt64(shift)) & 0xF)
    uartPutc(dp[nibble])
    shift -= 4
  }
}
