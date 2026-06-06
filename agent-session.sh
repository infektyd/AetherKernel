#!/usr/bin/env bash
#===----------------------------------------------------------------------===#
# Runtime V29 agent control session.
#
#   usage: ./agent-session.sh [serial-port]
#
# Sends request-wrapped UART shell commands and reduces the responses to one
# machine-checkable health line for an agent or host script.
#===----------------------------------------------------------------------===#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERIAL_PORT="${1:-${AETHER_SERIAL_PORT:-/dev/cu.usbserial-B0044J1V}}"
SERIAL_LOG="${AETHER_SERIAL_LOG:-/tmp/aether-serial.log}"
SESSION_LOG="${AETHER_AGENT_SESSION_LOG:-/tmp/aether-agent-session.log}"
PROBE_TIMEOUT="${AETHER_AGENT_SESSION_PROBE_TIMEOUT:-15}"
ID_BASE="${AETHER_AGENT_SESSION_ID_BASE:-2900}"

# Probe commands: cmd=agent, cmd=bootcert, cmd=runtime, cmd=stress, cmd=soak, cmd=events.

usage() {
  sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'
}

die() {
  echo "agent-session: $*" >&2
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
  printf '%s\n' "$*" | tee -a "$SESSION_LOG"
}

probe_request() {
  local id="$1"
  local command="$2"
  local summary_regex="$3"
  local start delta summary_line probe_output

  start="$(file_size "$SERIAL_LOG")"
  log_line "agent-session probe id=$id command=$command state=send"

  if ! probe_output="$(AETHER_SERIAL_PROBE_TIMEOUT="$PROBE_TIMEOUT" "$SCRIPT_DIR/serial-probe.sh" "req id=$id cmd=$command" "^resp id=$id ok=1 cmd=$command end" "$SERIAL_PORT" 2>&1)"; then
    printf '%s\n' "$probe_output" | tee -a "$SESSION_LOG" >&2
    log_line "agent-session probe id=$id command=$command ok=0 reason=response_timeout"
    return 1
  fi

  printf '%s\n' "$probe_output" | tee -a "$SESSION_LOG"
  delta="$(file_delta "$SERIAL_LOG" "$start")"
  summary_line="$(printf '%s\n' "$delta" | grep -a -E "$summary_regex" | tail -n 1 || true)"
  if [ -z "$summary_line" ]; then
    log_line "agent-session probe id=$id command=$command ok=0 reason=summary_missing"
    {
      echo "--- serial delta ---"
      printf '%s\n' "$delta" | tail -n 80
    } | tee -a "$SESSION_LOG" >&2
    return 1
  fi

  log_line "agent-session summary command=$command id=$id line=$summary_line"
  log_line "agent-session probe id=$id command=$command ok=1"
  return 0
}

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
  usage
  exit 0
fi

is_positive_int "$PROBE_TIMEOUT" || die "AETHER_AGENT_SESSION_PROBE_TIMEOUT must be a positive integer"
is_non_negative_int "$ID_BASE" || die "AETHER_AGENT_SESSION_ID_BASE must be a non-negative integer"

if [ "${AETHER_AGENT_SESSION_DRY_RUN:-0}" = "1" ]; then
  echo "Runtime V29 agent control session dry run"
  echo "serial port: $SERIAL_PORT"
  echo "serial log: $SERIAL_LOG"
  echo "session log: $SESSION_LOG"
  echo "probe timeout: ${PROBE_TIMEOUT}s"
  echo "id base: $ID_BASE"
  echo "probe: req id=$((ID_BASE + 1)) cmd=agent"
  echo "probe: req id=$((ID_BASE + 2)) cmd=bootcert"
  echo "probe: req id=$((ID_BASE + 3)) cmd=runtime"
  echo "probe: req id=$((ID_BASE + 4)) cmd=stress"
  echo "probe: req id=$((ID_BASE + 5)) cmd=soak"
  echo "probe: req id=$((ID_BASE + 6)) cmd=events"
  exit 0
fi

[ -f "$SERIAL_LOG" ] || die "serial log missing: $SERIAL_LOG"
mkdir -p "$(dirname "$SESSION_LOG")"

log_line "agent-session start version=29 serial_log=$SERIAL_LOG log=$SESSION_LOG"

probe_request "$((ID_BASE + 1))" "agent" "^agent ok=1 version=29 health=green .*bootcert=1 .*runtime=1 .*protocol=2 .*events_lost=0"
probe_request "$((ID_BASE + 2))" "bootcert" "^bootcert ok=1 version=37 .*job_exec=1 .*worker_feed=1 .*secondary_workers=1 .*preemptive=1 .*smp_scheduler=1 .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*certificate=1 .*agent=1 .*runtime=1 .*events_lost=0"
probe_request "$((ID_BASE + 3))" "runtime" "^runtime ok=1 version=28 .*audit=1"
probe_request "$((ID_BASE + 4))" "stress" "^stress ok=1 .*heap_leak=0 frame_leak=0"
probe_request "$((ID_BASE + 5))" "soak" "^soak ok=1 .*failures=0 .*heap_leak=0 frame_leak=0"
probe_request "$((ID_BASE + 6))" "events" "^events count=.* lost=0 .*selftest=1"

log_line "agent-session ok=1 version=29 health=green bootcert=1 runtime=1 stress=1 soak=1 events_lost=0 log=$SESSION_LOG"
