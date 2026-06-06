#!/usr/bin/env bash
#===----------------------------------------------------------------------===#
# One-command AetherKernel network iteration loop.
#
#   usage: ./net-iterate.sh [tftp-root]
#
# Builds and stages kernel8.img/config.txt, sends the serial reset command, and
# watches TFTP + serial logs for proof that the Pi fetched over TFTP,
# booted the staged image, brought up the Runtime V37 shell, and proves a small
# command set through ./serial-probe.sh.
#===----------------------------------------------------------------------===#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TFTP_ROOT="${1:-${AETHER_TFTP_ROOT:-$HOME/aether-tftp}}"
PREFIX="${AETHER_TFTP_PREFIX:-aether}"
PREFIX="${PREFIX#/}"
PREFIX="${PREFIX%/}"
SERIAL_PORT="${AETHER_SERIAL_PORT:-/dev/cu.usbserial-B0044J1V}"
SERIAL_LOG="${AETHER_SERIAL_LOG:-/tmp/aether-serial.log}"
DNSMASQ_LOG="${AETHER_DNSMASQ_LOG:-${AETHER_TFTP_LOG:-/tmp/aether-dnsmasq.log}}"
TIMEOUT_S="${AETHER_NETITERATE_TIMEOUT:-150}"
RETRIES="${AETHER_NETITERATE_RETRIES:-3}"

usage() {
  sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'
}

