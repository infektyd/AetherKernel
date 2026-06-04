// swift-tools-version: 6.0
import PackageDescription

// AetherKernel — bare-metal Raspberry Pi 4B (BCM2711) kernel in Embedded Swift.
// No swift-mmio: its macros pull in swift-syntax, which the 6.0 toolchain (the
// one carrying the Embedded aarch64 stdlib) cannot compile against macOS SDK 26.
// We do MMIO via a tiny C volatile shim in the Support target instead.
let package = Package(
  name: "AetherKernel",
  products: [
    .executable(name: "Application", targets: ["Application"])
  ],
  dependencies: [],
  targets: [
    .executableTarget(
      name: "Application",
      dependencies: ["Support"]),
    .target(name: "Support"),
  ])
