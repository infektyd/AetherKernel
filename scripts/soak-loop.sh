#!/usr/bin/env bash
#===----------------------------------------------------------------------===#
# Runtime V26 host soak harness.
#
#   usage: ./soak-loop.sh [tftp-root]
#
# Runs repeated netboot iterations, then records request-wrapped status,
# bootcert, stress, soak, and events summaries from the UART shell.
#===----------------------------------------------------------------------===#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TFTP_ROOT="${1:-${AETHER_TFTP_ROOT:-$HOME/aether-tftp}}"
PREFIX="${AETHER_TFTP_PREFIX:-aether}"
PREFIX="${PREFIX#/}"
PREFIX="${PREFIX%/}"
SERIAL_PORT="${AETHER_SERIAL_PORT:-/dev/cu.usbserial-B0044J1V}"
SERIAL_LOG="${AETHER_SERIAL_LOG:-/tmp/aether-serial.log}"
SOAK_LOG="${AETHER_SOAK_LOG:-/tmp/aether-soak.log}"
CYCLES="${AETHER_SOAK_CYCLES:-12}"
PROBE_TIMEOUT="${AETHER_SOAK_PROBE_TIMEOUT:-15}"
PROBE_SETTLE="${AETHER_SOAK_PROBE_SETTLE:-3}"
ID_BASE="${AETHER_SOAK_ID_BASE:-2600}"
NETITERATE_SKIP_SHELL_PROBES="${AETHER_SOAK_NETITERATE_SKIP_SHELL_PROBES:-0}"

# Request probe commands: cmd=status, cmd=bootcert, cmd=stress, cmd=soak, cmd=events.

usage() {
  sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'
}

die() {
  echo "soak-loop: $*" >&2
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
    echo "soak-loop: staged kernel8.img sha256 mismatch" >&2
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
  printf '%s\n' "$*" | tee -a "$SOAK_LOG"
}

probe_request() {
  local cycle="$1"
  local id="$2"
  local command="$3"
  local summary_regex="$4"
  local start delta summary_line probe_output

  start="$(file_size "$SERIAL_LOG")"
  log_line "soak probe cycle=$cycle id=$id command=$command state=send"

  if ! probe_output="$(AETHER_SERIAL_PROBE_TIMEOUT="$PROBE_TIMEOUT" "$SCRIPT_DIR/serial/serial-probe.sh" "req id=$id cmd=$command" "^resp id=$id ok=1 cmd=$command end" "$SERIAL_PORT" 2>&1)"; then
    printf '%s\n' "$probe_output" | tee -a "$SOAK_LOG" >&2
    log_line "soak probe cycle=$cycle id=$id command=$command ok=0 reason=response_timeout"
    return 1
  fi

  printf '%s\n' "$probe_output" | tee -a "$SOAK_LOG"
  delta="$(file_delta "$SERIAL_LOG" "$start")"
  summary_line="$(printf '%s\n' "$delta" | grep -a -E "$summary_regex" | tail -n 1 || true)"
  if [ -z "$summary_line" ]; then
    log_line "soak probe cycle=$cycle id=$id command=$command ok=0 reason=summary_missing"
    {
      echo "--- serial delta ---"
      printf '%s\n' "$delta" | tail -n 80
    } | tee -a "$SOAK_LOG" >&2
    return 1
  fi

  log_line "soak summary cycle=$cycle command=$command id=$id line=$summary_line"
  log_line "soak probe cycle=$cycle id=$id command=$command ok=1"
  return 0
}

run_cycle_probes() {
  local cycle="$1"
  local base id

  base=$((ID_BASE + ((cycle - 1) * 10)))
  id=$((base + 1))
  probe_request "$cycle" "$id" "status" "^status uptime_ms=.*timer_mask="
  id=$((base + 2))
  probe_request "$cycle" "$id" "sched12" "^sched12 ok=1 version=44 .*concurrency=1 .*rounds=3 .*completions=3 .*failures=0 .*dispatches=[1-9][0-9]* .*total=0 .*capacity=8 .*soak_core1=[1-9][0-9]* .*soak_core2=[1-9][0-9]* .*soak_core3=[1-9][0-9]* .*selftest=1"
  id=$((base + 3))
  probe_request "$cycle" "$id" "bootcert" "^bootcert ok=1 version=66 .*concurrency=1 .*priority=1 .*fairness=1 .*stealing=[01] .*backpressure=[01] .*handoff=[01] .*wake=[01] .*job_exec=[01] .*worker_feed=[01] .*secondary_workers=[01] .*preemptive=[01] .*smp_scheduler=[01] .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*certificate=1 .*agent=1 .*runtime=1 .*syscall=1 .*uaccess=1 .*usermode=1 .*process=1 .*loader=1 .*multiprocess=1 .*sdhci=1 .*card=1 .*block=1 .*fat32=1 .*mailbox=1 .*framebuf=1 .*console=1 .*pcie=1 .*vl805=1 .*xhci=1 .*kbd=[01] .*events_lost=0"
  id=$((base + 4))
  probe_request "$cycle" "$id" "stress" "^stress ok=1 .*heap_leak=0 frame_leak=0"
  id=$((base + 5))
  probe_request "$cycle" "$id" "soak" "^soak ok=1 .*failures=0 .*heap_leak=0 frame_leak=0"
  id=$((base + 6))
  probe_request "$cycle" "$id" "events" "^events count=.* lost=0 .*selftest=1"
}

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
  usage
  exit 0
