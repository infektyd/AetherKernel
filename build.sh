#!/usr/bin/env bash
#===----------------------------------------------------------------------===#
# Build AetherKernel -> kernel8.img (bare-metal Raspberry Pi 4B / AArch64).
#
# Uses the swift-6.0-RELEASE toolchain because it carries the Embedded stdlib
# for aarch64-none-none-elf. Flags are the toolset JSON translated to -Xswiftc/
# -Xlinker (this toolchain's SwiftPM has no --toolset). lld + a tiny Python
# extractor replace llvm-objcopy (not installed; brew llvm is too big for disk).
#===----------------------------------------------------------------------===#
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

TOOLCHAIN=/Library/Developer/Toolchains/swift-6.0-RELEASE.xctoolchain/usr/bin
TRIPLE=aarch64-none-none-elf

"$TOOLCHAIN/swift" build \
  --configuration release \
  --triple "$TRIPLE" \
  -Xswiftc -enable-experimental-feature -Xswiftc Embedded \
  -Xswiftc -Xfrontend -Xswiftc -disable-stack-protector \
  -Xswiftc -Xfrontend -Xswiftc -function-sections \
  -Xswiftc -Xclang-linker -Xswiftc -fuse-ld=lld \
  -Xswiftc -Xclang-linker -Xswiftc -nostdlib \
  -Xlinker -T -Xlinker Sources/Support/linkerscript.ld \
  -Xlinker --unresolved-symbols=ignore-in-object-files

BIN=".build/$TRIPLE/release/Application"
echo "==> extracting raw binary -> kernel8.img"
python3 elf2bin.py "$BIN" kernel8.img
echo "==> kernel8.img:"
ls -la kernel8.img
echo "==> done."
