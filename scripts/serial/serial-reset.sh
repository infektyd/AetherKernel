#!/usr/bin/env bash
#===----------------------------------------------------------------------===#
# Send the AetherKernel serial reboot command over the USB-TTL adapter.
#
#   usage: ./serial-reset.sh [serial-port]
#
# Defaults:
#   serial-port: $AETHER_SERIAL_PORT, /dev/cu.usbserial-B0044J1V, or the first
#                /dev/cu.usbserial-* device.
#===----------------------------------------------------------------------===#
set -euo pipefail

PORT="${1:-${AETHER_SERIAL_PORT:-}}"
PAYLOAD="${AETHER_SERIAL_RESET_PAYLOAD:-r}"

usage() {
  sed -n '2,11p' "$0" | sed 's/^# \{0,1\}//'
}

die() {
  echo "serial-reset: $*" >&2
  exit 1
}

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
  usage
  exit 0
fi

if [ -z "$PORT" ]; then
  if [ -e /dev/cu.usbserial-B0044J1V ]; then
    PORT="/dev/cu.usbserial-B0044J1V"
  else
    PORT="$(ls /dev/cu.usbserial-* 2>/dev/null | head -n 1 || true)"
  fi
fi

[ -n "$PORT" ] || die "no USB serial port found"

if [ "${AETHER_SERIAL_RESET_DRY_RUN:-0}" = "1" ]; then
  echo "serial port: $PORT"
  echo "payload: $PAYLOAD"
  echo "frame: ${PAYLOAD}\\n"
  exit 0
fi

python3 - "$PORT" "$PAYLOAD" <<'PY'
import os
import sys
import termios
import time

port = sys.argv[1]
payload = (sys.argv[2] + "\n").encode("ascii")

fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
try:
    attrs = termios.tcgetattr(fd)
    attrs[0] = 0
    attrs[1] = 0
    attrs[2] = termios.CREAD | termios.CLOCAL | termios.CS8
    attrs[3] = 0
    attrs[4] = termios.B115200
    attrs[5] = termios.B115200
    attrs[6][termios.VMIN] = 0
    attrs[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    time.sleep(0.05)
    os.write(fd, payload)
    termios.tcdrain(fd)
finally:
    os.close(fd)
PY

echo "sent serial reset payload to $PORT"
