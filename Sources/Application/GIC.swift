//===----------------------------------------------------------------------===//
// GIC-400 (GICv2) driver.
//===----------------------------------------------------------------------===//
import Support

private let GICD: UInt = 0xFF84_1000
private let GICC: UInt = 0xFF84_2000

let CNTP_GIC_INTID: UInt32 = 30
// BCM2711: UART0 is GIC_SPI 121 in the Linux device tree, so architectural
// GIC INTID is 32 + 121 = 153.
let UART0_GIC_INTID: UInt32 = 153

// GICD register offsets
private let GICD_CTLR: UInt = 0x000
private let GICD_ISENABLER: UInt = 0x100
private let GICD_IPRIORITYR: UInt = 0x400
private let GICD_ITARGETSR: UInt = 0x800
private let GICD_ICFGR: UInt = 0xC00

// GICC register offsets
private let GICC_CTLR: UInt = 0x000
private let GICC_PMR: UInt = 0x004
private let GICC_IAR: UInt = 0x00C
private let GICC_EOIR: UInt = 0x010

func gicEnableInterrupt(_ intid: UInt32) {
  gicSetTargetCpu0(intid)

  // Level-sensitive: ICFGR has two bits per INTID; 0b00 is level triggered.
  let cfgOffset = GICD_ICFGR + UInt((intid / 16) * 4)
  let cfgShift = (intid % 16) * 2
  var c = mmio_read32(GICD + cfgOffset)
  c &= ~(UInt32(0x3) << cfgShift)
  mmio_write32(GICD + cfgOffset, c)

  // Priority 0xA0. GICv2 packs four priority bytes per word.
  let priorityOffset = GICD_IPRIORITYR + UInt((intid / 4) * 4)
  let priorityShift = (intid % 4) * 8
  var p = mmio_read32(GICD + priorityOffset)
  p &= ~(UInt32(0xFF) << priorityShift)
  p |= UInt32(0xA0) << priorityShift
  mmio_write32(GICD + priorityOffset, p)

  // Enable bit. ISENABLER is write-one-to-set.
  let enableOffset = GICD_ISENABLER + UInt((intid / 32) * 4)
  let enableBit = intid % 32
  mmio_write32(GICD + enableOffset, UInt32(1) << enableBit)
}

func gicSetTargetCpu0(_ intid: UInt32) {
  if intid < 32 {
    return
  }

  let targetOffset = GICD_ITARGETSR + UInt((intid / 4) * 4)
  let targetShift = (intid % 4) * 8
  var t = mmio_read32(GICD + targetOffset)
  t &= ~(UInt32(0xFF) << targetShift)
  t |= UInt32(0x01) << targetShift
  mmio_write32(GICD + targetOffset, t)
}

func gicInitRuntimeIRQs() {
  // GICD_CTLR = 0 (quiesce distributor)
  mmio_write32(GICD + GICD_CTLR, 0)

  gicEnableInterrupt(CNTP_GIC_INTID)
  gicEnableInterrupt(UART0_GIC_INTID)

  // GICD_CTLR = 1 (EnableGrp1, NS view)
  mmio_write32(GICD + GICD_CTLR, 1)

  // GICC_PMR = 0xFF (open priority gate; masks nothing)
  mmio_write32(GICC + GICC_PMR, 0xFF)

  // GICC_CTLR = 1 (EnableGrp1)
  mmio_write32(GICC + GICC_CTLR, 1)
}

func gicInitTimerIRQ() {
  gicInitRuntimeIRQs()
}

func gicAck() -> UInt32 {
  mmio_read32(GICC + GICC_IAR)
}

func gicEoi(_ iar: UInt32) {
  mmio_write32(GICC + GICC_EOIR, iar)
}
