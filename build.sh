#!/usr/bin/env bash
#===----------------------------------------------------------------------===#
# Build AetherKernel -> kernel8.img (bare-metal Raspberry Pi 4B / AArch64).
#
# Mach-O path (swift-6.3.2-RELEASE): targets arm64-apple-none-macho, the only
# AArch64 triple for which the toolchain ships the Embedded _Concurrency module
# (async/await). Linking uses ld64 via a toolset; macho2bin.py extracts the flat
# kernel8.img. (The prior ELF path — aarch64-none-none-elf + lld + linker script
# + elf2bin.py — is preserved on the `main`/`elf-baseline` branches; that triple
# has no Embedded concurrency.)
#===----------------------------------------------------------------------===#
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

TOOLCHAIN="$HOME/Library/Developer/Toolchains/swift-6.3.2-RELEASE.xctoolchain/usr/bin"
TRIPLE=arm64-apple-none-macho

"$TOOLCHAIN/swift" build \
  --configuration release \
  --triple "$TRIPLE" \
  --toolset Toolsets/rpi4-macho.json

BIN=".build/$TRIPLE/release/Application"
echo "==> extracting flat binary -> kernel8.img"
# macho2bin lays the named segments out by VM address from --base-address,
# zero-filling gaps. __BOOT (pinned to 0x80000) carries _start first.
uv run ./macho2bin.py "$BIN" kernel8.img \
  --base-address 0x80000 \
  --segments '__BOOT,__TEXT,__DATA' \
  --max-end-address 0x400000
echo "==> kernel8.img:"
ls -la kernel8.img
echo "==> done."
