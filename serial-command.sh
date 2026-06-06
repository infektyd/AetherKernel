#!/usr/bin/env bash
#===----------------------------------------------------------------------===#
# Send one line-oriented AetherKernel UART shell command over USB-TTL.
#
#   usage: ./serial-command.sh [--request-id id] <command> [serial-port]
#
# Defaults:
#   serial-port: $AETHER_SERIAL_PORT, /dev/cu.usbserial-B0044J1V, or the first
#                /dev/cu.usbserial-* device.
#===----------------------------------------------------------------------===#
set -euo pipefail

REQUEST_ID=""
if [ "${1:-}" = "--request-id" ]; then
  REQUEST_ID="${2:-}"
  shift 2 || true
fi

COMMAND="${1:-}"
PORT="${2:-${AETHER_SERIAL_PORT:-}}"

usage() {
  sed -n '2,11p' "$0" | sed 's/^# \{0,1\}//'
}

die() {
  echo "serial-command: $*" >&2
  exit 1
}

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
  usage
  exit 0
fi

[ -n "$COMMAND" ] || die "missing command"

if [ -n "$REQUEST_ID" ]; then
  case "$REQUEST_ID" in
    ''|*[!0-9]*)
      die "--request-id must be a non-negative integer"
      ;;
  esac
fi

case "$COMMAND" in
  *$'\n'*|*$'\r'*)
    die "command must be a single line"
    ;;
esac

if [ -z "$PORT" ]; then
  if [ -e /dev/cu.usbserial-B0044J1V ]; then
    PORT="/dev/cu.usbserial-B0044J1V"
  else
    PORT="$(ls /dev/cu.usbserial-* 2>/dev/null | head -n 1 || true)"
  fi
fi

[ -n "$PORT" ] || die "no USB serial port found"

if [ "${AETHER_SERIAL_COMMAND_DRY_RUN:-0}" = "1" ]; then
  echo "serial port: $PORT"
  echo "command: $COMMAND"
  if [ -n "$REQUEST_ID" ]; then
    echo "request id: $REQUEST_ID"
    echo "payload: req id=${REQUEST_ID} cmd=${COMMAND}\\n"
  else
    echo "payload: ${COMMAND}\\n"
  fi
  exit 0
fi

if [ -n "$REQUEST_ID" ]; then
  PAYLOAD="req id=${REQUEST_ID} cmd=${COMMAND}"
else
  PAYLOAD="$COMMAND"
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

if [ -n "$REQUEST_ID" ]; then
  echo "sent serial command '$COMMAND' request-id $REQUEST_ID to $PORT"
else
  echo "sent serial command '$COMMAND' to $PORT"
fi
