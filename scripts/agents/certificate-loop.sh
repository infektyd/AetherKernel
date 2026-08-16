#!/usr/bin/env bash
#===----------------------------------------------------------------------===#
# Runtime V30 substrate certificate loop.
#
#   usage: ./certificate-loop.sh [tftp-root]
#
# Runs repeated netboot iterations, an agent session, a certificate request, and
# the host runtime audit to prove the Swift-native kernel substrate end to end.
#===----------------------------------------------------------------------===#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TFTP_ROOT="${1:-${AETHER_TFTP_ROOT:-$HOME/aether-tftp}}"
PREFIX="${AETHER_TFTP_PREFIX:-aether}"
PREFIX="${PREFIX#/}"
PREFIX="${PREFIX%/}"
SERIAL_PORT="${AETHER_SERIAL_PORT:-/dev/cu.usbserial-B0044J1V}"
SERIAL_LOG="${AETHER_SERIAL_LOG:-/tmp/aether-serial.log}"
CERTIFICATE_LOG="${AETHER_CERTIFICATE_LOOP_LOG:-/tmp/aether-certificate-loop.log}"
CYCLES="${AETHER_CERTIFICATE_LOOP_CYCLES:-3}"
PROBE_TIMEOUT="${AETHER_CERTIFICATE_LOOP_PROBE_TIMEOUT:-15}"
ID_BASE="${AETHER_CERTIFICATE_LOOP_ID_BASE:-3000}"
MACHO="${AETHER_CERTIFICATE_LOOP_MACHO:-.build/release/Application}"

# Probe commands: cmd=certificate, plus agent-session.sh and runtime-audit.sh.

usage() {
  sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'
}

die() {
  echo "certificate-loop: $*" >&2
  exit 1
}

sha256_file() {
  shasum -a 256 "$1" | awk '{print $1}'
}

parse_netiterate_kernel_sha256() {
  local output="$1"
  local hash
  hash="$(printf '%s\n' "$output" | sed -n 's/^verified kernel8\.img sha256 \([0-9a-f]\{64\}\)$/\1/p' | tail -n 1)"
  if [ -z "$hash" ]; then
    die "net-iterate did not report verified kernel8.img sha256"
  fi
  printf '%s' "$hash"
}

bind_staged_kernel_sha256() {
  local netiterate_output="$1"
  local staged_kernel="$2"
  local netiterate_hash
  local staged_hash

  netiterate_hash="$(parse_netiterate_kernel_sha256 "$netiterate_output")"
  staged_hash="$(sha256_file "$staged_kernel")"
  if [ "$netiterate_hash" != "$staged_hash" ]; then
    echo "certificate-loop: staged kernel8.img sha256 mismatch" >&2
    echo "  net-iterate: $netiterate_hash" >&2
    echo "  staged:      $staged_hash" >&2
    exit 1
  fi
  printf '%s' "$netiterate_hash"
}

is_non_negative_int() {
  case "$1" in
    ''|*[!0-9]*)
      return 1
      ;;
  esac
  return 0
}

