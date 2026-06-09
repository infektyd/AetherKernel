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

  if ! probe_output="$(AETHER_SERIAL_PROBE_TIMEOUT="$PROBE_TIMEOUT" "$SCRIPT_DIR/serial-probe.sh" "req id=$id cmd=$command" "^resp id=$id ok=1 cmd=$command end" "$SERIAL_PORT" 2>&1)"; then
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
  probe_request "$cycle" "$id" "sched12" "^sched12 ok=1 version=44 .*concurrency=1 .*rounds=3 .*completions=3 .*failures=0"
  id=$((base + 3))
  probe_request "$cycle" "$id" "bootcert" "^bootcert ok=1 version=44 .*concurrency=1 .*priority=1 .*fairness=1 .*stealing=1 .*backpressure=1 .*handoff=1 .*wake=1 .*job_exec=1 .*worker_feed=1 .*secondary_workers=1 .*preemptive=1 .*smp_scheduler=1 .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*certificate=1 .*agent=1 .*events_lost=0"
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

  AETHER_NETITERATE_SKIP_SHELL_PROBES="$NETITERATE_SKIP_SHELL_PROBES" \
    "$SCRIPT_DIR/net-iterate.sh" "$TFTP_ROOT" 2>&1 | tee -a "$SOAK_LOG"

  log_line "soak cycle=$cycle state=settle seconds=$PROBE_SETTLE"
  sleep "$PROBE_SETTLE"
  run_cycle_probes "$cycle"

  duration=$((SECONDS - start_seconds))
  completed="$cycle"
  log_line "soak cycle=$cycle ok=1 duration_s=$duration"
  cycle=$((cycle + 1))
done

finalized=1
log_line "soak result ok=1 cycles=$CYCLES completed=$completed log=$SOAK_LOG"
