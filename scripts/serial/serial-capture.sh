#!/usr/bin/env bash
#===----------------------------------------------------------------------===#
# Background UART capture for AetherKernel bench scripts.
#
#   usage: ./serial-capture.sh [serial-port]
#
# Restarts the logger after other tools briefly open the USB-TTL port.
#===----------------------------------------------------------------------===#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PORT="${1:-${AETHER_SERIAL_PORT:-/dev/cu.usbserial-B0044J1V}}"
LOG="${AETHER_SERIAL_LOG:-/tmp/aether-serial.log}"
CAPTURE_PY="${AETHER_SERIAL_CAPTURE_PY:-$SCRIPT_DIR/aether-serial-capture.py}"

if [ ! -f "$CAPTURE_PY" ]; then
  echo "serial-capture: missing capture script: $CAPTURE_PY" >&2
  exit 1
fi

if [ -e "$PORT" ]; then
  lsof -t "$PORT" 2>/dev/null | xargs kill 2>/dev/null || true
  sleep 0.2
fi

AETHER_SERIAL_PORT="$PORT" AETHER_SERIAL_LOG="$LOG" \
  nohup python3 -u "$CAPTURE_PY" >/tmp/aether-serial-capture.out 2>&1 &
sleep 0.3
echo "serial-capture: pid=$! port=$PORT log=$LOG"