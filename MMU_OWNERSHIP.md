# AetherKernel MMU Ownership

Runtime V21 records the current EL1 stage-1 MMU boundary before we attempt any
future isolation work. This is a contract note, not a paging design.

## Primary Sources

- Arm memory management guide:
  https://developer.arm.com/-/media/Arm%20Developer%20Community/PDF/Learn%20the%20Architecture/LearnTheArchitecture-MemoryManagement-101811_0100_00_en.pdf
- Raspberry Pi BCM2711 ARM peripherals:
  https://datasheets.raspberrypi.com/bcm2711/bcm2711-peripherals.pdf

## Current Runtime V21 Contract

- AetherKernel uses EL1 stage-1 translation through `TTBR0_EL1`.
- The current table is an identity map: virtual address equals physical address
  for the mapped 32-bit address space.
- `TCR_EL1` selects the current translation regime; with `T0SZ=25`, the low
  `TTBR0_EL1` range covers more than the 4 GiB window this kernel currently
  describes.
- `MAIR_EL1` slot 0 is Normal cacheable memory and slot 1 is Device memory.
- `mmu_enable()` installs one 4 KiB L1 table with 512 entries and four live
  1 GiB block mappings:
  - `0x00000000 -> 0x00000000`, Normal
  - `0x40000000 -> 0x40000000`, Normal
  - `0x80000000 -> 0x80000000`, Normal
  - `0xC0000000 -> 0xC0000000`, Device
- Entries 4 through 511 remain fault entries.

## BCM2711 Boundary

BCM2711 physical RAM starts at `0x0_0000_0000` for the ARM. In Low Peripheral
mode, main peripherals appear to the ARM at `0x0_FC00_0000` through
`0x0_FF7F_FFFF`, and ARM Local peripherals appear at `0x0_FF80_0000` through
`0x0_FFFF_FFFF`. AetherKernel's current fourth 1 GiB Device block covers those
low peripheral aliases.

## Future Remap Invariants

- no dynamic remap in V21.
- Any future map, unmap, permission, or attribute change must invalidate stale
  TLB entries with the correct TLBI/barrier sequence.
- Any future sub-1-GiB mapping needs explicit ownership for the page-table frame
  allocator before it can replace the current static L1-only table.
- Device ranges must not be accidentally reclassified as Normal memory.
- Normal-memory mappings used by Swift concurrency must preserve cacheable
  exclusive-monitor behavior for `ldxr`/`stxr` atomics.
- The first future implementation step should add a page-table allocator and
  table descriptor selftests before enabling a finer-grained remap.
