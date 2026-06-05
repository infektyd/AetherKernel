#!/usr/bin/env bash
#===----------------------------------------------------------------------===#
# Send one AetherKernel shell command and wait for a matching serial response.
#
#   usage: ./serial-probe.sh <command> <expected-regex> [serial-port]
#
# Wraps ./serial-command.sh and watches $AETHER_SERIAL_LOG from a fresh offset.
#===----------------------------------------------------------------------===#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMMAND="${1:-}"
EXPECTED_REGEX="${2:-}"
PORT="${3:-${AETHER_SERIAL_PORT:-}}"
SERIAL_LOG="${AETHER_SERIAL_LOG:-/tmp/aether-serial.log}"
TIMEOUT_S="${AETHER_SERIAL_PROBE_TIMEOUT:-10}"

usage() {
  sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'
}

die() {
  echo "serial-probe: $*" >&2
  exit 1
}

file_size() {
  if [ -f "$1" ]; then
    stat -f %z "$1" 2>/dev/null || stat -c %s "$1"
  else
    echo 0
  fi
}

file_delta() {
  local file="$1"
  local start="$2"
  if [ ! -f "$file" ]; then
    return 0
  fi
  tail -c +$((start + 1)) "$file" 2>/dev/null || true
}

is_positive_int() {
  case "$1" in
    ''|*[!0-9]*)
      return 1
      ;;
  esac
  [ "$1" -gt 0 ] 2>/dev/null
}

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
  usage
  exit 0
fi

[ -n "$COMMAND" ] || die "missing command"
[ -n "$EXPECTED_REGEX" ] || die "missing expected regex"
is_positive_int "$TIMEOUT_S" || die "AETHER_SERIAL_PROBE_TIMEOUT must be a positive integer"

if [ -z "$PORT" ]; then
  if [ -e /dev/cu.usbserial-B0044J1V ]; then
    PORT="/dev/cu.usbserial-B0044J1V"
  else
    PORT="$(ls /dev/cu.usbserial-* 2>/dev/null | head -n 1 || true)"
  fi
fi

[ -n "$PORT" ] || die "no USB serial port found"

if [ "${AETHER_SERIAL_PROBE_DRY_RUN:-0}" = "1" ]; then
  echo "serial port: $PORT"
  echo "command: $COMMAND"
  echo "expected regex: $EXPECTED_REGEX"
  echo "serial log: $SERIAL_LOG"
  echo "timeout: ${TIMEOUT_S}s"
  exit 0
fi

[ -f "$SERIAL_LOG" ] || die "serial log missing: $SERIAL_LOG"

start="$(file_size "$SERIAL_LOG")"
"$SCRIPT_DIR/serial-command.sh" "$COMMAND" "$PORT" >/dev/null

deadline=$((SECONDS + TIMEOUT_S))
delta=""
while [ "$SECONDS" -lt "$deadline" ]; do
  delta="$(file_delta "$SERIAL_LOG" "$start")"
  match="$(printf '%s\n' "$delta" | grep -a -E "$EXPECTED_REGEX" | tail -n 1 || true)"
  if [ -n "$match" ]; then
    echo "matched: $match"
    exit 0
  fi
  sleep 1
done

echo "serial-probe: timeout waiting for command '$COMMAND'" >&2
echo "expected regex: $EXPECTED_REGEX" >&2
echo "--- serial delta ---" >&2
printf '%s\n' "$delta" | tail -n 80 >&2
exit 1