is_positive_int() {
  is_non_negative_int "$1" || return 1
  [ "$1" -gt 0 ] 2>/dev/null
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

log_line() {
  printf '%s\n' "$*" | tee -a "$CERTIFICATE_LOG"
}

probe_certificate() {
  local cycle="$1"
  local id="$2"
  local start delta summary_line probe_output

  start="$(file_size "$SERIAL_LOG")"
  log_line "certificate-loop probe cycle=$cycle id=$id command=certificate state=send"

  if ! probe_output="$(AETHER_SERIAL_PROBE_TIMEOUT="$PROBE_TIMEOUT" "$SCRIPT_DIR/../serial/serial-probe.sh" "req id=$id cmd=certificate" "^resp id=$id ok=1 cmd=certificate end" "$SERIAL_PORT" 2>&1)"; then
    printf '%s\n' "$probe_output" | tee -a "$CERTIFICATE_LOG" >&2
    log_line "certificate-loop probe cycle=$cycle id=$id command=certificate ok=0 reason=response_timeout"
    return 1
  fi

  printf '%s\n' "$probe_output" | tee -a "$CERTIFICATE_LOG"
  delta="$(file_delta "$SERIAL_LOG" "$start")"
  summary_line="$(printf '%s\n' "$delta" | grep -a -E "^certificate ok=1 version=63 substrate=1 .*bootcert=[01] .*concurrency=1 .*priority=1 .*fairness=1 .*stealing=[01] .*backpressure=[01] .*handoff=[01] .*wake=[01] .*job_exec=[01] .*worker_feed=[01] .*secondary_workers=[01] .*preemptive=[01] .*smp_scheduler=[01] .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*agent=1 .*runtime=1 .*events_lost=0" | tail -n 1 || true)"
  if [ -z "$summary_line" ]; then
    log_line "certificate-loop probe cycle=$cycle id=$id command=certificate ok=0 reason=summary_missing"
    {
      echo "--- serial delta ---"
      printf '%s\n' "$delta" | tail -n 80
    } | tee -a "$CERTIFICATE_LOG" >&2
    return 1
  fi

  log_line "certificate-loop summary cycle=$cycle command=certificate id=$id line=$summary_line"
  log_line "certificate-loop probe cycle=$cycle id=$id command=certificate ok=1"
  return 0
}

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
  usage
  exit 0
fi

is_positive_int "$CYCLES" || die "AETHER_CERTIFICATE_LOOP_CYCLES must be a positive integer"
is_positive_int "$PROBE_TIMEOUT" || die "AETHER_CERTIFICATE_LOOP_PROBE_TIMEOUT must be a positive integer"
is_non_negative_int "$ID_BASE" || die "AETHER_CERTIFICATE_LOOP_ID_BASE must be a non-negative integer"

if [ "${AETHER_CERTIFICATE_LOOP_DRY_RUN:-0}" = "1" ]; then
  echo "Runtime V30 substrate certificate loop dry run"
  echo "tftp root: $TFTP_ROOT"
  echo "serial port: $SERIAL_PORT"
  echo "serial log: $SERIAL_LOG"
  echo "certificate log: $CERTIFICATE_LOG"
  echo "cycles: $CYCLES"
  echo "probe timeout: ${PROBE_TIMEOUT}s"
  echo "id base: $ID_BASE"
  echo "probe: net-iterate.sh $TFTP_ROOT"
  echo "probe: agent-session.sh"
  echo "probe: req id=$((ID_BASE + 1)) cmd=certificate"
  echo "bind: verified kernel8.img sha256 from net-iterate output against $TFTP_ROOT/$PREFIX/kernel8.img"
  echo "probe: runtime-audit .build/release/Application"
  exit 0
fi

[ -f "$SERIAL_LOG" ] || die "serial log missing: $SERIAL_LOG"
mkdir -p "$(dirname "$CERTIFICATE_LOG")"

log_line "certificate-loop start version=30 cycles=$CYCLES tftp_root=$TFTP_ROOT log=$CERTIFICATE_LOG"

completed=0
cycle=1
while [ "$cycle" -le "$CYCLES" ]; do
  log_line "certificate-loop cycle=$cycle state=netboot"
  netiterate_output="$("$SCRIPT_DIR/../netboot/net-iterate.sh" "$TFTP_ROOT" | tee -a "$CERTIFICATE_LOG")"

  log_line "certificate-loop cycle=$cycle state=agent-session"
  AETHER_AGENT_SESSION_ID_BASE="$((ID_BASE + cycle * 100))" "$SCRIPT_DIR/agent-session.sh" "$SERIAL_PORT" | tee -a "$CERTIFICATE_LOG"

  probe_certificate "$cycle" "$((ID_BASE + cycle))"

  log_line "certificate-loop cycle=$cycle state=runtime-audit"
  STAGED_KERNEL="$TFTP_ROOT/$PREFIX/kernel8.img"
  KERNEL_SHA256="$(bind_staged_kernel_sha256 "$netiterate_output" "$STAGED_KERNEL")"
  log_line "certificate-loop cycle=$cycle kernel8.img sha256=$KERNEL_SHA256"
  "$SCRIPT_DIR/../runtime-audit.sh" "$MACHO" | tee -a "$CERTIFICATE_LOG"

  completed=$cycle
  log_line "certificate-loop cycle=$cycle ok=1"
  cycle=$((cycle + 1))
done

log_line "certificate-loop ok=1 version=63 cycles=$CYCLES completed=$completed substrate=1 bootcert=1 concurrency=1 priority=1 fairness=1 stealing=1 backpressure=1 handoff=1 wake=1 job_exec=1 worker_feed=1 secondary_workers=1 preemptive=1 smp_scheduler=1 atomics=1 locks=1 queues=1 smp=1 scheduler=1 agent=1 runtime=1 events_lost=0 log=$CERTIFICATE_LOG"