die() {
  echo "net-iterate: $*" >&2
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

probe_shell() {
  local command="$1"
  local expected="$2"

  echo "probe shell: $command"
  "$SCRIPT_DIR/serial-probe.sh" "$command" "$expected" "$SERIAL_PORT"
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

tftp_server_running() {
  local escaped_root
  escaped_root="$(printf '%s' "$TFTP_ROOT" | sed 's/[.[\*^$()+?{|]/\\&/g')"
  pgrep -f "dnsmasq.*${escaped_root}" >/dev/null && return 0
  pgrep -f "tftp-now.*serve.*${escaped_root}" >/dev/null && return 0
  pgrep -f "tftpd.*${escaped_root}" >/dev/null && return 0
  pgrep -f "aether_tftp.py.*${escaped_root}" >/dev/null && return 0
  return 1
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
  echo "watch TFTP log: $DNSMASQ_LOG"
  echo "expect TFTP prefix: $PREFIX/"
  echo "attempts: $RETRIES"
  echo "timeout per attempt: ${TIMEOUT_S}s"
  echo "shell probes: ./serial-probe.sh status protocol bootcert sched sched2 sched3 sched4 sched5 sched6 cores locks runqueues req-status req-sched req-cores req-locks req-runqueues req-sched2 req-sched3 req-sched4 req-sched5 req-sched6 canceltest taskcheck channeltest mmu poolcheck pools heapfrag poolstats bootcheck stress soak kobjects drivers drivercheck tasks2 mailboxes sendtest supervisor health capcheck events"
  exit 0
fi

[ -f "$SERIAL_LOG" ] || die "serial log missing: $SERIAL_LOG"
[ -f "$DNSMASQ_LOG" ] || die "TFTP log missing: $DNSMASQ_LOG"
[ -d "$TFTP_ROOT/$PREFIX" ] || die "TFTP prefix missing: $TFTP_ROOT/$PREFIX"
tftp_server_running || die "TFTP server does not appear to be serving $TFTP_ROOT"

"$SCRIPT_DIR/netflash.sh" "$TFTP_ROOT"

attempt=1
last_dns_delta=""
last_serial_delta=""

while [ "$attempt" -le "$RETRIES" ]; do
  serial_start="$(file_size "$SERIAL_LOG")"
  dns_start="$(file_size "$DNSMASQ_LOG")"
  sd_fallback_seen=0

  echo "netboot attempt ${attempt}/${RETRIES}: reset Pi, then wait up to ${TIMEOUT_S}s for TFTP fetch + fresh AetherKernel boot..."
  "$SCRIPT_DIR/serial-reset.sh" "$SERIAL_PORT"

  deadline=$((SECONDS + TIMEOUT_S))
  while [ "$SECONDS" -lt "$deadline" ]; do
    dns_delta="$(file_delta "$DNSMASQ_LOG" "$dns_start")"
    serial_delta="$(file_delta "$SERIAL_LOG" "$serial_start")"

    if printf '%s' "$serial_delta" | grep -q "async heartbeat: timer-backed sleep 1s"; then
      echo "netboot attempt ${attempt}/${RETRIES} booted stale SD fallback image detected"
      echo "--- serial delta from stale fallback ---"
      printf '%s\n' "$serial_delta" | tail -n 120
      exit 2
    fi

    if printf '%s' "$dns_delta" | grep -q "$PREFIX/.*kernel8.img" \
      && printf '%s' "$serial_delta" | grep -q "=== AetherKernel ===" \
      && printf '%s' "$serial_delta" | grep -q "rtv2 fast 0x0000000000000000" \
      && printf '%s' "$serial_delta" | grep -q "rtv2 slow 0x0000000000000000" \
      && printf '%s' "$serial_delta" | grep -q "rtv2 long 0x0000000000000000" \
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
      && printf '%s' "$serial_delta" | grep -q "runtime v15: capability-tagged kernel handles" \
      && printf '%s' "$serial_delta" | grep -q "runtime v16: kernel event log ring" \
      && printf '%s' "$serial_delta" | grep -q "runtime v17: deterministic boot certificate" \
      && printf '%s' "$serial_delta" | grep -q "runtime v18: cooperative cancellation tokens" \
      && printf '%s' "$serial_delta" | grep -q "runtime v19: structured aether task spawn" \
      && printf '%s' "$serial_delta" | grep -q "runtime v20: bounded async channels" \
      && printf '%s' "$serial_delta" | grep -q "runtime v21: mmu ownership boundary" \
      && printf '%s' "$serial_delta" | grep -q "runtime v22: guarded typed pools" \
      && printf '%s' "$serial_delta" | grep -q "runtime v23: allocator and pool pressure telemetry" \
      && printf '%s' "$serial_delta" | grep -q "runtime v24: fixed driver registry" \
      && printf '%s' "$serial_delta" | grep -q "runtime v25: scriptable command protocol v2" \
      && printf '%s' "$serial_delta" | grep -q "runtime v27: panic taxonomy and symbolic retained records" \
      && printf '%s' "$serial_delta" | grep -q "runtime v28: swift runtime dependency audit" \
      && printf '%s' "$serial_delta" | grep -q "runtime v29: agent-oriented control session" \
      && printf '%s' "$serial_delta" | grep -q "runtime v30: swift-native kernel substrate certificate" \
      && printf '%s' "$serial_delta" | grep -q "runtime v31: preemptive scheduler substrate" \
      && printf '%s' "$serial_delta" | grep -q "runtime v32: smp secondary-core bring-up" \
      && printf '%s' "$serial_delta" | grep -q "runtime v33: atomics spinlocks per-core run queues" \
      && printf '%s' "$serial_delta" | grep -q "runtime v34: timer-driven smp scheduler dispatch" \
      && printf '%s' "$serial_delta" | grep -q "runtime v35: secondary-owned scheduler workers" \
      && printf '%s' "$serial_delta" | grep -q "runtime v36: timer-fed secondary scheduler workers" \
      && printf '%s' "$serial_delta" | grep -q "runtime v37: timer-fed secondary C scheduler jobs" \
      && printf '%s' "$serial_delta" | grep -q "runtime v38: secondary scheduler wake protocol" \
      && printf '%s' "$serial_delta" | grep -q "handlecheck ok=1 .*handle_selftest=1 .*cap_selftest=1" \
      && printf '%s' "$serial_delta" | grep -q "rtv13 mail tx 0x0000000000000000" \
      && printf '%s' "$serial_delta" | grep -q "rtv13 mail rx 0x0000000000000000" \
      && printf '%s' "$serial_delta" | grep -q "shell ready commands=help,protocol,status,heap,queues,tasks,tasks2,kobjects,drivers,drivercheck,mailboxes,sendtest,supervisor,health,capcheck,events,runtime,agent,certificate,sched,sched2,sched3,sched4,sched5,sched6,cores,locks,runqueues,diag,irqs,timers,memcheck,faults,retained,retained-clear,memmap,mmu,pools,poolcheck,heapfrag,poolstats,frames,heapcheck,framecheck,stress,frameprobe,bootcert,canceltest,taskcheck,channeltest,bootcheck,soak,heap-invalid-free-test,heap-double-free-test,panic-test,fault-test,reboot"; then
      echo "netboot iteration verified on attempt ${attempt}/${RETRIES}"
      if [ "${AETHER_NETITERATE_SKIP_SHELL_PROBES:-0}" != "1" ]; then
        # probe shell: status
        probe_shell "status" "^status uptime_ms=.*timer_mask="
        # probe shell: protocol
        probe_shell "protocol" "^protocol version=2 .*begin_end=1 .*errors=1"
        # probe shell: bootcert
        probe_shell "bootcert" "^bootcert ok=1 version=38 .*wake=1 .*job_exec=1 .*worker_feed=1 .*secondary_workers=1 .*preemptive=1 .*smp_scheduler=1 .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*certificate=1 .*agent=1 .*runtime=1 .*events_lost=0"
        # probe shell: runtime
        probe_shell "runtime" "^runtime ok=1 version=28 .*source_hooks=10 .*linked_hooks=2 .*heap_shims=5 .*linked_heap_shims=3 .*required_symbols=5"
        # probe shell: agent
        probe_shell "agent" "^agent ok=1 version=29 health=green .*bootcert=1 .*runtime=1 .*protocol=2 .*events_lost=0"
        # probe shell: certificate
        probe_shell "certificate" "^certificate ok=1 version=38 substrate=1 .*bootcert=1 .*wake=1 .*job_exec=1 .*worker_feed=1 .*secondary_workers=1 .*preemptive=1 .*smp_scheduler=1 .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*agent=1 .*runtime=1 .*memory=1 .*objects=1 .*tasks=1 .*mailboxes=1 .*supervisor=1 .*handles=1 .*events=1 .*cancellations=1 .*channels=1 .*drivers=1 .*pressure=1 .*pools=1 .*mmu=1 .*events_lost=0"
        # probe shell: sched
        probe_shell "sched" "^sched ok=1 version=31 .*active=1 .*cores=1 .*core=0 .*ticks=[1-9][0-9]* .*irq_ticks=[1-9][0-9]* .*preemptions=[1-9][0-9]* .*runqueue=0/[1-9][0-9]* .*selftest=1"
        # probe shell: sched2
        probe_shell "sched2" "^sched2 ok=1 version=34 .*preemptive=1 .*smp_scheduler=1 .*active=1 .*cores=4 .*online=4 .*dispatches=[1-9][0-9]* .*routes=[1-9][0-9]* .*imbalance=[0-9][0-9]* .*core0=[1-9][0-9]* .*core1=[1-9][0-9]* .*core2=[1-9][0-9]* .*core3=[1-9][0-9]* .*selftest=1"
        # probe shell: sched3
        probe_shell "sched3" "^sched3 ok=1 version=35 .*secondary_workers=1 .*active=1 .*cores=4 .*online=4 .*worker_drains=[1-9][0-9]* .*worker_idles=[0-9][0-9]* .*imbalance=[0-9][0-9]* .*core0=[0-9][0-9]* .*core1=[1-9][0-9]* .*core2=[1-9][0-9]* .*core3=[1-9][0-9]* .*selftest=1"
        # probe shell: sched4
        probe_shell "sched4" "^sched4 ok=1 version=36 .*worker_feed=1 .*secondary_workers=1 .*feeds=[1-9][0-9]* .*drains=[1-9][0-9]* .*drops=[0-9][0-9]* .*gap=[0-9][0-9]* .*feed_imbalance=[0-9][0-9]* .*drain_imbalance=[0-9][0-9]* .*core0_feed=0 .*core1_feed=[1-9][0-9]* .*core2_feed=[1-9][0-9]* .*core3_feed=[1-9][0-9]* .*core0_drain=0 .*core1_drain=[1-9][0-9]* .*core2_drain=[1-9][0-9]* .*core3_drain=[1-9][0-9]* .*selftest=1"
        # probe shell: sched5
        probe_shell "sched5" "^sched5 ok=1 version=37 .*job_exec=1 .*worker_feed=1 .*secondary_workers=1 .*executions=[1-9][0-9]* .*completions=[1-9][0-9]* .*noops=[0-9][0-9]* .*checksum=[1-9][0-9]* .*gap=[0-9][0-9]* .*imbalance=[0-9][0-9]* .*core0_exec=0 .*core1_exec=[1-9][0-9]* .*core2_exec=[1-9][0-9]* .*core3_exec=[1-9][0-9]* .*core0_done=0 .*core1_done=[1-9][0-9]* .*core2_done=[1-9][0-9]* .*core3_done=[1-9][0-9]* .*selftest=1"
        # probe shell: sched6
        probe_shell "sched6" "^sched6 ok=1 version=38 .*wake=1 .*job_exec=1 .*worker_feed=1 .*signals=[1-9][0-9]* .*mask=0xe .*targets=[1-9][0-9]* .*waits=[1-9][0-9]* .*wakes=[1-9][0-9]* .*gap=[0-9][0-9]* .*imbalance=[0-9][0-9]* .*core0_wait=0 .*core1_wait=[1-9][0-9]* .*core2_wait=[1-9][0-9]* .*core3_wait=[1-9][0-9]* .*core0_wake=0 .*core1_wake=[1-9][0-9]* .*core2_wake=[1-9][0-9]* .*core3_wake=[1-9][0-9]* .*selftest=1"
        # probe shell: cores
        probe_shell "cores" "^cores ok=1 version=32 .*capacity=4 .*online=4 .*mask=0xf .*primary=0 .*release=0xe .*selftest=1 .*core0=1 .*core1=1 .*core2=1 .*core3=1"
        # probe shell: locks
        probe_shell "locks" "^locks ok=1 version=33 .*atomics=1 .*spinlocks=1 .*selftest=1"
        # probe shell: runqueues
        probe_shell "runqueues" "^runqueues ok=1 version=33 .*cores=4 .*capacity=[1-9][0-9]* .*total=0 .*core0=0 .*core1=0 .*core2=0 .*core3=0 .*selftest=1"
        # probe shell: req-agent
        probe_shell "req id=29 cmd=agent" "^resp id=29 ok=1 cmd=agent end"
        # probe shell: req-certificate
        probe_shell "req id=30 cmd=certificate" "^resp id=30 ok=1 cmd=certificate end"
        # probe shell: req-sched
        probe_shell "req id=31 cmd=sched" "^resp id=31 ok=1 cmd=sched end"
        # probe shell: req-cores
        probe_shell "req id=32 cmd=cores" "^resp id=32 ok=1 cmd=cores end"
        # probe shell: req-locks
        probe_shell "req id=33 cmd=locks" "^resp id=33 ok=1 cmd=locks end"
        # probe shell: req-runqueues
        probe_shell "req id=34 cmd=runqueues" "^resp id=34 ok=1 cmd=runqueues end"
        # probe shell: req-sched2
        probe_shell "req id=35 cmd=sched2" "^resp id=35 ok=1 cmd=sched2 end"
        # probe shell: req-sched3
        probe_shell "req id=36 cmd=sched3" "^resp id=36 ok=1 cmd=sched3 end"
        # probe shell: req-sched4
        probe_shell "req id=37 cmd=sched4" "^resp id=37 ok=1 cmd=sched4 end"
        # probe shell: req-sched5
        probe_shell "req id=38 cmd=sched5" "^resp id=38 ok=1 cmd=sched5 end"
        # probe shell: req-sched6
        probe_shell "req id=39 cmd=sched6" "^resp id=39 ok=1 cmd=sched6 end"
        # probe shell: req-status
        probe_shell "req id=25 cmd=status" "^resp id=25 ok=1 cmd=status end"
        # probe shell: canceltest
        probe_shell "canceltest" "^canceltest ok=1 .*completed=1"
        # probe shell: taskcheck
        probe_shell "taskcheck" "^taskcheck ok=1 .*spawns="
        # probe shell: channeltest
        probe_shell "channeltest" "^channeltest ok=1 .*received=1"
        # probe shell: mmu
        probe_shell "mmu" "^mmu ok=1 .*regions=4 .*block_size=0x40000000"
        # probe shell: poolcheck
        probe_shell "poolcheck" "^poolcheck ok=1 .*bad_frees=1 .*double_frees=1"
        # probe shell: pools
        probe_shell "pools" "^pools count=.* capacity=.* selftest=1"
        # probe shell: heapfrag
        probe_shell "heapfrag" "^heapfrag ok=1 .*fragmentation_permil=.*pressure_largest_free="
        # probe shell: poolstats
        probe_shell "poolstats" "^poolstats ok=1 .*total_slots=.*failed_allocs="
        # probe shell: bootcheck
        probe_shell "bootcheck" "^bootcheck ok=1 .*frame_free="
        # probe shell: stress
        probe_shell "stress" "^stress ok=1 .*heap_leak=0 frame_leak=0"
        # probe shell: soak
        probe_shell "soak" "^soak ok=1 .*failures=0 .*heap_leak=0 frame_leak=0"
        # probe shell: kobjects
        probe_shell "kobjects" "^kobjects count=.* active=.* handle_selftest=1 .*cap_selftest=1"
        # probe shell: drivers
        probe_shell "drivers" "^drivers count=4 capacity=4 selftest=1"
        # probe shell: drivercheck
        probe_shell "drivercheck" "^drivercheck ok=1 .*uart_irq=.*timer_irq=.*watchdog_resets="
        # probe shell: tasks2
        probe_shell "tasks2" "^tasks2 count=.* task index=.*fast"
        # probe shell: mailboxes
        probe_shell "mailboxes" "^mailboxes count=.* queue_capacity="
        # probe shell: sendtest
        probe_shell "sendtest" "^sendtest ok=1 .*received=1"
        # probe shell: supervisor
        probe_shell "supervisor" "^supervisor count=.* unhealthy=0"
        # probe shell: health
        probe_shell "health" "^health ok=1 .*supervised="
        # probe shell: capcheck
        probe_shell "capcheck" "^capcheck ok=1 .*denied=1 .*stale=1"
        # probe shell: events
        probe_shell "events" "^events count=.* lost=0 .*selftest=1"
      fi
      echo "--- TFTP delta ---"
      printf '%s\n' "$dns_delta" | tail -n 80
      echo "--- serial delta ---"
      printf '%s\n' "$serial_delta" | tail -n 120
      exit 0
    fi

    if printf '%s' "$serial_delta" | grep -q "runtime v4: irq-backed uart shell" \
      && printf '%s' "$serial_delta" | grep -q "shell ready commands=" \
      && ! printf '%s' "$serial_delta" | grep -q "runtime v38: secondary scheduler wake protocol"; then
      echo "netboot attempt ${attempt}/${RETRIES} stale pre-V38 SD fallback image detected"
      echo "TFTP kernel fetch was not verified; staged network image is not proven."
      print_tftp_diagnostics "$dns_delta"
      last_dns_delta="$dns_delta"
      last_serial_delta="$serial_delta"
      sd_fallback_seen=1
      break
    fi
    sleep 1
  done

  if [ "$sd_fallback_seen" != "1" ]; then
    last_dns_delta="$(file_delta "$DNSMASQ_LOG" "$dns_start")"
    last_serial_delta="$(file_delta "$SERIAL_LOG" "$serial_start")"
    echo "netboot attempt ${attempt}/${RETRIES} did not verify within ${TIMEOUT_S}s"
  fi

    if [ "$attempt" -lt "$RETRIES" ]; then
      if [ "$sd_fallback_seen" = "1" ]; then
        echo "retrying after stale pre-V38 SD fallback..."
    fi
    echo "--- TFTP delta from failed attempt ---"
    printf '%s\n' "$last_dns_delta" | tail -n 40
    print_tftp_diagnostics "$last_dns_delta"
    echo "--- serial delta from failed attempt ---"
    printf '%s\n' "$last_serial_delta" | tail -n 60
    echo "retrying..."
  fi

  attempt=$((attempt + 1))
done

echo "netboot iteration did not verify after ${RETRIES} attempt(s)"
print_tftp_diagnostics "$last_dns_delta"
if printf '%s' "$last_serial_delta" | grep -q "runtime v4: irq-backed uart shell" \
  && ! printf '%s' "$last_serial_delta" | grep -q "runtime v38: secondary scheduler wake protocol"; then
  echo "final result: stale pre-V38 SD fallback image booted, but staged network image is not proven."
  final_exit=3
else
  final_exit=1
fi
echo "--- TFTP delta from final attempt ---"
printf '%s\n' "$last_dns_delta" | tail -n 80
echo "--- serial delta from final attempt ---"
printf '%s\n' "$last_serial_delta" | tail -n 120
exit "$final_exit"
