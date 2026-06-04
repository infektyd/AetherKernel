#!/usr/bin/env bash
#===----------------------------------------------------------------------===#
# Copy kernel8.img + config.txt onto the Pi's SD boot partition.
# Backs up the originals (once) so you can restore Raspberry Pi OS later.
#   usage: ./flash.sh [boot-mount-path]   (default: /Volumes/bootfs)
#===----------------------------------------------------------------------===#
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BOOT="${1:-/Volumes/bootfs}"

if [ ! -d "$BOOT" ]; then
  echo "Boot partition not found at: $BOOT"
  echo "Mount the SD card and pass its path, e.g. ./flash.sh /Volumes/boot"
  exit 1
fi
if [ ! -f "$SCRIPT_DIR/kernel8.img" ]; then
  echo "kernel8.img missing — run ./build.sh first."
  exit 1
fi

if [ -f "$BOOT/kernel8.img" ] && [ ! -f "$BOOT/kernel8.img.orig" ]; then
  cp "$BOOT/kernel8.img" "$BOOT/kernel8.img.orig"
  echo "backed up original kernel8.img -> kernel8.img.orig"
fi
if [ -f "$BOOT/config.txt" ] && [ ! -f "$BOOT/config.txt.orig" ]; then
  cp "$BOOT/config.txt" "$BOOT/config.txt.orig"
  echo "backed up original config.txt -> config.txt.orig"
fi

cp "$SCRIPT_DIR/kernel8.img" "$BOOT/kernel8.img"
cp "$SCRIPT_DIR/config.txt" "$BOOT/config.txt"
sync
echo "flashed kernel8.img + config.txt -> $BOOT"
echo "eject the SD, move it to the Pi, then open the serial console (see RUNBOOK.md)."
