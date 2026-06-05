#!/usr/bin/env bash
#===----------------------------------------------------------------------===#
# Build and stage AetherKernel into the network-boot TFTP prefix.
#
#   usage: ./netflash.sh [tftp-root]
#
# Defaults:
#   tftp-root  : $AETHER_TFTP_ROOT or ~/aether-tftp
#   prefix dir : $AETHER_TFTP_PREFIX or aether
#===----------------------------------------------------------------------===#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TFTP_ROOT="${1:-${AETHER_TFTP_ROOT:-$HOME/aether-tftp}}"
PREFIX="${AETHER_TFTP_PREFIX:-aether}"
PREFIX="${PREFIX#/}"
PREFIX="${PREFIX%/}"
KERNEL_IMG="${AETHER_KERNEL_IMG:-$SCRIPT_DIR/kernel8.img}"
CONFIG_TXT="${AETHER_CONFIG_TXT:-$SCRIPT_DIR/config.txt}"

usage() {
  sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'
}

die() {
  echo "netflash: $*" >&2
  exit 1
}

sha256_file() {
  shasum -a 256 "$1" | awk '{print $1}'
}

verify_copy() {
  local src="$1"
  local dst="$2"
  local src_hash
  local dst_hash
  src_hash="$(sha256_file "$src")"
  dst_hash="$(sha256_file "$dst")"
  if [ "$src_hash" != "$dst_hash" ]; then
    echo "verify failed for $(basename "$dst")"
    echo "  source: $src_hash"
    echo "  target: $dst_hash"
    exit 1
  fi
  echo "verified $(basename "$dst") sha256 $dst_hash"
}

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
  usage
  exit 0
fi

[ -n "$PREFIX" ] || die "AETHER_TFTP_PREFIX must not be empty"

if [ "${AETHER_NETFLASH_SKIP_BUILD:-0}" != "1" ]; then
  "$SCRIPT_DIR/build.sh"
fi

[ -f "$KERNEL_IMG" ] || die "kernel image missing: $KERNEL_IMG"
[ -f "$CONFIG_TXT" ] || die "config.txt missing: $CONFIG_TXT"

DEST="$TFTP_ROOT/$PREFIX"
[ -d "$DEST" ] || die "TFTP prefix missing: $DEST (run ./prepare-tftp.sh first)"

cp "$KERNEL_IMG" "$DEST/kernel8.img"
cp "$CONFIG_TXT" "$DEST/config.txt"
sync

verify_copy "$KERNEL_IMG" "$DEST/kernel8.img"
verify_copy "$CONFIG_TXT" "$DEST/config.txt"
echo "netflashed kernel8.img + config.txt -> $DEST"
echo "reset the Pi and watch /tmp/aether-serial.log for TFTP_GET + Runtime V3 shell ready"
