#!/usr/bin/env bash
#===----------------------------------------------------------------------===#
# Cold power-cycle the Pi so the bootloader re-enters NETBOOT/TFTP mode.
#
#   usage: ./power-cycle.sh [off|on|cycle]      (default: cycle)
#
# WHY THIS EXISTS:
#   The Raspberry Pi 4 bootloader only re-enters netboot/TFTP mode on a COLD
#   start. A warm reboot or the BCM2711 PM watchdog reset is NOT enough — power
#   must physically disconnect and reconnect. The Pi 4 USB-C port is power-input
#   only (no software power gate, no onboard button), so a cold cycle REQUIRES an
#   external switch on the power line. This script abstracts that switch behind a
#   backend so unattended runs (net-iterate.sh) self-recover with no human.
#
# BACKENDS (set AETHER_POWER_BACKEND):
#   wemo     Belkin Wemo plug, local UPnP/SOAP (no cloud). needs AETHER_POWER_HOST=<ip>
#            Port drifts (49153/49152/49154); set AETHER_POWER_WEMO_PORT if needed.
#   shelly   Shelly Plus/Gen2 plug, local HTTP RPC.   needs AETHER_POWER_HOST=<ip>
#   shelly1  Shelly Gen1 plug, local HTTP.            needs AETHER_POWER_HOST=<ip>
#   kasa     TP-Link Kasa via python-kasa CLI.        needs AETHER_POWER_HOST=<ip>
#   tasmota  Tasmota-flashed plug, local HTTP.        needs AETHER_POWER_HOST=<ip>
#   cmd      Generic: run your own shell commands.    needs AETHER_POWER_OFF_CMD
#                                                       and AETHER_POWER_ON_CMD
#   manual   No hardware yet: prompt a human to pull/restore power, then auto-
#            detect when the board is back. Lets the loop work TODAY and migrate
#            to a real plug later by only changing AETHER_POWER_BACKEND.
#
# ENV:
#   AETHER_POWER_BACKEND      one of the above (default: manual)
#   AETHER_POWER_HOST         plug IP/host (shelly/shelly1/kasa/tasmota)
#   AETHER_POWER_OFF_CMD      off command (cmd backend)
#   AETHER_POWER_ON_CMD       on command (cmd backend)
#   AETHER_POWER_DISCHARGE_S  seconds power stays off (default 4) for cap discharge
#   AETHER_POWER_TARGET_IP    Pi IP to ping for "back up" (default 10.42.0.2)
#   AETHER_SERIAL_LOG         serial capture log to watch (default /tmp/aether-serial.log)
#   AETHER_POWER_BACK_TIMEOUT seconds to wait for board to return (default 45;
#                             manual backend uses max(this, 600))
#   AETHER_POWER_DRY_RUN=1     print actions, change nothing
#===----------------------------------------------------------------------===#
set -euo pipefail

ACTION="${1:-cycle}"
BACKEND="${AETHER_POWER_BACKEND:-manual}"
HOST="${AETHER_POWER_HOST:-}"
DISCHARGE_S="${AETHER_POWER_DISCHARGE_S:-4}"
TARGET_IP="${AETHER_POWER_TARGET_IP:-10.42.0.2}"
SERIAL_LOG="${AETHER_SERIAL_LOG:-/tmp/aether-serial.log}"
BACK_TIMEOUT="${AETHER_POWER_BACK_TIMEOUT:-45}"
DRY_RUN="${AETHER_POWER_DRY_RUN:-0}"

die() { echo "power-cycle: $*" >&2; exit 1; }
log() { echo "power-cycle: $*"; }

if [ "$ACTION" = "-h" ] || [ "$ACTION" = "--help" ]; then
  sed -n '2,40p' "$0" | sed 's/^# \{0,1\}//'
  exit 0
fi

run() {  # echo + execute (or just echo under dry-run)
  if [ "$DRY_RUN" = "1" ]; then echo "  [dry-run] $*"; return 0; fi
  eval "$@"
}

file_size() { [ -f "$1" ] && (stat -f '%z' "$1" 2>/dev/null || wc -c <"$1" 2>/dev/null || echo 0) || echo 0; }

need_host() { [ -n "$HOST" ] || die "backend '$BACKEND' needs AETHER_POWER_HOST"; }

