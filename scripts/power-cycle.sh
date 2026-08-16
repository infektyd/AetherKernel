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
#   shelly   Shelly Gen2+/Gen4 (Plug US Gen4 S4PL-00116US), local HTTP RPC
#            Switch.Set / Switch.GetStatus.           needs AETHER_POWER_HOST=<ip>
#            Optional digest: AETHER_POWER_SHELLY_AUTH=user:pass
#   shelly1  Shelly Gen1 plug, local HTTP /relay/0.   needs AETHER_POWER_HOST=<ip>
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
#   AETHER_POWER_SHELLY_AUTH  digest user:pass if Shelly Gen2+/Gen4 has auth
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
  sed -n '2,42p' "$0" | sed 's/^# \{0,1\}//'
  exit 0
fi

run() {  # echo + execute (or just echo under dry-run)
  if [ "$DRY_RUN" = "1" ]; then echo "  [dry-run] $*"; return 0; fi
  eval "$@"
}

file_size() { [ -f "$1" ] && (stat -f '%z' "$1" 2>/dev/null || wc -c <"$1" 2>/dev/null || echo 0) || echo 0; }

need_host() { [ -n "$HOST" ] || die "backend '$BACKEND' needs AETHER_POWER_HOST"; }

# Belkin Wemo local control via UPnP SOAP (no cloud). $1 = BinaryState (1=on, 0=off).
#
# Hardened (2026-06-08): the WSP080's UPnP/Wi-Fi control stack becomes
# unresponsive after a while in service — observed reliably; exact trigger
# (elapsed time vs toggle count) is UNCONFIRMED, so we assume nothing. There is
# no software reset for a hung WSP080: it must be physically unplugged/replugged.
# What this code does instead of silently stalling:
#   - VERIFY every set with GetBinaryState, and RETRY a few times (rides out
#     transient UPnP blips).
#   - On hard failure, raise a LOUD, unmissable alert (stderr banner + sentinel
#     file + macOS notification) so a human replugs it promptly.
#   - Append a timestamped ledger of every toggle/failure so the real failure
#     pattern can be analyzed from data later (no guessed threshold baked in).
WEMO_PORT="${AETHER_POWER_WEMO_PORT:-49153}"
WEMO_RETRIES="${AETHER_WEMO_RETRIES:-4}"
WEMO_ALERT="${AETHER_WEMO_ALERT:-/tmp/aether-wemo-ALERT}"
WEMO_LEDGER="${AETHER_WEMO_LEDGER:-/tmp/aether-wemo-toggles.log}"

wemo_soap() {  # $1=SOAPACTION method  $2=inner body xml  $3=timeout-secs; prints response body
  curl -fsS --max-time "${3:-8}" \
    "http://$HOST:$WEMO_PORT/upnp/control/basicevent1" \
    -H 'Content-Type: text/xml; charset="utf-8"' \
    -H "SOAPACTION: \"urn:Belkin:service:basicevent:1#$1\"" \
    -d "<?xml version=\"1.0\" encoding=\"utf-8\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>$2</s:Body></s:Envelope>" \
    2>/dev/null
}

wemo_get() {  # prints current BinaryState digit (0=off, 1/8=on) or nothing on failure
  wemo_soap GetBinaryState '<u:GetBinaryState xmlns:u="urn:Belkin:service:basicevent:1"></u:GetBinaryState>' 5 \
    | grep -oE '<BinaryState>[0-9]+' | grep -oE '[0-9]+$' || true
}

wemo_alert() {  # loud + unmissable — the plug is hung and needs a physical replug
  local msg="$1"
  { echo ""
    echo "  =================== WEMO UNRESPONSIVE ==================="
    echo "  $HOST:$WEMO_PORT did not confirm — $msg"
    echo "  Known WSP080 failure: UPnP control stops responding after a while."
    echo "  FIX: physically UNPLUG the Wemo, wait ~5s, REPLUG it, then re-run."
    echo "  ========================================================"
    echo ""; } >&2
  printf '%s  WEMO UNRESPONSIVE %s:%s — %s\n' "$(date '+%F %T')" "$HOST" "$WEMO_PORT" "$msg" > "$WEMO_ALERT" 2>/dev/null || true
  osascript -e "display notification \"Wemo unresponsive — unplug/replug $HOST\" with title \"AetherKernel power\" sound name \"Basso\"" >/dev/null 2>&1 || true
}

