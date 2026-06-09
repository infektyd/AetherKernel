#!/usr/bin/env bash
#===----------------------------------------------------------------------===#
# Seed a Raspberry Pi 4 network-boot TFTP tree from a known-working bootfs.
#
#   usage: ./prepare-tftp.sh [boot-source] [tftp-root]
#          ./prepare-tftp.sh --download [tftp-root]
#
# Defaults:
#   boot-source: /Volumes/bootfs
#   tftp-root       : $AETHER_TFTP_ROOT or ~/aether-tftp
#   prefix dir      : $AETHER_TFTP_PREFIX or aether
#   firmware mirror : $AETHER_FIRMWARE_BASE_URL or Raspberry Pi firmware master
#
# The Pi EEPROM config in RUNBOOK.md uses TFTP_PREFIX_STR=aether/, so boot files
# live under "$tftp-root/aether". By default this script copies the firmware tree
# from an SD boot partition. With --download, it downloads the minimal Pi 4 boot
# firmware set instead, then writes this repo's current config.txt. The TFTP
# prefix intentionally omits generic start.elf/fixup.dat fallback files because
# the Pi 4 bench can hang after loading that fallback path.
#===----------------------------------------------------------------------===#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BOOT_SOURCE="${1:-/Volumes/bootfs}"
TFTP_ROOT="${2:-${AETHER_TFTP_ROOT:-$HOME/aether-tftp}}"
PREFIX="${AETHER_TFTP_PREFIX:-aether}"
PREFIX="${PREFIX#/}"
PREFIX="${PREFIX%/}"
FIRMWARE_BASE_URL="${AETHER_FIRMWARE_BASE_URL:-https://raw.githubusercontent.com/raspberrypi/firmware/master/boot}"
DOWNLOAD=0

usage() {
  sed -n '2,18p' "$0" | sed 's/^# \{0,1\}//'
}

die() {
  echo "prepare-tftp: $*" >&2
  exit 1
}

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
  usage
  exit 0
fi

if [ "${1:-}" = "--download" ]; then
  DOWNLOAD=1
  BOOT_SOURCE=""
  TFTP_ROOT="${2:-${AETHER_TFTP_ROOT:-$HOME/aether-tftp}}"
fi

[ -n "$PREFIX" ] || die "AETHER_TFTP_PREFIX must not be empty"
[ -f "$REPO_ROOT/config.txt" ] || die "repo config.txt missing"

required=(
  "start4.elf"
  "fixup4.dat"
  "bcm2711-rpi-4-b.dtb"
  "overlays/disable-bt.dtbo"
)

prune=(
  "start.elf"
  "fixup.dat"
  "pieeprom.sig"
  "pieeprom.upd"
  "vl805.sig"
  "vl805.bin"
  "recover4.elf"
  "recovery.elf"
)

DEST="$TFTP_ROOT/$PREFIX"
mkdir -p "$DEST"

if [ "$DOWNLOAD" -eq 1 ]; then
  command -v curl >/dev/null 2>&1 || die "curl is required for --download"
  echo "downloading Raspberry Pi firmware:"
  echo "  from: $FIRMWARE_BASE_URL"
  echo "  to:   $DEST"
  for rel in "${required[@]}"; do
    mkdir -p "$DEST/$(dirname "$rel")"
    curl -fsSL "$FIRMWARE_BASE_URL/$rel" -o "$DEST/$rel"
  done
  echo "downloaded Raspberry Pi firmware"
else
  [ -d "$BOOT_SOURCE" ] || die "boot source not found: $BOOT_SOURCE"
  for rel in "${required[@]}"; do
    [ -f "$BOOT_SOURCE/$rel" ] || die "required boot file missing: $BOOT_SOURCE/$rel"
  done

  echo "seeding Pi boot files:"
  echo "  from: $BOOT_SOURCE"
  echo "  to:   $DEST"

  for rel in "${required[@]}"; do
    mkdir -p "$DEST/$(dirname "$rel")"
    COPYFILE_DISABLE=1 cp "$BOOT_SOURCE/$rel" "$DEST/$rel"
  done
fi

cp "$REPO_ROOT/config.txt" "$DEST/config.txt"
for rel in "${prune[@]}"; do
  rm -f "$DEST/$rel"
done
sync

for rel in "${required[@]}" "config.txt"; do
  [ -f "$DEST/$rel" ] || die "seed verification failed: $DEST/$rel"
done

echo "prepared TFTP prefix: $DEST"
echo "next: ./netflash.sh $TFTP_ROOT"
