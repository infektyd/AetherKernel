#!/usr/bin/env bash
#===----------------------------------------------------------------------===#
# One-command AetherKernel network iteration loop.
#
#   usage: ./net-iterate.sh [tftp-root]
#
# Builds and stages kernel8.img/config.txt, sends the serial reset command, and
# watches dnsmasq + serial logs for proof that the Pi fetched over TFTP and
# booted the staged image.
#===----------------------------------------------------------------------===#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TFTP_ROOT="${1:-${AETHER_TFTP_ROOT:-$HOME/aether-tftp}}"
PREFIX="${AETHER_TFTP_PREFIX:-aether}"
PREFIX="${PREFIX#/}"
PREFIX="${PREFIX%/}"
SERIAL_PORT="${AETHER_SERIAL_PORT:-/dev/cu.usbserial-B0044J1V}"
SERIAL_LOG="${AETHER_SERIAL_LOG:-/tmp/aether-serial.log}"
DNSMASQ_LOG="${AETHER_DNSMASQ_LOG:-/tmp/aether-dnsmasq.log}"
TIMEOUT_S="${AETHER_NETITERATE_TIMEOUT:-150}"
RETRIES="${AETHER_NETITERATE_RETRIES:-3}"

usage() {
  sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'
}

die() {
  echo "net-iterate: $*" >&2
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

[ -n "$PREFIX" ] || die "AETHER_TFTP_PREFIX must not be empty"
is_positive_int "$TIMEOUT_S" || die "AETHER_NETITERATE_TIMEOUT must be a positive integer"
is_positive_int "$RETRIES" || die "AETHER_NETITERATE_RETRIES must be a positive integer"

if [ "${AETHER_NETITERATE_DRY_RUN:-0}" = "1" ]; then
  echo "./netflash.sh $TFTP_ROOT"
  echo "./serial-reset.sh $SERIAL_PORT"
  echo "watch serial log: $SERIAL_LOG"
  echo "watch dnsmasq log: $DNSMASQ_LOG"
  echo "expect TFTP prefix: $PREFIX/"
  echo "attempts: $RETRIES"
  echo "timeout per attempt: ${TIMEOUT_S}s"
  exit 0
fi

[ -f "$SERIAL_LOG" ] || die "serial log missing: $SERIAL_LOG"
[ -f "$DNSMASQ_LOG" ] || die "dnsmasq log missing: $DNSMASQ_LOG"
[ -d "$TFTP_ROOT/$PREFIX" ] || die "TFTP prefix missing: $TFTP_ROOT/$PREFIX"
pgrep -f "dnsmasq.*$(printf '%s' "$TFTP_ROOT" | sed 's/[.[\\*^$()+?{|]/\\&/g')" >/dev/null \
  || die "dnsmasq does not appear to be serving $TFTP_ROOT"

"$SCRIPT_DIR/netflash.sh" "$TFTP_ROOT"

attempt=1
last_dns_delta=""
last_serial_delta=""

while [ "$attempt" -le "$RETRIES" ]; do
  serial_start="$(file_size "$SERIAL_LOG")"
  dns_start="$(file_size "$DNSMASQ_LOG")"

  echo "netboot attempt ${attempt}/${RETRIES}: reset Pi, then wait up to ${TIMEOUT_S}s for TFTP fetch + fresh AetherKernel boot..."
  "$SCRIPT_DIR/serial-reset.sh" "$SERIAL_PORT"

  deadline=$((SECONDS + TIMEOUT_S))
  while [ "$SECONDS" -lt "$deadline" ]; do
    dns_delta="$(file_delta "$DNSMASQ_LOG" "$dns_start")"
    serial_delta="$(file_delta "$SERIAL_LOG" "$serial_start")"

    if printf '%s' "$dns_delta" | grep -q "$PREFIX/.*kernel8.img" \
      && printf '%s' "$serial_delta" | grep -q "=== AetherKernel ===" \
      && printf '%s' "$serial_delta" | grep -q "rtv2 fast 0x0000000000000000" \
      && printf '%s' "$serial_delta" | grep -q "rtv2 slow 0x0000000000000000" \
      && printf '%s' "$serial_delta" | grep -q "rtv2 long 0x0000000000000000"; then
      echo "netboot iteration verified on attempt ${attempt}/${RETRIES}"
      echo "--- dnsmasq delta ---"
      printf '%s\n' "$dns_delta" | tail -n 80
      echo "--- serial delta ---"
      printf '%s\n' "$serial_delta" | tail -n 120
      exit 0
    fi
    sleep 1
  done

  last_dns_delta="$(file_delta "$DNSMASQ_LOG" "$dns_start")"
  last_serial_delta="$(file_delta "$SERIAL_LOG" "$serial_start")"
  echo "netboot attempt ${attempt}/${RETRIES} did not verify within ${TIMEOUT_S}s"

  if [ "$attempt" -lt "$RETRIES" ]; then
    echo "--- dnsmasq delta from failed attempt ---"
    printf '%s\n' "$last_dns_delta" | tail -n 40
    echo "--- serial delta from failed attempt ---"
    printf '%s\n' "$last_serial_delta" | tail -n 60
    echo "retrying..."
  fi

  attempt=$((attempt + 1))
done

echo "netboot iteration did not verify after ${RETRIES} attempt(s)"
echo "--- dnsmasq delta from final attempt ---"
printf '%s\n' "$last_dns_delta" | tail -n 80
echo "--- serial delta from final attempt ---"
printf '%s\n' "$last_serial_delta" | tail -n 120
exit 1
