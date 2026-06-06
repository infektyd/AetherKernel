#!/usr/bin/env bash
#===----------------------------------------------------------------------===#
# Guided AetherKernel netboot bring-up check.
#
#   usage: ./netboot-doctor.sh [tftp-root]
#
# Use this for the current bench wedge: the old SD image cannot reset itself,
# so the script stages the latest image, proves the Mac/TFTP side, asks for one
# physical reset, then watches for Pi TFTP + fresh serial boot evidence.
#===----------------------------------------------------------------------===#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TFTP_ROOT="${1:-${AETHER_TFTP_ROOT:-$HOME/aether-tftp}}"
PREFIX="${AETHER_TFTP_PREFIX:-aether}"
PREFIX="${PREFIX#/}"
PREFIX="${PREFIX%/}"
IFACE="${AETHER_NETBOOT_INTERFACE:-en0}"
SERVER_IP="${AETHER_NETBOOT_SERVER_IP:-10.42.0.1}"
SERIAL_LOG="${AETHER_SERIAL_LOG:-/tmp/aether-serial.log}"
DNSMASQ_LOG="${AETHER_DNSMASQ_LOG:-/tmp/aether-dnsmasq.log}"
TIMEOUT_S="${AETHER_NETBOOT_DOCTOR_TIMEOUT:-180}"

usage() {
  sed -n '2,11p' "$0" | sed 's/^# \{0,1\}//'
}

die() {
  echo "netboot-doctor: $*" >&2
  exit 1
}

print_tftp_diagnostics() {
  local dns_delta="$1"

  if printf '%s' "$dns_delta" | grep -Eq "failed sending .*/start4\\.elf|timeout sending .*/start4\\.elf"; then
    echo "diagnostic: Pi bootloader did not reliably receive start4.elf over TFTP."
    echo "diagnostic: kernel was not reached; try restarting serve-netboot with AETHER_TFTP_NO_BLOCKSIZE=1 for an A/B test."
  fi

  if printf '%s' "$dns_delta" | grep -Eq "failed sending .*/kernel8\\.img|timeout sending .*/kernel8\\.img"; then
    echo "diagnostic: kernel8.img transfer was attempted but not cleanly completed before fallback/retry."
  fi
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

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
  usage
  exit 0
fi

if [ "${AETHER_NETBOOT_DOCTOR_DRY_RUN:-0}" = "1" ]; then
  echo "check interface: $IFACE at $SERVER_IP"
  echo "check dnsmasq TFTP root: $TFTP_ROOT"
  echo "stage latest image: ./netflash.sh $TFTP_ROOT"
  echo "ACTION: reset or power-cycle the Pi once"
  echo "watch TFTP prefix: $PREFIX/"
  echo "watch serial log: $SERIAL_LOG"
  echo "watch dnsmasq log: $DNSMASQ_LOG"
  exit 0
fi

[ -n "$PREFIX" ] || die "AETHER_TFTP_PREFIX must not be empty"
[ -f "$SERIAL_LOG" ] || die "serial log missing: $SERIAL_LOG"
[ -f "$DNSMASQ_LOG" ] || die "dnsmasq log missing: $DNSMASQ_LOG"

if ! ifconfig "$IFACE" | grep -q "status: active"; then
  die "$IFACE is not active"
fi
if ! ifconfig "$IFACE" | grep -q "inet $SERVER_IP "; then
  die "$IFACE does not have $SERVER_IP"
fi

pgrep -f "dnsmasq.*$(printf '%s' "$TFTP_ROOT" | sed 's/[.[\*^$()+?{|]/\\&/g')" >/dev/null \
  || die "dnsmasq does not appear to be serving $TFTP_ROOT"

"$SCRIPT_DIR/netflash.sh" "$TFTP_ROOT"

[ -f "$TFTP_ROOT/$PREFIX/kernel8.img" ] || die "staged kernel missing"
[ -f "$TFTP_ROOT/$PREFIX/config.txt" ] || die "staged config missing"

tmp="$(mktemp -d)"
cleanup() {
  rm -rf "$tmp"
}
trap cleanup EXIT
(
  cd "$tmp"
  printf 'binary\nget %s/config.txt\nquit\n' "$PREFIX" | tftp "$SERVER_IP" >/tmp/aether-doctor-tftp.out 2>&1
)
echo "local TFTP check:"
cat /tmp/aether-doctor-tftp.out

serial_start="$(file_size "$SERIAL_LOG")"
dns_start="$(file_size "$DNSMASQ_LOG")"

echo
echo "ACTION: reset or power-cycle the Pi once now."
echo "I am watching for: dnsmasq sends $PREFIX/kernel8.img + serial prints fresh Runtime V14 shell markers."
echo "Timeout: ${TIMEOUT_S}s"

deadline=$((SECONDS + TIMEOUT_S))
while [ "$SECONDS" -lt "$deadline" ]; do
  dns_delta="$(file_delta "$DNSMASQ_LOG" "$dns_start")"
  serial_delta="$(file_delta "$SERIAL_LOG" "$serial_start")"

  if printf '%s' "$dns_delta" | grep -q "$PREFIX/.*kernel8.img" \
    && printf '%s' "$serial_delta" | grep -q "=== AetherKernel ===" \
    && printf '%s' "$serial_delta" | grep -q "runtime v4: irq-backed uart shell" \
    && printf '%s' "$serial_delta" | grep -q "runtime v5: diagnostics shell" \
    && printf '%s' "$serial_delta" | grep -q "runtime v6: retained panic/fault records" \
    && printf '%s' "$serial_delta" | grep -q "runtime v7: memory map + frame allocator" \
    && printf '%s' "$serial_delta" | grep -q "runtime v8: allocator guardrails" \
    && printf '%s' "$serial_delta" | grep -q "runtime v9: bounded memory pressure self-tests" \
    && printf '%s' "$serial_delta" | grep -q "runtime v10: explicit guard probes" \
    && printf '%s' "$serial_delta" | grep -q "runtime v11: boot and soak invariants" \
    && printf '%s' "$serial_delta" | grep -q "runtime v12: kernel object table + task registry" \
    && printf '%s' "$serial_delta" | grep -q "runtime v13: bounded mailbox message queues" \
    && printf '%s' "$serial_delta" | grep -q "runtime v14: deterministic task supervisor" \
    && printf '%s' "$serial_delta" | grep -q "shell ready commands=help,status,heap,queues,tasks,tasks2,kobjects,mailboxes,sendtest,supervisor,health,diag,irqs,timers,memcheck,faults,retained,retained-clear,memmap,frames,heapcheck,framecheck,stress,frameprobe,bootcheck,soak,heap-invalid-free-test,heap-double-free-test,panic-test,fault-test,reboot"; then
    echo "netboot bring-up verified"
    echo "--- dnsmasq delta ---"
    printf '%s\n' "$dns_delta" | tail -n 80
    echo "--- serial delta ---"
    printf '%s\n' "$serial_delta" | tail -n 120
    exit 0
  fi
  sleep 1
done

echo "netboot bring-up did not verify within ${TIMEOUT_S}s"
print_tftp_diagnostics "$(file_delta "$DNSMASQ_LOG" "$dns_start")"
echo "--- dnsmasq delta ---"
file_delta "$DNSMASQ_LOG" "$dns_start" | tail -n 80
echo "--- serial delta ---"
file_delta "$SERIAL_LOG" "$serial_start" | tail -n 120
exit 1
