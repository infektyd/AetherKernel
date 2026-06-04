//===----------------------------------------------------------------------===//
// GIC-400 (GICv2) driver for timer interrupts.
//===----------------------------------------------------------------------===//
import Support

private let GICD: UInt = 0xFF84_1000
private let GICC: UInt = 0xFF84_2000

// GICD register offsets
private let GICD_CTLR: UInt = 0x000
private let GICD_ISENABLER0: UInt = 0x100
private let GICD_IPRIORITYR7: UInt = 0x41C
private let GICD_ICFGR1: UInt = 0xC04

// GICC register offsets
private let GICC_CTLR: UInt = 0x000
private let GICC_PMR: UInt = 0x004
private let GICC_IAR: UInt = 0x00C
private let GICC_EOIR: UInt = 0x010

func gicInitTimerIRQ() {
  // GICD_CTLR = 0 (quiesce distributor)
  mmio_write32(GICD + GICD_CTLR, 0)

  // GICD_ICFGR1: ensure INTID 30 level-sensitive (bits [29:28] = 0b00)
  var c = mmio_read32(GICD + GICD_ICFGR1)
  c &= ~(0x3 << 28)
  mmio_write32(GICD + GICD_ICFGR1, c)

  // GICD_IPRIORITYR7: INTID 30 priority = 0xA0 in bits [23:16] (word RMW)
  var p = mmio_read32(GICD + GICD_IPRIORITYR7)
  p &= ~(0xFF << 16)
  p |= (0xA0 << 16)
  mmio_write32(GICD + GICD_IPRIORITYR7, p)

  // GICD_ISENABLER0: enable INTID 30 (write 1 << 30)
  mmio_write32(GICD + GICD_ISENABLER0, 1 << 30)

  // GICD_CTLR = 1 (EnableGrp1, NS view)
  mmio_write32(GICD + GICD_CTLR, 1)

  // GICC_PMR = 0xFF (open priority gate; masks nothing)
  mmio_write32(GICC + GICC_PMR, 0xFF)

  // GICC_CTLR = 1 (EnableGrp1)
  mmio_write32(GICC + GICC_CTLR, 1)
}

func gicAck() -> UInt32 {
  mmio_read32(GICC + GICC_IAR)
}

func gicEoi(_ iar: UInt32) {
  mmio_write32(GICC + GICC_EOIR, iar)
}