fi

is_positive_int "$CYCLES" || die "AETHER_SOAK_CYCLES must be a positive integer"
is_positive_int "$PROBE_TIMEOUT" || die "AETHER_SOAK_PROBE_TIMEOUT must be a positive integer"
is_non_negative_int "$PROBE_SETTLE" || die "AETHER_SOAK_PROBE_SETTLE must be a non-negative integer"
is_non_negative_int "$ID_BASE" || die "AETHER_SOAK_ID_BASE must be a non-negative integer"

if [ "${AETHER_SOAK_DRY_RUN:-0}" = "1" ]; then
  echo "Runtime V26 host soak harness dry run"
  echo "cycles: $CYCLES"
  echo "tftp root: $TFTP_ROOT"
  echo "soak log: $SOAK_LOG"
  echo "serial port: $SERIAL_PORT"
  echo "serial log: $SERIAL_LOG"
  echo "probe timeout: ${PROBE_TIMEOUT}s"
  echo "probe settle: ${PROBE_SETTLE}s"
  echo "net-iterate shell probes: ${NETITERATE_SKIP_SHELL_PROBES:+skip=$NETITERATE_SKIP_SHELL_PROBES}"
  echo "./net-iterate.sh $TFTP_ROOT"
  echo "probe: req id=$((ID_BASE + 1)) cmd=status"
  echo "probe: req id=$((ID_BASE + 2)) cmd=sched12"
  echo "probe: req id=$((ID_BASE + 3)) cmd=bootcert"
  echo "probe: req id=$((ID_BASE + 4)) cmd=stress"
  echo "probe: req id=$((ID_BASE + 5)) cmd=soak"
  echo "probe: req id=$((ID_BASE + 6)) cmd=events"
  exit 0
fi

[ -f "$SERIAL_LOG" ] || die "serial log missing: $SERIAL_LOG"
mkdir -p "$(dirname "$SOAK_LOG")"

completed=0
LAST_KERNEL_SHA256=""
finalized=0
on_exit() {
  local status="$?"
  if [ "$finalized" != "1" ]; then
    log_line "soak result ok=0 cycles=$CYCLES completed=$completed exit=$status log=$SOAK_LOG"
  fi
}
trap on_exit EXIT

log_line "soak start cycles=$CYCLES tftp_root=$TFTP_ROOT serial_log=$SERIAL_LOG log=$SOAK_LOG"

cycle=1
while [ "$cycle" -le "$CYCLES" ]; do
  started_at="$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
  start_seconds="$SECONDS"
  log_line "soak cycle=$cycle state=begin timestamp=$started_at"

  netiterate_output="$(AETHER_NETITERATE_SKIP_SHELL_PROBES="$NETITERATE_SKIP_SHELL_PROBES" \
    "$SCRIPT_DIR/netboot/net-iterate.sh" "$TFTP_ROOT" 2>&1 | tee -a "$SOAK_LOG")"

  STAGED_KERNEL="$TFTP_ROOT/$PREFIX/kernel8.img"
  KERNEL_SHA256="$(bind_staged_kernel_sha256 "$netiterate_output" "$STAGED_KERNEL")"
  LAST_KERNEL_SHA256="$KERNEL_SHA256"
  log_line "soak cycle=$cycle kernel8.img sha256=$KERNEL_SHA256"

  log_line "soak cycle=$cycle state=settle seconds=$PROBE_SETTLE"
  sleep "$PROBE_SETTLE"
  run_cycle_probes "$cycle"

  duration=$((SECONDS - start_seconds))
  completed="$cycle"
  log_line "soak cycle=$cycle ok=1 duration_s=$duration kernel8.img sha256=$KERNEL_SHA256"
  cycle=$((cycle + 1))
done

finalized=1
log_line "soak result ok=1 cycles=$CYCLES completed=$completed kernel8.img sha256=$LAST_KERNEL_SHA256 log=$SOAK_LOG"
