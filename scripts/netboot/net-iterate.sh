#!/usr/bin/env bash
#===----------------------------------------------------------------------===#
# One-command AetherKernel network iteration loop.
#
#   usage: ./net-iterate.sh [tftp-root]
#
# Builds and stages kernel8.img/config.txt, sends the serial reset command, and
# watches TFTP + serial logs for proof that the Pi fetched over TFTP,
# booted the staged image, brought up the Runtime V41 shell, and proves a small
# command set through ./serial-probe.sh.
#===----------------------------------------------------------------------===#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIPTS_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
TFTP_ROOT="${1:-${AETHER_TFTP_ROOT:-$HOME/aether-tftp}}"
PREFIX="${AETHER_TFTP_PREFIX:-aether}"
PREFIX="${PREFIX#/}"
PREFIX="${PREFIX%/}"
SERIAL_PORT="${AETHER_SERIAL_PORT:-/dev/cu.usbserial-B0044J1V}"
SERIAL_LOG="${AETHER_SERIAL_LOG:-/tmp/aether-serial.log}"
DNSMASQ_LOG="${AETHER_DNSMASQ_LOG:-${AETHER_TFTP_LOG:-/tmp/aether-dnsmasq.log}}"
TIMEOUT_S="${AETHER_NETITERATE_TIMEOUT:-150}"
RETRIES="${AETHER_NETITERATE_RETRIES:-3}"
PROBE_TIMEOUT_S="${AETHER_NETITERATE_PROBE_TIMEOUT:-30}"

usage() {
  sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'
}

die() {
  echo "net-iterate: $*" >&2
  exit 1
}

sha256_file() {
  shasum -a 256 "$1" | awk '{print $1}'
}

parse_netflash_kernel_sha256() {
  local output="$1"
  local hash
  hash="$(printf '%s\n' "$output" | sed -n 's/^verified kernel8\.img sha256 \([0-9a-f]\{64\}\)$/\1/p' | head -n 1)"
  if [ -z "$hash" ]; then
    die "netflash did not report verified kernel8.img sha256"
  fi
  printf '%s' "$hash"
}

bind_staged_kernel_sha256() {
  local netflash_output="$1"
  local staged_kernel="$2"
  local netflash_hash
  local staged_hash

  netflash_hash="$(parse_netflash_kernel_sha256 "$netflash_output")"
  staged_hash="$(sha256_file "$staged_kernel")"
  if [ "$netflash_hash" != "$staged_hash" ]; then
    echo "net-iterate: staged kernel8.img sha256 mismatch" >&2
    echo "  netflash: $netflash_hash" >&2
    echo "  staged:   $staged_hash" >&2
    exit 1
  fi
  printf '%s' "$netflash_hash"
}

print_tftp_diagnostics() {
  local dns_delta="$1"

  if printf '%s' "$dns_delta" | grep -Eqa "failed sending .*/start4\\.elf|timeout sending .*/start4\\.elf"; then
    echo "diagnostic: Pi bootloader did not reliably receive start4.elf over TFTP."
    echo "diagnostic: kernel was not reached; try restarting serve-netboot with AETHER_TFTP_NO_BLOCKSIZE=1 for an A/B test."
  fi

  if printf '%s' "$dns_delta" | grep -Eqa "failed sending .*/kernel8\\.img|timeout sending .*/kernel8\\.img"; then
    echo "diagnostic: kernel8.img transfer was attempted but not cleanly completed before fallback/retry."
  fi
}