wemo_set() {  # $1 = desired BinaryState (1/0): set, verify, retry; loud-fail if unconfirmed
  need_host
  local want="$1" attempt got
  if [ "$DRY_RUN" = "1" ]; then echo "  [dry-run] wemo SetBinaryState $want (verified+retried)"; return 0; fi
  for attempt in $(seq 1 "$WEMO_RETRIES"); do
    wemo_soap SetBinaryState "<u:SetBinaryState xmlns:u=\"urn:Belkin:service:basicevent:1\"><BinaryState>$want</BinaryState></u:SetBinaryState>" "$((6 + attempt * 2))" >/dev/null 2>&1 || true
    got="$(wemo_get)"
    if { [ "$want" = "1" ] && { [ "$got" = "1" ] || [ "$got" = "8" ]; }; } \
       || { [ "$want" = "0" ] && [ "$got" = "0" ]; }; then
      printf '%s  set=%s confirmed=%s attempt=%s\n' "$(date '+%F %T')" "$want" "$got" "$attempt" >> "$WEMO_LEDGER" 2>/dev/null || true
      [ "$attempt" -gt 1 ] && log "wemo set=$want confirmed on attempt $attempt"
      return 0
    fi
    log "wemo set=$want unconfirmed (got '${got:-no-response}'), attempt $attempt/$WEMO_RETRIES"
    sleep 2
  done
  printf '%s  set=%s FAILED after %s attempts\n' "$(date '+%F %T')" "$want" "$WEMO_RETRIES" >> "$WEMO_LEDGER" 2>/dev/null || true
  wemo_alert "SetBinaryState $want unconfirmed after $WEMO_RETRIES attempts"
  return 1
}

# Shelly Gen2+/Gen4 (S4PL-00116US) local HTTP RPC. Backend name is `shelly`.
# Optional digest: AETHER_POWER_SHELLY_AUTH=user:password (not a cloud API).
shelly_set() {  # $1=true|false — Switch.Set then confirm via Switch.GetStatus output
  need_host
  local want="$1" got
  if [ -n "${AETHER_POWER_SHELLY_AUTH:-}" ]; then
    run "curl -fsS --digest -u \"$AETHER_POWER_SHELLY_AUTH\" --max-time 8 \"http://$HOST/rpc/Switch.Set?id=0&on=$want\" >/dev/null"
  else
    run "curl -fsS --max-time 8 \"http://$HOST/rpc/Switch.Set?id=0&on=$want\" >/dev/null"
  fi
  if [ "$DRY_RUN" = "1" ]; then
    echo "  [dry-run] would confirm via http://$HOST/rpc/Switch.GetStatus?id=0 output==$want"
    return 0
  fi
  if [ -n "${AETHER_POWER_SHELLY_AUTH:-}" ]; then
    got="$(curl -fsS --digest -u "$AETHER_POWER_SHELLY_AUTH" --max-time 5 \
      "http://$HOST/rpc/Switch.GetStatus?id=0" 2>/dev/null \
      | grep -oE '"output"[[:space:]]*:[[:space:]]*(true|false)' | grep -oE 'true|false' || true)"
  else
    got="$(curl -fsS --max-time 5 "http://$HOST/rpc/Switch.GetStatus?id=0" 2>/dev/null \
      | grep -oE '"output"[[:space:]]*:[[:space:]]*(true|false)' | grep -oE 'true|false' || true)"
  fi
  [ "$got" = "$want" ] || die "shelly set on=$want unconfirmed (GetStatus output='${got:-no-response}')"
}

power_off() {
  case "$BACKEND" in
    wemo)    wemo_set 0 ;;
    shelly)  shelly_set false ;;
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
    shelly)  shelly_set true ;;
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