# Belkin Wemo local control via UPnP SOAP. $1 = BinaryState (1=on, 0=off).
WEMO_PORT="${AETHER_POWER_WEMO_PORT:-49153}"
wemo_set() {
  need_host
  local state="$1"
  local body="<?xml version=\"1.0\" encoding=\"utf-8\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:SetBinaryState xmlns:u=\"urn:Belkin:service:basicevent:1\"><BinaryState>${state}</BinaryState></u:SetBinaryState></s:Body></s:Envelope>"
  run "curl -fsS --max-time 8 \"http://$HOST:$WEMO_PORT/upnp/control/basicevent1\" -H 'Content-Type: text/xml; charset=\"utf-8\"' -H 'SOAPACTION: \"urn:Belkin:service:basicevent:1#SetBinaryState\"' -d '$body' >/dev/null"
}

power_off() {
  case "$BACKEND" in
    wemo)    wemo_set 0 ;;
    shelly)  need_host; run "curl -fsS --max-time 8 \"http://$HOST/rpc/Switch.Set?id=0&on=false\" >/dev/null" ;;
    shelly1) need_host; run "curl -fsS --max-time 8 \"http://$HOST/relay/0?turn=off\" >/dev/null" ;;
    kasa)    need_host; run "kasa --host \"$HOST\" off >/dev/null" ;;
    tasmota) need_host; run "curl -fsS --max-time 8 \"http://$HOST/cm?cmnd=Power%20off\" >/dev/null" ;;
    cmd)     [ -n "${AETHER_POWER_OFF_CMD:-}" ] || die "cmd backend needs AETHER_POWER_OFF_CMD"; run "${AETHER_POWER_OFF_CMD}" ;;
    manual)  echo; echo ">>> PLEASE DISCONNECT POWER TO THE PI NOW (pull the plug / flip the switch). <<<" ;;
    *)       die "unknown AETHER_POWER_BACKEND='$BACKEND' (wemo|shelly|shelly1|kasa|tasmota|cmd|manual)" ;;
  esac
}

power_on() {
  case "$BACKEND" in
    wemo)    wemo_set 1 ;;
    shelly)  need_host; run "curl -fsS --max-time 8 \"http://$HOST/rpc/Switch.Set?id=0&on=true\" >/dev/null" ;;
    shelly1) need_host; run "curl -fsS --max-time 8 \"http://$HOST/relay/0?turn=on\" >/dev/null" ;;
    kasa)    need_host; run "kasa --host \"$HOST\" on >/dev/null" ;;
    tasmota) need_host; run "curl -fsS --max-time 8 \"http://$HOST/cm?cmnd=Power%20on\" >/dev/null" ;;
    cmd)     [ -n "${AETHER_POWER_ON_CMD:-}" ] || die "cmd backend needs AETHER_POWER_ON_CMD"; run "${AETHER_POWER_ON_CMD}" ;;
    manual)  echo ">>> NOW RECONNECT POWER TO THE PI. <<<"; echo ; echo "(waiting for the board to come back...)" ;;
    *)       die "unknown AETHER_POWER_BACKEND='$BACKEND'" ;;
  esac
}

# Wait until the board is demonstrably back: serial log grows (cold boot spews
# firmware/bootloader text immediately) OR the Pi answers ping. Either is proof
# power was restored. net-iterate.sh still does the full TFTP+kernel boot proof.
wait_for_back() {
  local timeout="$1" base_size deadline
  [ "$BACKEND" = "manual" ] && [ "$timeout" -lt 600 ] && timeout=600
  base_size="$(file_size "$SERIAL_LOG")"
  deadline=$((SECONDS + timeout))
  log "waiting up to ${timeout}s for board to return (serial-growth on $SERIAL_LOG or ping $TARGET_IP)..."
  while [ "$SECONDS" -lt "$deadline" ]; do
    if [ "$(file_size "$SERIAL_LOG")" -gt "$base_size" ]; then
      log "board is back (serial output resumed)"; return 0
    fi
    if ping -c 1 -t 1 "$TARGET_IP" >/dev/null 2>&1; then
      log "board is back (ping $TARGET_IP ok)"; return 0
    fi
    sleep 1
  done
  return 1
}

do_off() { log "backend=$BACKEND -> POWER OFF"; power_off; }
do_on()  { log "backend=$BACKEND -> POWER ON";  power_on; }

case "$ACTION" in
  off)   do_off ;;
  on)    do_on ;;
  cycle)
    do_off
    log "discharge ${DISCHARGE_S}s..."
    [ "$DRY_RUN" = "1" ] || sleep "$DISCHARGE_S"
    do_on
    if [ "$DRY_RUN" = "1" ]; then log "[dry-run] would wait for board to return"; exit 0; fi
    if wait_for_back "$BACK_TIMEOUT"; then
      log "cold power-cycle complete and verified"
    else
      die "board did not come back within timeout (backend=$BACKEND, host=${HOST:-n/a})"
    fi
    ;;
  *) die "unknown action '$ACTION' (off|on|cycle)" ;;
esac