probe_shell() {
  local command="$1"
  local expected="$2"
  local probe_timeout="${AETHER_SERIAL_PROBE_TIMEOUT:-10}"

  echo "probe shell: $command"
  AETHER_SERIAL_PROBE_TIMEOUT="$probe_timeout" "$SCRIPTS_ROOT/serial/serial-probe.sh" "$command" "$expected" "$SERIAL_PORT"
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
  echo "shell probes: ./serial-probe.sh status timers protocol bootcert sched sched2 sched3 sched4 sched5 sched6 sched7 sched8 sched9 sched10 sched11 cores locks runqueues req-status req-sched req-cores req-locks req-runqueues req-sched2 req-sched3 req-sched4 req-sched5 req-sched6 req-sched7 req-sched8 req-sched9 req-sched10 req-sched11 canceltest taskcheck channeltest mmu poolcheck pools heapfrag poolstats bootcheck stress soak kobjects drivers drivercheck tasks2 mailboxes sendtest supervisor health capcheck events vmm asplit el0 syscall uaccess usermode process loader multiprocess sdhci card block fat32 mailbox framebuf console pcie vl805 xhci"
  exit 0
fi

[ -f "$SERIAL_LOG" ] || die "serial log missing: $SERIAL_LOG"
[ -f "$DNSMASQ_LOG" ] || die "TFTP log missing: $DNSMASQ_LOG"
[ -d "$TFTP_ROOT/$PREFIX" ] || die "TFTP prefix missing: $TFTP_ROOT/$PREFIX"
tftp_server_running || die "TFTP server does not appear to be serving $TFTP_ROOT"

STAGED_KERNEL="$TFTP_ROOT/$PREFIX/kernel8.img"
netflash_output="$("$SCRIPT_DIR/netflash.sh" "$TFTP_ROOT")"
printf '%s\n' "$netflash_output"
[ -f "$STAGED_KERNEL" ] || die "staged kernel missing: $STAGED_KERNEL"
KERNEL_SHA256="$(bind_staged_kernel_sha256 "$netflash_output" "$STAGED_KERNEL")"

attempt=1
last_dns_delta=""
last_serial_delta=""

while [ "$attempt" -le "$RETRIES" ]; do
  serial_start="$(file_size "$SERIAL_LOG")"
  dns_start="$(file_size "$DNSMASQ_LOG")"
  sd_fallback_seen=0

  echo "netboot attempt ${attempt}/${RETRIES}: reset Pi, then wait up to ${TIMEOUT_S}s for TFTP fetch + fresh AetherKernel boot..."

  # Start (or re-start) serial capture BEFORE the power-cycle / reset.
  # This ensures the one-time early boot banner (e.g. "runtime v45: ...") and initial
  # kernel output are captured in the log. For cold-cycles (Shelly via netboot-auto)
  # the power-on triggers the bootloader netboot + kernel boot; logger must be
  # attached to the serial port *before* power-on. Warm resets may miss it too.
  # Moving this before the cycle fixes the ordering for all future slices.
  if [ -x "$SCRIPTS_ROOT/serial/serial-capture.sh" ]; then
    "$SCRIPTS_ROOT/serial/serial-capture.sh" "$SERIAL_PORT" >/dev/null
  fi

  # Host pings during the genet9 boot window. Start before power-cycle so
  # packets can arrive while the kernel is still in the bounded RX wait.
  # Kernel must boot if none arrive.
  ( ping -c 80 -W 1 10.42.0.2 >/dev/null 2>&1 || true ) &

  # V118: host UDP echo on 10.42.0.1:41240 so boot-time originate can fail-close.
  if ! pgrep -f 'aether-udp-echo-v118' >/dev/null 2>&1; then
    python3 "$SCRIPT_DIR/aether-udp-echo-v118.py" >/tmp/aether-udp-echo-v118.log 2>&1 &
    disown $! || true
  fi

  # V119: host TCP echo on 10.42.0.1:41241 so boot-time originate can fail-close.
  if ! pgrep -f 'aether-tcp-echo-v119' >/dev/null 2>&1; then
    python3 "$SCRIPT_DIR/aether-tcp-echo-v119.py" >/tmp/aether-tcp-echo-v119.log 2>&1 &
    disown $! || true
  fi

  # V120: known TFTP payload so boot-time RRQ can fail-close. New file only.
  python3 - "$TFTP_ROOT/$PREFIX/v120.bin" <<'PY'
from pathlib import Path
import sys
Path(sys.argv[1]).write_bytes(bytes.fromhex("A1200001A1200002A1200003A1200004"))
PY

  # V121: host mDNS A for aether-v121.local so boot-time query can fail-close.
  if ! pgrep -f 'aether-mdns-v121' >/dev/null 2>&1; then
    python3 "$SCRIPT_DIR/aether-mdns-v121.py" >/tmp/aether-mdns-v121.log 2>&1 &
    disown $! || true
  fi

  # V122: host HTTP/1.0 GET /aether/v122.txt so boot-time originate can fail-close.
  if ! pgrep -f 'aether-http-v122' >/dev/null 2>&1; then
    python3 "$SCRIPT_DIR/aether-http-v122.py" >/tmp/aether-http-v122.log 2>&1 &
    disown $! || true
  fi

  if [ -n "${AETHER_POWER_BACKEND:-}" ] && [ "${AETHER_POWER_BACKEND}" != "none" ]; then
    # Cold power-cycle via external switch — REQUIRED for the Pi bootloader to
    # re-enter netboot/TFTP mode (a warm serial reset does not re-arm it). This is
    # what lets unattended runs self-recover with no human at the bench.
    "$SCRIPTS_ROOT/power-cycle.sh" cycle || die "power-cycle failed (backend=${AETHER_POWER_BACKEND})"
  else
    # Default: warm serial reset. NOTE: this does NOT re-arm netboot mode, so the
    # Pi must already be in netboot (fresh cold boot). Set AETHER_POWER_BACKEND
    # (e.g. shelly) for a true unattended cold cycle. See power-cycle.sh.
    "$SCRIPTS_ROOT/serial/serial-reset.sh" "$SERIAL_PORT"
  fi
  # (serial-capture now started before the cycle above; removed duplicate start)

  deadline=$((SECONDS + TIMEOUT_S))
  while [ "$SECONDS" -lt "$deadline" ]; do
    dns_delta="$(file_delta "$DNSMASQ_LOG" "$dns_start")"
    serial_delta="$(file_delta "$SERIAL_LOG" "$serial_start")"

    if printf '%s' "$serial_delta" | grep -qa "async heartbeat: timer-backed sleep 1s"; then
      echo "netboot attempt ${attempt}/${RETRIES} booted stale SD fallback image detected"
      echo "--- serial delta from stale fallback ---"
      printf '%s\n' "$serial_delta" | tail -n 120
      exit 2
    fi

    if printf '%s' "$dns_delta" | grep -qa "$PREFIX/.*kernel8.img" \
      && printf '%s' "$serial_delta" | grep -qa "=== AetherKernel ===" \
      && printf '%s' "$serial_delta" | grep -qa "rtv2 fast woke 0x0000000000000000" \
      && printf '%s' "$serial_delta" | grep -qa "rtv2 slow woke 0x0000000000000000" \
      && printf '%s' "$serial_delta" | grep -qa "rtv2 long woke 0x0000000000000000" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v5: diagnostics shell" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v6: retained panic/fault records" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v7: memory map + frame allocator" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v8: allocator guardrails" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v9: bounded memory pressure self-tests" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v10: explicit guard probes" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v11: boot and soak invariants" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v12: kernel object table + task registry" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v13: bounded mailbox message queues" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v14: deterministic task supervisor" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v15: capability-tagged kernel handles" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v16: kernel event log ring" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v17: deterministic boot certificate" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v18: cooperative cancellation tokens" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v19: structured aether task spawn" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v20: bounded async channels" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v21: mmu ownership boundary" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v22: guarded typed pools" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v23: allocator and pool pressure telemetry" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v24: fixed driver registry" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v25: scriptable command protocol v2" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v27: panic taxonomy and symbolic retained records" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v28: swift runtime dependency audit" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v29: agent-oriented control session" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v30: swift-native kernel substrate certificate" \
      && printf '%s' "$serial_delta" | grep -qa "schedselftest ok=1 version=31" \
      && printf '%s' "$serial_delta" | grep -qa "schedselftest ok=1 version=32" \
      && printf '%s' "$serial_delta" | grep -qa "schedselftest ok=1 version=33" \
      && printf '%s' "$serial_delta" | grep -qa "schedselftest ok=1 version=34" \
      && printf '%s' "$serial_delta" | grep -qa "schedselftest ok=1 version=35" \
      && printf '%s' "$serial_delta" | grep -qa "schedselftest ok=1 version=36" \
      && printf '%s' "$serial_delta" | grep -qa "schedselftest ok=1 version=37" \
      && printf '%s' "$serial_delta" | grep -qa "schedselftest ok=1 version=38" \
      && printf '%s' "$serial_delta" | grep -qa "schedselftest ok=1 version=39" \
      && printf '%s' "$serial_delta" | grep -qa "schedselftest ok=1 version=40" \
      && printf '%s' "$serial_delta" | grep -qa "schedselftest ok=1 version=41" \
      && printf '%s' "$serial_delta" | grep -qa "schedselftest ok=1 version=42" \
      && printf '%s' "$serial_delta" | grep -qa "schedselftest ok=1 version=43" \
      && printf '%s' "$serial_delta" | grep -qa "schedselftest ok=1 version=44" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v45: dynamic virtual memory (page tables + TLB)" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v46: kernel/user address-space split (isolated page tables)" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v47: EL0 entry/exit and context save/restore" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v48: syscall ABI via SVC from EL0" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v49: fault-safe copy_from_user / copy_to_user" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v50: EPIC A capstone" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v51: process abstraction" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v52: user binary loader" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v53: multi-process user execution" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v54: BCM2711 EMMC2/SDHCI register probe" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v55: SD card identification (CMD0/CMD8/ACMD41/CMD2/CMD3)" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v56: single block read CMD17 + MBR 0x55AA verification" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v57: FAT32 file read (config.txt bytes + checksum)" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v58: VideoCore mailbox property interface (firmware revision)" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v59: VideoCore framebuffer allocation (width/height/pitch/addr)" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v60: text console (8x8 font blit + readback proof)" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v61: BCM2711 PCIe RC bring-up" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v62: VL805 USB 3.0 xHCI config-space probe" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v63: xHCI capability register probe" \
      && printf '%s' "$serial_delta" | grep -qa "sdhci ok=1 version=54" \
      && printf '%s' "$serial_delta" | grep -qa "card ok=1 version=55" \
      && printf '%s' "$serial_delta" | grep -qa "block ok=1 version=56" \
      && printf '%s' "$serial_delta" | grep -qa "fat32 ok=1 version=57" \
      && printf '%s' "$serial_delta" | grep -qa "mailbox ok=1 version=58" \
      && printf '%s' "$serial_delta" | grep -qa "framebuf ok=1 version=59" \
      && printf '%s' "$serial_delta" | grep -qa "console ok=1 version=60" \
      && printf '%s' "$serial_delta" | grep -qa "pcie ok=1 version=61" \
      && printf '%s' "$serial_delta" | grep -qa "vl805 ok=1 version=62" \
      && printf '%s' "$serial_delta" | grep -qa "xhci ok=1 version=63" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v64: xHCI controller init" \
      && printf '%s' "$serial_delta" | grep -qa "xhci_run ok=1 version=64 ports_connected=.*" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v65: USB device enumeration" \
      && printf '%s' "$serial_delta" | grep -qa "usb_enum ok=[01] version=65 addr=.* vendor=.* product=.* class=.* stage=.* portsc=.* portscR=.* slot_raw=.* ad_raw=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v66: HID boot-protocol keyboard" \
      && printf '%s' "$serial_delta" | grep -qa "kbd ok=[01] version=66 keycode=.* char=" \
      && printf '%s' "$serial_delta" | grep -qa "hubwalk ok=[01] version=66 ports=.* connected=.* hid=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v67: GENET register probe" \
      && printf '%s' "$serial_delta" | grep -qa "genet ok=1 version=67 rev=.* mdio=.* link=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v68: GENET UMAC MAC and RX MIB" \
      && printf '%s' "$serial_delta" | grep -qa "genet2 ok=1 version=68 mac=.* rx=.* frames=.* bytes=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v69: GENET mailbox station MAC" \
      && printf '%s' "$serial_delta" | grep -qa "genet3 ok=1 version=69 mac=.* mbox=.* umac=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v70: GENET mailbox board serial" \
      && printf '%s' "$serial_delta" | grep -qa "genet4 ok=1 version=70 serial=.* mbox=.* mac=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v71: GPIO register probe" \
      && printf '%s' "$serial_delta" | grep -qa "gpio ok=1 version=71 fsel=.* pup=.* uart=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v72: GENET leftover-RX stop and NC RX ring" \
      && printf '%s' "$serial_delta" | grep -qa "genet5 ok=1 version=72 stop=.* ring=.* rx=.* frames=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v73: GENET UMAC MAC and TX ARP" \
      && printf '%s' "$serial_delta" | grep -qa "genet6 ok=1 version=73 mac=.* tx=.* frames=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v74: GENET Linux ring-16 and TX CONS" \
      && printf '%s' "$serial_delta" | grep -qa "genet7 ok=1 version=74 ring=.* tx=.* cons=.* prod=.* frames=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v75: GENET v4 TDMA PROD doorbell" \
      && printf '%s' "$serial_delta" | grep -qa "genet8 ok=1 version=75 prod=.* cons=.* tx=.* frames=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v76: GENET ARP or ICMP reply" \
      && printf '%s' "$serial_delta" | grep -qa "genet9 ok=1 version=76 rx=.* tx=.* kind=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v77: GENET bounded multi-reply poll" \
      && printf '%s' "$serial_delta" | grep -qa "genet10 ok=1 version=77 rx=.* tx=.* replies=.* kind=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v78: GENET bounded UDP echo" \
      && printf '%s' "$serial_delta" | grep -qa "genet11 ok=1 version=78 rx=.* tx=.* replies=.* kind=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v79: GENET bounded TCP echo" \
      && printf '%s' "$serial_delta" | grep -qa "genet12 ok=1 version=79 rx=.* tx=.* replies=.* kind=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v80: I2C and SPI register probe" \
      && printf '%s' "$serial_delta" | grep -qa "i2c ok=1 version=80 bsc=.* div=.* spi=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v81: PWM register probe" \
      && printf '%s' "$serial_delta" | grep -qa "pwm ok=1 version=81 ctl=.* sta=.* pwm1=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v82: I2C no-ACK transfer" \
      && printf '%s' "$serial_delta" | grep -qa "i2c2 ok=1 version=82 nack=1 addr=.* sta=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v83: SPI0 bounded transfer" \
      && printf '%s' "$serial_delta" | grep -qa "spi2 ok=1 version=83 done=1 loop=.* rx=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v84: system timer register probe" \
      && printf '%s' "$serial_delta" | grep -qa "stimer ok=1 version=84 clo=.* chi=.* chans=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v85: SD config.txt reload" \
      && printf '%s' "$serial_delta" | grep -qa "sdload ok=1 version=85 file=config.txt bytes=.* checksum=.* match=1" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v86: FAT32 root list" \
      && printf '%s' "$serial_delta" | grep -qa "sdls ok=1 version=86 files=.* config=1 other=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v87: FAT32 second file" \
      && printf '%s' "$serial_delta" | grep -qa "sdfile ok=1 version=87 name=.* bytes=.* checksum=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v88: FAT32 overlays walk" \
      && printf '%s' "$serial_delta" | grep -qa "sdovl ok=1 version=88 files=.* name=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v89: FAT32 overlay file" \
      && printf '%s' "$serial_delta" | grep -qa "sdovf ok=1 version=89 name=.* bytes=.* checksum=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v90: FAT32 issue.txt" \
      && printf '%s' "$serial_delta" | grep -qa "sdiss ok=[01] version=90 present=[01] name=.* bytes=.* checksum=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v91: GPIO output readback" \
      && printf '%s' "$serial_delta" | grep -qa "gpio2 ok=1 version=91 pin=42 set=1 clr=1" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v92: system timer C1 match" \
      && printf '%s' "$serial_delta" | grep -qa "stimer2 ok=1 version=92 chan=1 match=1" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v93: PWM clock enable" \
      && printf '%s' "$serial_delta" | grep -qa "pwm2 ok=1 version=93 clk=1 en=1" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v94: GPIO PUP readback" \
      && printf '%s' "$serial_delta" | grep -qa "gpio3 ok=1 version=94 pin=26 up=1 dn=1" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v95: system timer C3 match" \
      && printf '%s' "$serial_delta" | grep -qa "stimer3 ok=1 version=95 chan=3 match=1" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v96: mailbox temperature" \
      && printf '%s' "$serial_delta" | grep -qa "mboxt ok=1 version=96 temp=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v97: mailbox clock rate" \
      && printf '%s' "$serial_delta" | grep -qa "mboxc ok=1 version=97 clk=3 hz=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v98: watchdog remaining" \
      && printf '%s' "$serial_delta" | grep -qa "wdog2 ok=1 version=98 armed=1 off=1 remain=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v99: mailbox voltage" \
      && printf '%s' "$serial_delta" | grep -qa "mboxv ok=1 version=99 id=1 uv=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v100: RNG200 word" \
      && printf '%s' "$serial_delta" | grep -qa "rng ok=1 version=100 ready=1 data=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v101: DMA memcpy" \
      && printf '%s' "$serial_delta" | grep -qa "dma2 ok=1 version=101 chan=4 match=1 bytes=32" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v102: SD free-cluster write" \
      && printf '%s' "$serial_delta" | grep -qa "sdwr ok=1 version=102 match=1 bytes=512 clus=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v103: FAT32 scratch create" \
      && printf '%s' "$serial_delta" | grep -qa "sdmk ok=1 version=103 match=1 created=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v104: FAT32 scratch reread" \
      && printf '%s' "$serial_delta" | grep -qa "sdrd ok=1 version=104 match=1 present=1 name=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v105: SDHCI card status" \
      && printf '%s' "$serial_delta" | grep -qa "sdst ok=1 version=105 state=4 ready=1 rca=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v106: SDHCI send SCR" \
      && printf '%s' "$serial_delta" | grep -qa "sdscr ok=1 version=106 spec=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v107: SDHCI SD_STATUS" \
      && printf '%s' "$serial_delta" | grep -qa "sdss ok=1 version=107 type=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v108: SDHCI bus width" \
      && printf '%s' "$serial_delta" | grep -qa "sdbus ok=1 version=108 bits=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v109: SDHCI multi-block" \
      && printf '%s' "$serial_delta" | grep -qa "sdmb ok=1 version=109 blocks=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v110: SDHCI switch check" \
      && printf '%s' "$serial_delta" | grep -qa "sdsw ok=1 version=110 grp1=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v111: SDHCI multi-block write" \
      && printf '%s' "$serial_delta" | grep -qa "sdmw ok=1 version=111 match=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v112: SDHCI set block count" \
      && printf '%s' "$serial_delta" | grep -qa "sdbc ok=1 version=112 count=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v113: FAT32 FSInfo" \
      && printf '%s' "$serial_delta" | grep -qa "sdfi ok=1 version=113 lead=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v114: FAT32 backup boot" \
      && printf '%s' "$serial_delta" | grep -qa "sdfb ok=1 version=114 match=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v115: FAT32 FAT mirror" \
      && printf '%s' "$serial_delta" | grep -qa "sdfm ok=1 version=115 match=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v116: FAT32 scratch unlink" \
      && printf '%s' "$serial_delta" | grep -qa "sdrm ok=1 version=116 deleted=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v117: GENET originate ping" \
      && printf '%s' "$serial_delta" | grep -qa "genet13 ok=1 version=117 arp=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v118: GENET originate UDP" \
      && printf '%s' "$serial_delta" | grep -qa "genet14 ok=1 version=118 udp=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v119: GENET originate TCP" \
      && printf '%s' "$serial_delta" | grep -qa "genet15 ok=1 version=119 tcp=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v120: GENET originate TFTP" \
      && printf '%s' "$serial_delta" | grep -qa "genet16 ok=1 version=120 tftp=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v121: GENET originate mDNS" \
      && printf '%s' "$serial_delta" | grep -qa "genet17 ok=1 version=121 mdns=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v122: GENET originate HTTP" \
      && printf '%s' "$serial_delta" | grep -qa "genet18 ok=1 version=122 http=" \
      && printf '%s' "$serial_delta" | grep -qa "vmmcheck ok=1" \
      && printf '%s' "$serial_delta" | grep -qa "asplit ok=1 version=46" \
      && printf '%s' "$serial_delta" | grep -qa "el0 ok=1 version=47" \
      && printf '%s' "$serial_delta" | grep -qa "syscall ok=1 version=48 abi=48 table=1 dispatched=1 num=1 ret=0x0000000000482026" \
      && printf '%s' "$serial_delta" | grep -qa "uaccess ok=1 version=49" \
      && printf '%s' "$serial_delta" | grep -qa "usermode ok=1 version=50 fault_contained=1" \
      && printf '%s' "$serial_delta" | grep -qa "process ok=1 version=51" \
      && printf '%s' "$serial_delta" | grep -qa "processes ok=1 version=52" \
      && printf '%s' "$serial_delta" | grep -qa "multiprocess ok=1 version=53" \
      && printf '%s' "$serial_delta" | grep -qa "handlecheck ok=1 .*handle_selftest=1 .*cap_selftest=1" \
      && printf '%s' "$serial_delta" | grep -qa "rtv13 mail tx 0x0000000000000000" \
      && printf '%s' "$serial_delta" | grep -qa "rtv13 mail rx 0x0000000000000000" \
      && printf '%s' "$serial_delta" | grep -qa "shell ready commands=help,protocol,status,heap,queues,tasks,tasks2,kobjects,drivers,drivercheck,mailboxes,sendtest,supervisor,health,handlecheck,capcheck,events,runtime,agent,certificate,sched,sched2,sched3,sched4,sched5,sched6,sched7,sched8,sched9,sched10,sched11,sched12,cores,locks,runqueues,diag,irqs,timers,memcheck,faults,retained,retained-clear,memmap,mmu,pools,poolcheck,heapfrag,poolstats,frames,heapcheck,framecheck,stress,frameprobe,bootcert,canceltest,taskcheck,channeltest,bootcheck,soak,heap-invalid-free-test,heap-double-free-test,panic-test,fault-test,reboot,vmm,asplit,el0,syscall,uaccess,usermode,process,loader,multiprocess,sdhci,card,block,fat32,mailbox,framebuf,console,pcie,vl805,xhci"; then
      echo "netboot iteration verified on attempt ${attempt}/${RETRIES}"
      echo "verified kernel8.img sha256 $KERNEL_SHA256"
      if [ "${AETHER_NETITERATE_SKIP_SHELL_PROBES:-0}" != "1" ]; then
        export AETHER_SERIAL_PROBE_TIMEOUT="$PROBE_TIMEOUT_S"
        # probe shell: status
        probe_shell "status" "^status uptime_ms=.*timer_mask="
        # probe shell: timers (printTimers schema: now= freq= active_count= active_mask= sleep_deadline= sleepers=; no executor_deadline=)
        probe_shell "timers" "^timers now=.* freq=.* active_count=.* active_mask=.* sleep_deadline=.* sleepers="
        # probe shell: queues (printQueues schema: ready= sleepers= timer_mask=; no delayed=)
        probe_shell "queues" "^queues ready=.* sleepers=.* timer_mask="
        # probe shell: protocol
        probe_shell "protocol" "^protocol version=2 .*begin_end=1 .*errors=1"
        # probe shell: runtime
        probe_shell "runtime" "^runtime ok=1 version=28 .*swift=6.3.2 .*source_hooks=10 .*linked_hooks=2 .*heap_shims=5 .*linked_heap_shims=3 .*required_symbols=5"
        # probe shell: agent
        probe_shell "agent" "^agent ok=1 version=29 health=green .*bootcert=1 .*runtime=1 .*protocol=2 .*events_lost=0"
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
        # probe shell: sched7
        probe_shell "sched7" "^sched7 ok=1 version=39 .*handoff=1 .*wake=1 .*job_exec=1 .*issued=[1-9][0-9]* .*completed=[1-9][0-9]* .*gap=[0-9][0-9]* .*imbalance=[0-9][0-9]* .*core0_issue=0 .*core1_issue=[1-9][0-9]* .*core2_issue=[1-9][0-9]* .*core3_issue=[1-9][0-9]* .*core0_done=0 .*core1_done=[1-9][0-9]* .*core2_done=[1-9][0-9]* .*core3_done=[1-9][0-9]* .*selftest=1"
        # probe shell: sched8
        probe_shell "sched8" "^sched8 ok=1 version=40 .*backpressure=1 .*handoff=1 .*wake=1 .*high_water=[8-9][0-9]* .*overflows=[1-9][0-9]* .*total=0 .*capacity=8 .*core0_high=8 .*core1_high=8 .*core2_high=8 .*core3_high=8 .*core0_overflow=[1-9][0-9]* .*core1_overflow=[1-9][0-9]* .*core2_overflow=[1-9][0-9]* .*core3_overflow=[1-9][0-9]* .*selftest=1"
        # probe shell: sched9
        # sched9: steal DESTINATION distribution is nondeterministic (all steals
        # can land on one core); the kernel's selftest=1 plus steals/completions
        # >=1 is the real gate. Destination counts only need to be present.
        probe_shell "sched9" "^sched9 ok=1 version=41 .*stealing=1 .*backpressure=1 .*handoff=1 .*wake=1 .*steals=[1-9][0-9]* .*completions=[1-9][0-9]* .*total=0 .*capacity=8 .*source_core1=[1-9][0-9]* .*source_core2=[0-9][0-9]* .*source_core3=[0-9][0-9]* .*dest_core1=0 .*dest_core2=[0-9][0-9]* .*dest_core3=[0-9][0-9]* .*attempts_core1=[0-9][0-9]* .*attempts_core2=[1-9][0-9]* .*attempts_core3=[1-9][0-9]* .*selftest=1"
        # probe shell: sched10
        AETHER_SERIAL_PROBE_TIMEOUT="${AETHER_NETITERATE_SLOW_PROBE_TIMEOUT:-180}" \
          probe_shell "sched10" "^sched10 ok=1 version=42 .*fairness=1 .*stealing=1 .*backpressure=1 .*handoff=1 .*wake=1 .*balances=[1-9][0-9]* .*completions=[1-9][0-9]* .*total=0 .*capacity=8 .*source_core1=[1-9][0-9]* .*source_core2=[0-9][0-9]* .*source_core3=[0-9][0-9]* .*dest_core1=0 .*dest_core2=[0-9][0-9]* .*dest_core3=[0-9][0-9]* .*attempts_core1=[0-9][0-9]* .*attempts_core2=[1-9][0-9]* .*attempts_core3=[1-9][0-9]* .*queue_imbalance=[0-9][0-9]* .*selftest=1"
        export AETHER_SERIAL_PROBE_TIMEOUT="$PROBE_TIMEOUT_S"
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
        # probe shell: req-sched7
        probe_shell "req id=40 cmd=sched7" "^resp id=40 ok=1 cmd=sched7 end"
        # probe shell: req-sched8
        probe_shell "req id=41 cmd=sched8" "^resp id=41 ok=1 cmd=sched8 end"
        # probe shell: req-sched9
        probe_shell "req id=42 cmd=sched9" "^resp id=42 ok=1 cmd=sched9 end"
        # probe shell: req-sched10
        probe_shell "req id=43 cmd=sched10" "^resp id=43 ok=1 cmd=sched10 end"
        # probe shell: sched11
        AETHER_SERIAL_PROBE_TIMEOUT="${AETHER_NETITERATE_SLOW_PROBE_TIMEOUT:-180}" \
          probe_shell "sched11" "^sched11 ok=1 version=43 .*priority=1 .*fairness=1 .*stealing=1 .*backpressure=1 .*handoff=1 .*wake=1 .*preemptions=[1-9][0-9]* .*yields=[1-9][0-9]* .*completions=[1-9][0-9]* .*imbalance=[0-9][0-9]* .*total=0 .*capacity=8 .*low_core1=[1-9][0-9]* .*low_core2=[0-9][0-9]* .*low_core3=[0-9][0-9]* .*high_core1=[1-9][0-9]* .*high_core2=[0-9][0-9]* .*high_core3=[0-9][0-9]* .*preempt_core1=[1-9][0-9]* .*preempt_core2=[0-9][0-9]* .*preempt_core3=[0-9][0-9]* .*yield_core1=[1-9][0-9]* .*yield_core2=[0-9][0-9]* .*yield_core3=[0-9][0-9]* .*selftest=1"
        # probe shell: req-sched11
        AETHER_SERIAL_PROBE_TIMEOUT="${AETHER_NETITERATE_SLOW_PROBE_TIMEOUT:-180}" \
          probe_shell "req id=44 cmd=sched11" "^resp id=44 ok=1 cmd=sched11 end"
        # probe shell: sched12
        AETHER_SERIAL_PROBE_TIMEOUT="${AETHER_NETITERATE_SLOW_PROBE_TIMEOUT:-180}" \
          probe_shell "sched12" "^sched12 ok=1 version=44 .*concurrency=1 .*rounds=3 .*completions=3 .*failures=0 .*dispatches=[1-9][0-9]* .*total=0 .*capacity=8 .*soak_core1=[1-9][0-9]* .*soak_core2=[1-9][0-9]* .*soak_core3=[1-9][0-9]* .*selftest=1"
        # probe shell: req-sched12
        AETHER_SERIAL_PROBE_TIMEOUT="${AETHER_NETITERATE_SLOW_PROBE_TIMEOUT:-180}" \
          probe_shell "req id=45 cmd=sched12" "^resp id=45 ok=1 cmd=sched12 end"
        # probe shell: bootcert (v66). Require ok=1; kbd=[01] is reported only
        # (structural kbd=0 without keypress must not false-pass). smp_scheduler
        # is an interval-based dispatch flag that can read 0 at probe time
        # (sched2/sched12 probes above are the real SMP dispatch gate).
        AETHER_SERIAL_PROBE_TIMEOUT="${AETHER_NETITERATE_SLOW_PROBE_TIMEOUT:-180}" \
          probe_shell "bootcert" "^bootcert ok=1 version=66 .*concurrency=1 .*priority=1 .*fairness=1 .*stealing=[01] .*backpressure=[01] .*handoff=[01] .*wake=[01] .*job_exec=[01] .*worker_feed=[01] .*secondary_workers=[01] .*preemptive=[01] .*smp_scheduler=[01] .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*certificate=1 .*agent=1 .*runtime=1 .*syscall=1 .*uaccess=1 .*usermode=1 .*process=1 .*loader=1 .*multiprocess=1 .*sdhci=1 .*card=1 .*block=1 .*fat32=1 .*mailbox=1 .*framebuf=1 .*console=1 .*pcie=1 .*vl805=1 .*xhci=1 .*kbd=[01] .*swift=6.3.2 .*events_lost=0"
        export AETHER_SERIAL_PROBE_TIMEOUT="$PROBE_TIMEOUT_S"
        # probe shell: certificate
        AETHER_SERIAL_PROBE_TIMEOUT="${AETHER_NETITERATE_SLOW_PROBE_TIMEOUT:-180}" \
          probe_shell "certificate" "^certificate ok=1 version=63 substrate=1 .*bootcert=[01] .*concurrency=1 .*priority=1 .*fairness=1 .*stealing=[01] .*backpressure=[01] .*handoff=[01] .*wake=[01] .*job_exec=[01] .*worker_feed=[01] .*secondary_workers=[01] .*preemptive=[01] .*smp_scheduler=[01] .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*agent=1 .*runtime=1 .*memory=1 .*objects=1 .*tasks=1 .*mailboxes=1 .*supervisor=1 .*handles=1 .*events=[0-9]+ .*cancellations=1 .*channels=1 .*drivers=1 .*pressure=1 .*pools=1 .*mmu=1 .*vmm=1 .*asplit=1 .*el0=1 .*syscall=1 .*uaccess=1 .*usermode=1 .*process=1 .*loader=1 .*multiprocess=1 .*sdhci=1 .*card=1 .*block=1 .*fat32=1 .*mailbox=1 .*framebuf=1 .*console=1 .*pcie=1 .*vl805=1 .*xhci=1 .*swift=6.3.2 .*events_lost=0"
        export AETHER_SERIAL_PROBE_TIMEOUT="$PROBE_TIMEOUT_S"
        # probe shell: xhci
        probe_shell "xhci" "^xhci ok=1 version=63 hciversion=0x100 ports=5 slots=32 scratch=31"
        # GENET probe: ok=1 is SYS_REV live. mdio/link may be 0 (fail-closed MDIO).
        probe_shell "genet" "^genet ok=1 version=67 rev=.* mdio=.* link="
        # GENET2: ok=1 is a valid unicast UMAC MAC. frames=0 is honest (no DMA rings).
        probe_shell "genet2" "^genet2 ok=1 version=68 mac=.* rx=.* frames=.* bytes="
        # GENET3: ok=1 is a non-zero firmware MAC. umac=0 is honest (no UMAC write).
        probe_shell "genet3" "^genet3 ok=1 version=69 mac=.* mbox=1 umac="
        # GENET4: ok=1 is a non-zero firmware serial. Prefer mailbox over MDIO.
        probe_shell "genet4" "^genet4 ok=1 version=70 serial=.* mbox=1 mac="
        # GPIO: ok=1 is live 2711 GPFSEL + PUP_PDN. uart=1 is GPIO14/15 ALT0.
        probe_shell "gpio" "^gpio ok=1 version=71 fsel=.* pup=.* uart=1"
        # GENET5: leftover RX stopped, our NC ring programmed, RX re-enabled.
        probe_shell "genet5" "^genet5 ok=1 version=72 stop=1 ring=1 rx=1 frames="
        # GENET6: mailbox MAC written to UMAC, one TX ARP issued. frames=0 is honest.
        probe_shell "genet6" "^genet6 ok=1 version=73 mac=1 tx=1 frames="
        # GENET7: Linux ring-16 geometry. tx=1 only if TDMA CONS moved. frames=0 is honest.
        probe_shell "genet7" "^genet7 ok=1 version=74 ring=1 tx=.* cons=.* prod=.* frames="
        # GENET8: v4 TDMA PROD at 0x0C. tx=1 only if CONS moved or PROD latched.
        probe_shell "genet8" "^genet8 ok=1 version=75 prod=.* cons=.* tx=.* frames="
        # GENET9: ARP or ICMP reply. kind=none if no request arrived. Do not require ping.
        probe_shell "genet9" "^genet9 ok=1 version=76 rx=.* tx=.* kind="
        # GENET10: bounded unpark/poll/park. Host ping must overlap this window.
        ping_out="${AETHER_PING_LOG:-/tmp/aether-ping-genet10.txt}"
        : > "$ping_out"
        # Delay ping until UART has delivered genet10 and the 4s poll is unparked.
        ( sleep 0.5; ping -c 2 -W 2 10.42.0.2 > "$ping_out" 2>&1 ) &
        ping_pid=$!
        AETHER_SERIAL_PROBE_TIMEOUT=15 probe_shell "genet10" "^genet10 ok=1 version=77 rx=.* tx=.* replies=.* kind="
        wait "$ping_pid" || true
        echo "==== host ping 10.42.0.2 ===="
        cat "$ping_out" || true
        echo "==== end host ping ===="
        # GENET11: bounded UDP echo on port 7. Host datagrams must overlap this window.
        udp_out="${AETHER_UDP_LOG:-/tmp/aether-udp-genet11.txt}"
        : > "$udp_out"
        ( sleep 0.5; python3 - <<'PY' > "$udp_out" 2>&1
import socket
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.settimeout(2.0)
ok = 0
for _ in range(2):
    s.sendto(b"aether", ("10.42.0.2", 7))
    try:
        data, addr = s.recvfrom(64)
        print("udp_echo recv=%s from=%s" % (data.decode("ascii", "replace"), addr[0]))
        if data == b"aether":
            ok += 1
    except Exception as e:
        print("udp_echo fail=%s" % e)
print("udp_echo ok=%d/2" % ok)
s.close()
PY
        ) &
        udp_pid=$!
        AETHER_SERIAL_PROBE_TIMEOUT=15 probe_shell "genet11" "^genet11 ok=1 version=78 rx=.* tx=.* replies=.* kind="
        wait "$udp_pid" || true
        echo "==== host udp 10.42.0.2:7 ===="
        cat "$udp_out" || true
        echo "==== end host udp ===="
        # GENET12: bounded TCP echo on port 7. Host connect must overlap this window.
        tcp_out="${AETHER_TCP_LOG:-/tmp/aether-tcp-genet12.txt}"
        : > "$tcp_out"
        ( sleep 0.5; python3 - <<'PY' > "$tcp_out" 2>&1
import socket
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.settimeout(3.0)
try:
    s.connect(("10.42.0.2", 7))
    s.sendall(b"aether")
    data = s.recv(64)
    print("tcp_echo recv=%s" % data.decode("ascii", "replace"))
    print("tcp_echo ok=%s" % (b"aether" == data))
except Exception as e:
    print("tcp_echo fail=%s" % e)
finally:
    try:
        s.close()
    except Exception:
        pass
PY
        ) &
        tcp_pid=$!
        AETHER_SERIAL_PROBE_TIMEOUT=15 probe_shell "genet12" "^genet12 ok=1 version=79 rx=.* tx=.* replies=.* kind="
        wait "$tcp_pid" || true
        echo "==== host tcp 10.42.0.2:7 ===="
        cat "$tcp_out" || true
        echo "==== end host tcp ===="
        # V80: BSC1 + SPI0 register probe. Read-only. No extra hardware.
        probe_shell "i2c" "^i2c ok=1 version=80 bsc=1 div=.* spi=1"
        # V81: PWM0+PWM1 register probe. Read-only. No extra hardware.
        probe_shell "pwm" "^pwm ok=1 version=81 ctl=.* sta=.* pwm1=1"
        # V82: bounded BSC1 write to vacant 0x7F. Honest NACK. No extra hardware.
        probe_shell "i2c2" "^i2c2 ok=1 version=82 nack=1 addr=0x7f sta="
        # V83: one SPI0 byte. DONE required. loop=1 only with MOSI-MISO jumper.
        probe_shell "spi2" "^spi2 ok=1 version=83 done=1 loop=.* rx="
        # V84: system timer CLO/CHI + four compare slots. Read-only.
        probe_shell "stimer" "^stimer ok=1 version=84 clo=.* chi=.* chans=1"
        # V85: reload config.txt after GENET. match=1 vs V57. No EL0.
        probe_shell "sdload" "^sdload ok=1 version=85 file=config.txt bytes=.* checksum=.* match=1"
        # V86: list FAT32 root after GENET. files>=2 + CONFIG.TXT. No EL0.
        probe_shell "sdls" "^sdls ok=1 version=86 files=.* config=1 other="
        # V87: second FAT32 root file after GENET. Not CONFIG.TXT. No EL0.
        probe_shell "sdfile" "^sdfile ok=1 version=87 name=.* bytes=.* checksum="
        # V88: walk overlays/ after GENET. files>=1. No EL0.
        probe_shell "sdovl" "^sdovl ok=1 version=88 files=.* name="
        # V89: load one overlays/ file after GENET. Size cap 65536. No EL0.
        probe_shell "sdovf" "^sdovf ok=1 version=89 name=.* bytes=.* checksum="
        # V90: load issue.txt by name after GENET. Fail-closed if missing. No EL0.
        probe_shell "sdiss" "^sdiss ok=[01] version=90 present=[01] name=.* bytes=.* checksum="
        # V91: GPIO42 SET/CLR + GPLEV after GENET. No jumper. No EL0.
        probe_shell "gpio2" "^gpio2 ok=1 version=91 pin=42 set=1 clr=1"
        # V92: system timer C1 match after GENET. No GPU C0/C2. No EL0.
        probe_shell "stimer2" "^stimer2 ok=1 version=92 chan=1 match=1"
        # V93: PWM clock enable + CTL poke after GENET. No pin-mux. No EL0.
        probe_shell "pwm2" "^pwm2 ok=1 version=93 clk=1 en=1"
        # V94: GPIO26 PUP_PDN write+readback after GENET. REG1 only. No EL0.
        probe_shell "gpio3" "^gpio3 ok=1 version=94 pin=26 up=1 dn=1"
        # V95: system timer C3 match after GENET. No GPU C0/C2. No EL0.
        probe_shell "stimer3" "^stimer3 ok=1 version=95 chan=3 match=1"
        # V96: mailbox GET_TEMPERATURE after GENET. Millidegrees. No EL0.
        probe_shell "mboxt" "^mboxt ok=1 version=96 temp="
        # V97: mailbox GET_CLOCK_RATE (ARM) after GENET. Hz. No EL0.
        probe_shell "mboxc" "^mboxc ok=1 version=97 clk=3 hz="
        # V98: PM watchdog remaining-tick readback after GENET. Arm/read/disable. No reset. No EL0.
        probe_shell "wdog2" "^wdog2 ok=1 version=98 armed=1 off=1 remain="
        # V99: mailbox GET_VOLTAGE (core) after GENET. Microvolts. No EL0.
        probe_shell "mboxv" "^mboxv ok=1 version=99 id=1 uv="
        # V100: BCM2711 RNG200 word after GENET. FIFO ready. No EL0.
        probe_shell "rng" "^rng ok=1 version=100 ready=1 data="
        # V101: BCM2711 DMA engine memcpy after GENET. Channel 4, 32 bytes. No EL0.
        probe_shell "dma2" "^dma2 ok=1 version=101 chan=4 match=1 bytes=32"
        # V102: SDHCI CMD24 free-cluster write after GENET. FAT/dir unchanged. No EL0.
        probe_shell "sdwr" "^sdwr ok=1 version=102 match=1 bytes=512 clus="
        # V103: FAT32 create/link AETHER.TMP after GENET. Fail-closed if foreign. No EL0.
        probe_shell "sdmk" "^sdmk ok=1 version=103 match=1 created="
        # V104: re-read AETHER.TMP by name after GENET. Read-only. No EL0.
        probe_shell "sdrd" "^sdrd ok=1 version=104 match=1 present=1 name="
        # V105: SDHCI CMD13 SEND_STATUS after GENET. TRAN + READY_FOR_DATA. No EL0.
        probe_shell "sdst" "^sdst ok=1 version=105 state=4 ready=1 rca="
        # V106: SDHCI ACMD51 SEND_SCR after GENET. Structure 0 + 4-bit. No EL0.
        probe_shell "sdscr" "^sdscr ok=1 version=106 spec="
        # V107: SDHCI ACMD13 SD_STATUS after GENET. SD/SDHC type. No EL0.
        probe_shell "sdss" "^sdss ok=1 version=107 type="
        # V108: SDHCI ACMD6 SET_BUS_WIDTH after GENET. 4-bit + MBR. No EL0.
        probe_shell "sdbus" "^sdbus ok=1 version=108 bits="
        # V109: SDHCI CMD18 multi-block read after GENET. 2 blocks + MBR. No EL0.
        probe_shell "sdmb" "^sdmb ok=1 version=109 blocks="
        # V110: SDHCI CMD6 SWITCH_FUNC check after GENET. Group-1 default. No EL0.
        probe_shell "sdsw" "^sdsw ok=1 version=110 grp1="
        # V111: SDHCI CMD25 multi-block write after GENET. 2-block readback. No EL0.
        probe_shell "sdmw" "^sdmw ok=1 version=111 match="
        # V112: SDHCI CMD23 SET_BLOCK_COUNT after GENET. 2 blocks + MBR. No EL0.
        probe_shell "sdbc" "^sdbc ok=1 version=112 count="
        # V113: FAT32 FSInfo sector after GENET. Lead+struct. No EL0.
        probe_shell "sdfi" "^sdfi ok=1 version=113 lead="
        # V114: FAT32 backup boot sector after GENET. BPB match. No EL0.
        probe_shell "sdfb" "^sdfb ok=1 version=114 match="
        # V115: FAT32 FAT-mirror compare after GENET. fats>=2. No EL0.
        probe_shell "sdfm" "^sdfm ok=1 version=115 match="
        # V116: FAT32 scratch unlink after GENET. deleted+absent. No EL0.
        probe_shell "sdrm" "^sdrm ok=1 version=116 deleted="
        # V117: originate ARP + ICMP echo to the TFTP host. No EL0.
        probe_shell "genet13" "^genet13 ok=1 version=117 arp="
        # V118: originate UDP echo to the TFTP host:41240. No EL0.
        probe_shell "genet14" "^genet14 ok=1 version=118 udp="
        # V119: originate TCP echo to the TFTP host:41241. No EL0.
        probe_shell "genet15" "^genet15 ok=1 version=119 tcp="
        # V120: originate TFTP RRQ of aether/v120.bin. No EL0.
        probe_shell "genet16" "^genet16 ok=1 version=120 tftp="
        # V121: originate mDNS A query for aether-v121.local. No EL0.
        probe_shell "genet17" "^genet17 ok=1 version=121 mdns="
        # V122: originate HTTP GET /aether/v122.txt. No EL0.
        probe_shell "genet18" "^genet18 ok=1 version=122 http="
        # probe shell: req-status
        probe_shell "req id=25 cmd=status" "^resp id=25 ok=1 cmd=status end"
        # probe shell: canceltest
        probe_shell "canceltest" "^canceltest ok=1 .*completed=1"
        # probe shell: taskcheck
        probe_shell "taskcheck" "^taskcheck ok=1 .*spawns="
        # probe shell: channeltest
        probe_shell "channeltest" "^channeltest ok=1 .*received=1"
        # probe shell: mmu
        probe_shell "mmu" "^mmu ok=1 .*regions=5 .*block_size=0x40000000"
        # probe shell: vmm (printVMM schema: ok= version=50 pt= selftest=)
        probe_shell "vmm" "^vmm ok=1 version=50 pt=.* selftest=.*"
        # probe shell: asplit
        probe_shell "asplit" "^asplit ok=1 version=46"
        # probe shell: el0
        probe_shell "el0" "^el0 ok=1 version=47"
        # probe shell: syscall (printSyscall schema: ok= version=48 abi= table=)
        probe_shell "syscall" "^syscall ok=1 version=48 .*"
        # probe shell: uaccess
        probe_shell "uaccess" "^uaccess ok=1 version=49"
        # probe shell: usermode (boot grep: fault_contained=1 when ok=1)
        probe_shell "usermode" "^usermode ok=1 version=50 fault_contained=1"
        # probe shell: process
        probe_shell "process" "^process ok=1 version=51 .*"
        # probe shell: loader
        probe_shell "loader" "^loader ok=1 version=52"
        # probe shell: multiprocess
        probe_shell "multiprocess" "^multiprocess ok=1 version=53"
        # probe shell: sdhci
        probe_shell "sdhci" "^sdhci ok=1 version=54 .*"
        # probe shell: card
        probe_shell "card" "^card ok=1 version=55 .*"
        # probe shell: block
        probe_shell "block" "^block ok=1 version=56 .*"
        # probe shell: fat32 (printFat32: file=config.txt is literal)
        probe_shell "fat32" "^fat32 ok=1 version=57 file=config.txt .*"
        # probe shell: mailbox
        probe_shell "mailbox" "^mailbox ok=1 version=58 .*"
        # probe shell: framebuf
        probe_shell "framebuf" "^framebuf ok=1 version=59 .*"
        # probe shell: console (printConsole: display=0 is literal)
        # Live counter: boot prints counter=0 (IRQs still off); this shell
        # line is the second sample. A second back-to-back `console` probe
        # consistently missed its reply (host saw timeout; kernel printed
        # ~3s later). Do not add a duplicate console probe.
        probe_shell "console" "^console ok=1 version=60 .* display=0"
        # probe shell: pcie
        probe_shell "pcie" "^pcie ok=1 version=61 .*"
        # probe shell: vl805
        probe_shell "vl805" "^vl805 ok=1 version=62 .*"
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
        # probe shell: handlecheck
        probe_shell "handlecheck" "^handlecheck ok=1 .*handle_selftest=1 .*cap_selftest=1"
        # probe shell: capcheck
        probe_shell "capcheck" "^capcheck ok=1 .*denied=1 .*stale=1"
        # probe shell: events
        # events: the 64-slot ring evicts by design once a v66 boot emits >64
        # events (sequence=66 observed) — single-digit loss is structural, not a
        # failure; the kernel's own selftest stays authoritative.
        probe_shell "events" "^events count=.* lost=[0-9] .*selftest=1"
      fi
      echo "--- TFTP delta ---"
      printf '%s\n' "$dns_delta" | tail -n 80
      echo "--- serial delta ---"
      printf '%s\n' "$serial_delta" | tail -n 120
      exit 0
    fi

    if printf '%s' "$serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
      && printf '%s' "$serial_delta" | grep -qa "shell ready commands=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v65: USB device enumeration" \
      && ! printf '%s' "$serial_delta" | grep -qa "runtime v66: HID boot-protocol keyboard"; then
      echo "netboot attempt ${attempt}/${RETRIES} stale pre-V66 SD fallback image detected"
      echo "TFTP kernel fetch was not verified; staged network image is not proven."
      print_tftp_diagnostics "$dns_delta"
      last_dns_delta="$dns_delta"
      last_serial_delta="$serial_delta"
      sd_fallback_seen=1
      break
    fi
    if printf '%s' "$serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
      && printf '%s' "$serial_delta" | grep -qa "shell ready commands=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v64: xHCI controller init" \
      && ! printf '%s' "$serial_delta" | grep -qa "runtime v65: USB device enumeration"; then
      echo "netboot attempt ${attempt}/${RETRIES} stale pre-V65 SD fallback image detected"
      echo "TFTP kernel fetch was not verified; staged network image is not proven."
      print_tftp_diagnostics "$dns_delta"
      last_dns_delta="$dns_delta"
      last_serial_delta="$serial_delta"
      sd_fallback_seen=1
      break
    fi
    if printf '%s' "$serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
      && printf '%s' "$serial_delta" | grep -qa "shell ready commands=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v63: xHCI capability register probe" \
      && ! printf '%s' "$serial_delta" | grep -qa "runtime v64: xHCI controller init"; then
      echo "netboot attempt ${attempt}/${RETRIES} stale pre-V64 SD fallback image detected"
      echo "TFTP kernel fetch was not verified; staged network image is not proven."
      print_tftp_diagnostics "$dns_delta"
      last_dns_delta="$dns_delta"
      last_serial_delta="$serial_delta"
      sd_fallback_seen=1
      break
    fi
    if printf '%s' "$serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
      && printf '%s' "$serial_delta" | grep -qa "shell ready commands=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v62: VL805 USB 3.0 xHCI config-space probe" \
      && ! printf '%s' "$serial_delta" | grep -qa "runtime v63: xHCI capability register probe"; then
      echo "netboot attempt ${attempt}/${RETRIES} stale pre-V63 SD fallback image detected"
      echo "TFTP kernel fetch was not verified; staged network image is not proven."
      print_tftp_diagnostics "$dns_delta"
      last_dns_delta="$dns_delta"
      last_serial_delta="$serial_delta"
      sd_fallback_seen=1
      break
    fi
    if printf '%s' "$serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
      && printf '%s' "$serial_delta" | grep -qa "shell ready commands=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v61: BCM2711 PCIe RC bring-up" \
      && ! printf '%s' "$serial_delta" | grep -qa "runtime v62: VL805 USB 3.0 xHCI config-space probe"; then
      echo "netboot attempt ${attempt}/${RETRIES} stale pre-V62 SD fallback image detected"
      echo "TFTP kernel fetch was not verified; staged network image is not proven."
      print_tftp_diagnostics "$dns_delta"
      last_dns_delta="$dns_delta"
      last_serial_delta="$serial_delta"
      sd_fallback_seen=1
      break
    fi
    if printf '%s' "$serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
      && printf '%s' "$serial_delta" | grep -qa "shell ready commands=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v60: text console (8x8 font blit + readback proof)" \
      && ! printf '%s' "$serial_delta" | grep -qa "runtime v61: BCM2711 PCIe RC bring-up"; then
      echo "netboot attempt ${attempt}/${RETRIES} stale pre-V61 SD fallback image detected"
      echo "TFTP kernel fetch was not verified; staged network image is not proven."
      print_tftp_diagnostics "$dns_delta"
      last_dns_delta="$dns_delta"
      last_serial_delta="$serial_delta"
      sd_fallback_seen=1
      break
    fi
    if printf '%s' "$serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
      && printf '%s' "$serial_delta" | grep -qa "shell ready commands=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v59: VideoCore framebuffer allocation (width/height/pitch/addr)" \
      && ! printf '%s' "$serial_delta" | grep -qa "runtime v60: text console (8x8 font blit + readback proof)"; then
      echo "netboot attempt ${attempt}/${RETRIES} stale pre-V60 SD fallback image detected"
      echo "TFTP kernel fetch was not verified; staged network image is not proven."
      print_tftp_diagnostics "$dns_delta"
      last_dns_delta="$dns_delta"
      last_serial_delta="$serial_delta"
      sd_fallback_seen=1
      break
    fi
    if printf '%s' "$serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
      && printf '%s' "$serial_delta" | grep -qa "shell ready commands=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v58: VideoCore mailbox property interface (firmware revision)" \
      && ! printf '%s' "$serial_delta" | grep -qa "runtime v59: VideoCore framebuffer allocation (width/height/pitch/addr)"; then
      echo "netboot attempt ${attempt}/${RETRIES} stale pre-V59 SD fallback image detected"
      echo "TFTP kernel fetch was not verified; staged network image is not proven."
      print_tftp_diagnostics "$dns_delta"
      last_dns_delta="$dns_delta"
      last_serial_delta="$serial_delta"
      sd_fallback_seen=1
      break
    fi
    if printf '%s' "$serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
      && printf '%s' "$serial_delta" | grep -qa "shell ready commands=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v57: FAT32 file read (config.txt bytes + checksum)" \
      && ! printf '%s' "$serial_delta" | grep -qa "runtime v58: VideoCore mailbox property interface (firmware revision)"; then
      echo "netboot attempt ${attempt}/${RETRIES} stale pre-V58 SD fallback image detected"
      echo "TFTP kernel fetch was not verified; staged network image is not proven."
      print_tftp_diagnostics "$dns_delta"
      last_dns_delta="$dns_delta"
      last_serial_delta="$serial_delta"
      sd_fallback_seen=1
      break
    fi
    if printf '%s' "$serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
      && printf '%s' "$serial_delta" | grep -qa "shell ready commands=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v56: single block read CMD17 + MBR 0x55AA verification" \
      && ! printf '%s' "$serial_delta" | grep -qa "runtime v57: FAT32 file read (config.txt bytes + checksum)"; then
      echo "netboot attempt ${attempt}/${RETRIES} stale pre-V57 SD fallback image detected"
      echo "TFTP kernel fetch was not verified; staged network image is not proven."
      print_tftp_diagnostics "$dns_delta"
      last_dns_delta="$dns_delta"
      last_serial_delta="$serial_delta"
      sd_fallback_seen=1
      break
    fi
    if printf '%s' "$serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
      && printf '%s' "$serial_delta" | grep -qa "shell ready commands=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v55: SD card identification (CMD0/CMD8/ACMD41/CMD2/CMD3)" \
      && ! printf '%s' "$serial_delta" | grep -qa "runtime v56: single block read CMD17 + MBR 0x55AA verification"; then
      echo "netboot attempt ${attempt}/${RETRIES} stale pre-V56 SD fallback image detected"
      echo "TFTP kernel fetch was not verified; staged network image is not proven."
      print_tftp_diagnostics "$dns_delta"
      last_dns_delta="$dns_delta"
      last_serial_delta="$serial_delta"
      sd_fallback_seen=1
      break
    fi
    if printf '%s' "$serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
      && printf '%s' "$serial_delta" | grep -qa "shell ready commands=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v54: BCM2711 EMMC2/SDHCI register probe" \
      && ! printf '%s' "$serial_delta" | grep -qa "runtime v55: SD card identification (CMD0/CMD8/ACMD41/CMD2/CMD3)"; then
      echo "netboot attempt ${attempt}/${RETRIES} stale pre-V55 SD fallback image detected"
      echo "TFTP kernel fetch was not verified; staged network image is not proven."
      print_tftp_diagnostics "$dns_delta"
      last_dns_delta="$dns_delta"
      last_serial_delta="$serial_delta"
      sd_fallback_seen=1
      break
    fi
    if printf '%s' "$serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
      && printf '%s' "$serial_delta" | grep -qa "shell ready commands=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v53: multi-process user execution" \
      && ! printf '%s' "$serial_delta" | grep -qa "runtime v54: BCM2711 EMMC2/SDHCI register probe"; then
      echo "netboot attempt ${attempt}/${RETRIES} stale pre-V54 SD fallback image detected"
      echo "TFTP kernel fetch was not verified; staged network image is not proven."
      print_tftp_diagnostics "$dns_delta"
      last_dns_delta="$dns_delta"
      last_serial_delta="$serial_delta"
      sd_fallback_seen=1
      break
    fi
    if printf '%s' "$serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
      && printf '%s' "$serial_delta" | grep -qa "shell ready commands=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v52: user binary loader" \
      && ! printf '%s' "$serial_delta" | grep -qa "runtime v53: multi-process user execution"; then
      echo "netboot attempt ${attempt}/${RETRIES} stale pre-V53 SD fallback image detected"
      echo "TFTP kernel fetch was not verified; staged network image is not proven."
      print_tftp_diagnostics "$dns_delta"
      last_dns_delta="$dns_delta"
      last_serial_delta="$serial_delta"
      sd_fallback_seen=1
      break
    fi
    if printf '%s' "$serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
      && printf '%s' "$serial_delta" | grep -qa "shell ready commands=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v51: process abstraction" \
      && ! printf '%s' "$serial_delta" | grep -qa "runtime v52: user binary loader"; then
      echo "netboot attempt ${attempt}/${RETRIES} stale pre-V52 SD fallback image detected"
      echo "TFTP kernel fetch was not verified; staged network image is not proven."
      print_tftp_diagnostics "$dns_delta"
      last_dns_delta="$dns_delta"
      last_serial_delta="$serial_delta"
      sd_fallback_seen=1
      break
    fi
    if printf '%s' "$serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
      && printf '%s' "$serial_delta" | grep -qa "shell ready commands=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v50: EPIC A capstone" \
      && ! printf '%s' "$serial_delta" | grep -qa "runtime v51: process abstraction"; then
      echo "netboot attempt ${attempt}/${RETRIES} stale pre-V51 SD fallback image detected"
      echo "TFTP kernel fetch was not verified; staged network image is not proven."
      print_tftp_diagnostics "$dns_delta"
      last_dns_delta="$dns_delta"
      last_serial_delta="$serial_delta"
      sd_fallback_seen=1
      break
    fi
    if printf '%s' "$serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
      && printf '%s' "$serial_delta" | grep -qa "shell ready commands=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v49: fault-safe copy_from_user / copy_to_user" \
      && ! printf '%s' "$serial_delta" | grep -qa "runtime v50: EPIC A capstone"; then
      echo "netboot attempt ${attempt}/${RETRIES} stale pre-V50 SD fallback image detected"
      echo "TFTP kernel fetch was not verified; staged network image is not proven."
      print_tftp_diagnostics "$dns_delta"
      last_dns_delta="$dns_delta"
      last_serial_delta="$serial_delta"
      sd_fallback_seen=1
      break
    fi
    if printf '%s' "$serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
      && printf '%s' "$serial_delta" | grep -qa "shell ready commands=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v48: syscall ABI via SVC from EL0" \
      && ! printf '%s' "$serial_delta" | grep -qa "runtime v49: fault-safe copy_from_user / copy_to_user"; then
      echo "netboot attempt ${attempt}/${RETRIES} stale pre-V49 SD fallback image detected"
      echo "TFTP kernel fetch was not verified; staged network image is not proven."
      print_tftp_diagnostics "$dns_delta"
      last_dns_delta="$dns_delta"
      last_serial_delta="$serial_delta"
      sd_fallback_seen=1
      break
    fi
    if printf '%s' "$serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
      && printf '%s' "$serial_delta" | grep -qa "shell ready commands=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v47: EL0 entry/exit and context save/restore" \
      && ! printf '%s' "$serial_delta" | grep -qa "runtime v48: syscall ABI via SVC from EL0"; then
      echo "netboot attempt ${attempt}/${RETRIES} stale pre-V48 SD fallback image detected"
      echo "TFTP kernel fetch was not verified; staged network image is not proven."
      print_tftp_diagnostics "$dns_delta"
      last_dns_delta="$dns_delta"
      last_serial_delta="$serial_delta"
      sd_fallback_seen=1
      break
    fi
    if printf '%s' "$serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
      && printf '%s' "$serial_delta" | grep -qa "shell ready commands=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v46: kernel/user address-space split (isolated page tables)" \
      && ! printf '%s' "$serial_delta" | grep -qa "runtime v47: EL0 entry/exit and context save/restore"; then
      echo "netboot attempt ${attempt}/${RETRIES} stale pre-V47 SD fallback image detected"
      echo "TFTP kernel fetch was not verified; staged network image is not proven."
      print_tftp_diagnostics "$dns_delta"
      last_dns_delta="$dns_delta"
      last_serial_delta="$serial_delta"
      sd_fallback_seen=1
      break
    fi
    if printf '%s' "$serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
      && printf '%s' "$serial_delta" | grep -qa "shell ready commands=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v45: dynamic virtual memory (page tables + TLB)" \
      && ! printf '%s' "$serial_delta" | grep -qa "runtime v46: kernel/user address-space split (isolated page tables)"; then
      echo "netboot attempt ${attempt}/${RETRIES} stale pre-V46 SD fallback image detected"
      echo "TFTP kernel fetch was not verified; staged network image is not proven."
      print_tftp_diagnostics "$dns_delta"
      last_dns_delta="$dns_delta"
      last_serial_delta="$serial_delta"
      sd_fallback_seen=1
      break
    fi

    # v31–v44 stale-SD classifiers below grep unconditional enable banners only to
    # fingerprint banner-only SD fallback images (final_exit=3). Boot proof for
    # scheduler versions requires schedselftest ok=1 version=N (main gate :234-247).
    if printf '%s' "$serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
      && printf '%s' "$serial_delta" | grep -qa "shell ready commands=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v44: bounded smp concurrency soak" \
      && ! printf '%s' "$serial_delta" | grep -qa "runtime v45: dynamic virtual memory (page tables + TLB)"; then
      echo "netboot attempt ${attempt}/${RETRIES} stale pre-V45 SD fallback image detected"
      echo "TFTP kernel fetch was not verified; staged network image is not proven."
      print_tftp_diagnostics "$dns_delta"
      last_dns_delta="$dns_delta"
      last_serial_delta="$serial_delta"
      sd_fallback_seen=1
      break
    fi
    if printf '%s' "$serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
      && printf '%s' "$serial_delta" | grep -qa "shell ready commands=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v43: secondary scheduler priority preemption" \
      && ! printf '%s' "$serial_delta" | grep -qa "runtime v44: bounded smp concurrency soak"; then
      echo "netboot attempt ${attempt}/${RETRIES} stale pre-V44 SD fallback image detected"
      echo "TFTP kernel fetch was not verified; staged network image is not proven."
      print_tftp_diagnostics "$dns_delta"
      last_dns_delta="$dns_delta"
      last_serial_delta="$serial_delta"
      sd_fallback_seen=1
      break
    fi
    if printf '%s' "$serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
      && printf '%s' "$serial_delta" | grep -qa "shell ready commands=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v42: secondary scheduler load balancing" \
      && ! printf '%s' "$serial_delta" | grep -qa "runtime v43: secondary scheduler priority preemption"; then
      echo "netboot attempt ${attempt}/${RETRIES} stale pre-V43 SD fallback image detected"
      echo "TFTP kernel fetch was not verified; staged network image is not proven."
      print_tftp_diagnostics "$dns_delta"
      last_dns_delta="$dns_delta"
      last_serial_delta="$serial_delta"
      sd_fallback_seen=1
      break
    fi
    if printf '%s' "$serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
      && printf '%s' "$serial_delta" | grep -qa "shell ready commands=" \
      && printf '%s' "$serial_delta" | grep -qa "runtime v41: secondary scheduler work stealing" \
      && ! printf '%s' "$serial_delta" | grep -qa "runtime v42: secondary scheduler load balancing"; then
      echo "netboot attempt ${attempt}/${RETRIES} stale pre-V42 SD fallback image detected"
      echo "TFTP kernel fetch was not verified; staged network image is not proven."
      print_tftp_diagnostics "$dns_delta"
      last_dns_delta="$dns_delta"
      last_serial_delta="$serial_delta"
      sd_fallback_seen=1
      break
    fi
    if printf '%s' "$serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
      && printf '%s' "$serial_delta" | grep -qa "shell ready commands=" \
      && ! printf '%s' "$serial_delta" | grep -qa "runtime v41: secondary scheduler work stealing"; then
      echo "netboot attempt ${attempt}/${RETRIES} stale pre-V41 SD fallback image detected"
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
        if printf '%s' "$last_serial_delta" | grep -qa "runtime v65: USB device enumeration" \
          && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v66: HID boot-protocol keyboard"; then
          echo "retrying after stale pre-V66 SD fallback..."
        elif printf '%s' "$last_serial_delta" | grep -qa "runtime v64: xHCI controller init" \
          && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v65: USB device enumeration"; then
          echo "retrying after stale pre-V65 SD fallback..."
        elif printf '%s' "$last_serial_delta" | grep -qa "runtime v63: xHCI capability register probe" \
          && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v64: xHCI controller init"; then
          echo "retrying after stale pre-V64 SD fallback..."
        elif printf '%s' "$last_serial_delta" | grep -qa "runtime v62: VL805 USB 3.0 xHCI config-space probe" \
          && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v63: xHCI capability register probe"; then
          echo "retrying after stale pre-V63 SD fallback..."
        elif printf '%s' "$last_serial_delta" | grep -qa "runtime v61: BCM2711 PCIe RC bring-up" \
          && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v62: VL805 USB 3.0 xHCI config-space probe"; then
          echo "retrying after stale pre-V62 SD fallback..."
        elif printf '%s' "$last_serial_delta" | grep -qa "runtime v60: text console (8x8 font blit + readback proof)" \
          && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v61: BCM2711 PCIe RC bring-up"; then
          echo "retrying after stale pre-V61 SD fallback..."
        elif printf '%s' "$last_serial_delta" | grep -qa "runtime v59: VideoCore framebuffer allocation (width/height/pitch/addr)" \
          && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v60: text console (8x8 font blit + readback proof)"; then
          echo "retrying after stale pre-V60 SD fallback..."
        elif printf '%s' "$last_serial_delta" | grep -qa "runtime v58: VideoCore mailbox property interface (firmware revision)" \
          && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v59: VideoCore framebuffer allocation (width/height/pitch/addr)"; then
          echo "retrying after stale pre-V59 SD fallback..."
        elif printf '%s' "$last_serial_delta" | grep -qa "runtime v57: FAT32 file read (config.txt bytes + checksum)" \
          && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v58: VideoCore mailbox property interface (firmware revision)"; then
          echo "retrying after stale pre-V58 SD fallback..."
        elif printf '%s' "$last_serial_delta" | grep -qa "runtime v56: single block read CMD17 + MBR 0x55AA verification" \
          && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v57: FAT32 file read (config.txt bytes + checksum)"; then
          echo "retrying after stale pre-V57 SD fallback..."
        elif printf '%s' "$last_serial_delta" | grep -qa "runtime v55: SD card identification (CMD0/CMD8/ACMD41/CMD2/CMD3)" \
          && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v56: single block read CMD17 + MBR 0x55AA verification"; then
          echo "retrying after stale pre-V56 SD fallback..."
        elif printf '%s' "$last_serial_delta" | grep -qa "runtime v54: BCM2711 EMMC2/SDHCI register probe" \
          && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v55: SD card identification (CMD0/CMD8/ACMD41/CMD2/CMD3)"; then
          echo "retrying after stale pre-V55 SD fallback..."
        elif printf '%s' "$last_serial_delta" | grep -qa "runtime v53: multi-process user execution" \
          && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v54: BCM2711 EMMC2/SDHCI register probe"; then
          echo "retrying after stale pre-V54 SD fallback..."
        elif printf '%s' "$last_serial_delta" | grep -qa "runtime v52: user binary loader" \
          && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v53: multi-process user execution"; then
          echo "retrying after stale pre-V53 SD fallback..."
        elif printf '%s' "$last_serial_delta" | grep -qa "runtime v51: process abstraction" \
          && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v52: user binary loader"; then
          echo "retrying after stale pre-V52 SD fallback..."
        elif printf '%s' "$last_serial_delta" | grep -qa "runtime v50: EPIC A capstone" \
          && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v51: process abstraction"; then
          echo "retrying after stale pre-V51 SD fallback..."
        elif printf '%s' "$last_serial_delta" | grep -qa "runtime v49: fault-safe copy_from_user / copy_to_user" \
          && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v50: EPIC A capstone"; then
          echo "retrying after stale pre-V50 SD fallback..."
        elif printf '%s' "$last_serial_delta" | grep -qa "runtime v48: syscall ABI via SVC from EL0" \
          && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v49: fault-safe copy_from_user / copy_to_user"; then
          echo "retrying after stale pre-V49 SD fallback..."
        elif printf '%s' "$last_serial_delta" | grep -qa "runtime v47: EL0 entry/exit and context save/restore" \
          && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v48: syscall ABI via SVC from EL0"; then
          echo "retrying after stale pre-V48 SD fallback..."
        elif printf '%s' "$last_serial_delta" | grep -qa "runtime v46: kernel/user address-space split (isolated page tables)" \
          && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v47: EL0 entry/exit and context save/restore"; then
          echo "retrying after stale pre-V47 SD fallback..."
        elif printf '%s' "$last_serial_delta" | grep -qa "runtime v45: dynamic virtual memory (page tables + TLB)" \
          && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v46: kernel/user address-space split (isolated page tables)"; then
          echo "retrying after stale pre-V46 SD fallback..."
        elif printf '%s' "$last_serial_delta" | grep -qa "runtime v44: bounded smp concurrency soak" \
          && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v45: dynamic virtual memory (page tables + TLB)"; then
          echo "retrying after stale pre-V45 SD fallback..."
        elif printf '%s' "$last_serial_delta" | grep -qa "runtime v43: secondary scheduler priority preemption" \
          && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v44: bounded smp concurrency soak"; then
          echo "retrying after stale pre-V44 SD fallback..."
        elif printf '%s' "$last_serial_delta" | grep -qa "runtime v42: secondary scheduler load balancing" \
          && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v43: secondary scheduler priority preemption"; then
          echo "retrying after stale pre-V43 SD fallback..."
        elif printf '%s' "$last_serial_delta" | grep -qa "runtime v41: secondary scheduler work stealing" \
          && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v42: secondary scheduler load balancing"; then
          echo "retrying after stale pre-V42 SD fallback..."
        else
          echo "retrying after stale pre-V41 SD fallback..."
        fi
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
if printf '%s' "$last_serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
  && printf '%s' "$last_serial_delta" | grep -qa "runtime v65: USB device enumeration" \
  && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v66: HID boot-protocol keyboard"; then
  echo "final result: stale pre-V66 SD fallback image booted, but staged network image is not proven."
  final_exit=3
elif printf '%s' "$last_serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
  && printf '%s' "$last_serial_delta" | grep -qa "runtime v64: xHCI controller init" \
  && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v65: USB device enumeration"; then
  echo "final result: stale pre-V65 SD fallback image booted, but staged network image is not proven."
  final_exit=3
elif printf '%s' "$last_serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
  && printf '%s' "$last_serial_delta" | grep -qa "runtime v63: xHCI capability register probe" \
  && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v64: xHCI controller init"; then
  echo "final result: stale pre-V64 SD fallback image booted, but staged network image is not proven."
  final_exit=3
elif printf '%s' "$last_serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
  && printf '%s' "$last_serial_delta" | grep -qa "runtime v62: VL805 USB 3.0 xHCI config-space probe" \
  && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v63: xHCI capability register probe"; then
  echo "final result: stale pre-V63 SD fallback image booted, but staged network image is not proven."
  final_exit=3
elif printf '%s' "$last_serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
  && printf '%s' "$last_serial_delta" | grep -qa "runtime v61: BCM2711 PCIe RC bring-up" \
  && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v62: VL805 USB 3.0 xHCI config-space probe"; then
  echo "final result: stale pre-V62 SD fallback image booted, but staged network image is not proven."
  final_exit=3
elif printf '%s' "$last_serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
  && printf '%s' "$last_serial_delta" | grep -qa "runtime v60: text console (8x8 font blit + readback proof)" \
  && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v61: BCM2711 PCIe RC bring-up"; then
  echo "final result: stale pre-V61 SD fallback image booted, but staged network image is not proven."
  final_exit=3
elif printf '%s' "$last_serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
  && printf '%s' "$last_serial_delta" | grep -qa "runtime v59: VideoCore framebuffer allocation (width/height/pitch/addr)" \
  && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v60: text console (8x8 font blit + readback proof)"; then
  echo "final result: stale pre-V60 SD fallback image booted, but staged network image is not proven."
  final_exit=3
elif printf '%s' "$last_serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
  && printf '%s' "$last_serial_delta" | grep -qa "runtime v58: VideoCore mailbox property interface (firmware revision)" \
  && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v59: VideoCore framebuffer allocation (width/height/pitch/addr)"; then
  echo "final result: stale pre-V59 SD fallback image booted, but staged network image is not proven."
  final_exit=3
elif printf '%s' "$last_serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
  && printf '%s' "$last_serial_delta" | grep -qa "runtime v57: FAT32 file read (config.txt bytes + checksum)" \
  && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v58: VideoCore mailbox property interface (firmware revision)"; then
  echo "final result: stale pre-V58 SD fallback image booted, but staged network image is not proven."
  final_exit=3
elif printf '%s' "$last_serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
  && printf '%s' "$last_serial_delta" | grep -qa "runtime v56: single block read CMD17 + MBR 0x55AA verification" \
  && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v57: FAT32 file read (config.txt bytes + checksum)"; then
  echo "final result: stale pre-V57 SD fallback image booted, but staged network image is not proven."
  final_exit=3
elif printf '%s' "$last_serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
  && printf '%s' "$last_serial_delta" | grep -qa "runtime v55: SD card identification (CMD0/CMD8/ACMD41/CMD2/CMD3)" \
  && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v56: single block read CMD17 + MBR 0x55AA verification"; then
  echo "final result: stale pre-V56 SD fallback image booted, but staged network image is not proven."
  final_exit=3
elif printf '%s' "$last_serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
  && printf '%s' "$last_serial_delta" | grep -qa "runtime v54: BCM2711 EMMC2/SDHCI register probe" \
  && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v55: SD card identification (CMD0/CMD8/ACMD41/CMD2/CMD3)"; then
  echo "final result: stale pre-V55 SD fallback image booted, but staged network image is not proven."
  final_exit=3
elif printf '%s' "$last_serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
  && printf '%s' "$last_serial_delta" | grep -qa "runtime v53: multi-process user execution" \
  && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v54: BCM2711 EMMC2/SDHCI register probe"; then
  echo "final result: stale pre-V54 SD fallback image booted, but staged network image is not proven."
  final_exit=3
elif printf '%s' "$last_serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
  && printf '%s' "$last_serial_delta" | grep -qa "runtime v52: user binary loader" \
  && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v53: multi-process user execution"; then
  echo "final result: stale pre-V53 SD fallback image booted, but staged network image is not proven."
  final_exit=3
elif printf '%s' "$last_serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
  && printf '%s' "$last_serial_delta" | grep -qa "runtime v51: process abstraction" \
  && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v52: user binary loader"; then
  echo "final result: stale pre-V52 SD fallback image booted, but staged network image is not proven."
  final_exit=3
elif printf '%s' "$last_serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
  && printf '%s' "$last_serial_delta" | grep -qa "runtime v50: EPIC A capstone" \
  && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v51: process abstraction"; then
  echo "final result: stale pre-V51 SD fallback image booted, but staged network image is not proven."
  final_exit=3
elif printf '%s' "$last_serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
  && printf '%s' "$last_serial_delta" | grep -qa "runtime v49: fault-safe copy_from_user / copy_to_user" \
  && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v50: EPIC A capstone"; then
  echo "final result: stale pre-V50 SD fallback image booted, but staged network image is not proven."
  final_exit=3
elif printf '%s' "$last_serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
  && printf '%s' "$last_serial_delta" | grep -qa "runtime v48: syscall ABI via SVC from EL0" \
  && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v49: fault-safe copy_from_user / copy_to_user"; then
  echo "final result: stale pre-V49 SD fallback image booted, but staged network image is not proven."
  final_exit=3
elif printf '%s' "$last_serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
  && printf '%s' "$last_serial_delta" | grep -qa "runtime v47: EL0 entry/exit and context save/restore" \
  && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v48: syscall ABI via SVC from EL0"; then
  echo "final result: stale pre-V48 SD fallback image booted, but staged network image is not proven."
  final_exit=3
elif printf '%s' "$last_serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
  && printf '%s' "$last_serial_delta" | grep -qa "runtime v46: kernel/user address-space split (isolated page tables)" \
  && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v47: EL0 entry/exit and context save/restore"; then
  echo "final result: stale pre-V47 SD fallback image booted, but staged network image is not proven."
  final_exit=3
elif printf '%s' "$last_serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
  && printf '%s' "$last_serial_delta" | grep -qa "runtime v45: dynamic virtual memory (page tables + TLB)" \
  && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v46: kernel/user address-space split (isolated page tables)"; then
  echo "final result: stale pre-V46 SD fallback image booted, but staged network image is not proven."
  final_exit=3
elif printf '%s' "$last_serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
  && printf '%s' "$last_serial_delta" | grep -qa "runtime v44: bounded smp concurrency soak" \
  && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v45: dynamic virtual memory (page tables + TLB)"; then
  echo "final result: stale pre-V45 SD fallback image booted, but staged network image is not proven."
  final_exit=3
elif printf '%s' "$last_serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
  && printf '%s' "$last_serial_delta" | grep -qa "runtime v43: secondary scheduler priority preemption" \
  && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v44: bounded smp concurrency soak"; then
  echo "final result: stale pre-V44 SD fallback image booted, but staged network image is not proven."
  final_exit=3
elif printf '%s' "$last_serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
  && printf '%s' "$last_serial_delta" | grep -qa "runtime v42: secondary scheduler load balancing" \
  && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v43: secondary scheduler priority preemption"; then
  echo "final result: stale pre-V43 SD fallback image booted, but staged network image is not proven."
  final_exit=3
elif printf '%s' "$last_serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
  && printf '%s' "$last_serial_delta" | grep -qa "runtime v41: secondary scheduler work stealing" \
  && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v42: secondary scheduler load balancing"; then
  echo "final result: stale pre-V42 SD fallback image booted, but staged network image is not proven."
  final_exit=3
elif printf '%s' "$last_serial_delta" | grep -qa "runtime v4: irq-backed uart shell" \
  && ! printf '%s' "$last_serial_delta" | grep -qa "runtime v41: secondary scheduler work stealing"; then
  echo "final result: stale pre-V41 SD fallback image booted, but staged network image is not proven."
  final_exit=3
else
  final_exit=1
fi
echo "--- TFTP delta from final attempt ---"
printf '%s\n' "$last_dns_delta" | tail -n 80
echo "--- serial delta from final attempt ---"
printf '%s\n' "$last_serial_delta" | tail -n 120
exit "$final_exit"
