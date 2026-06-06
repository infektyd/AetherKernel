#!/usr/bin/env bash
#===----------------------------------------------------------------------===#
# Serve the AetherKernel TFTP root for Pi 4 EEPROM static-IP netboot.
#
#   usage: ./serve-netboot.sh [interface] [tftp-root]
#
# Defaults:
#   interface  : $AETHER_NETBOOT_INTERFACE or en0
#   tftp-root  : $AETHER_TFTP_ROOT or ~/aether-tftp
#   prefix dir : $AETHER_TFTP_PREFIX or aether
#
# This is intentionally TFTP-only. The EEPROM config supplies CLIENT_IP/TFTP_IP.
# The default provider is the repo-owned aether_tftp.py because this Pi 4 bench
# needs single-port duplicate-RRQ handling after the GPU firmware emits
# "Early terminate" and retries from a new UDP source port.
# Set AETHER_NETBOOT_REPLACE=1 to let this script stop an existing AetherKernel
# TFTP provider before binding UDP/69. This keeps the sudoers rule scoped to this
# one bench script instead of requiring passwordless kill/pkill.
# Set AETHER_TFTP_PROVIDER=dnsmasq to use Homebrew dnsmasq as an explicit fallback.
#===----------------------------------------------------------------------===#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IFACE="${1:-${AETHER_NETBOOT_INTERFACE:-en0}}"
TFTP_ROOT="${2:-${AETHER_TFTP_ROOT:-$HOME/aether-tftp}}"
PREFIX="${AETHER_TFTP_PREFIX:-aether}"
PREFIX="${PREFIX#/}"
PREFIX="${PREFIX%/}"
SERVER_IP="${AETHER_NETBOOT_SERVER_IP:-10.42.0.1}"
NO_BLOCKSIZE="${AETHER_TFTP_NO_BLOCKSIZE:-0}"
SINGLE_PORT="${AETHER_TFTP_SINGLE_PORT:-1}"
TFTP_MTU="${AETHER_TFTP_MTU:-}"
REPLACE_EXISTING="${AETHER_NETBOOT_REPLACE:-0}"
PROVIDER="${AETHER_TFTP_PROVIDER:-aether}"

usage() {
  sed -n '2,17p' "$0" | sed 's/^# \{0,1\}//'
}

die() {
  echo "serve-netboot: $*" >&2
  exit 1
}

stop_matching_provider() {
  local pattern="$1"
  local pid
  local pids
  pids="$(pgrep -f "$pattern" 2>/dev/null || true)"
  printf '%s\n' "$pids" | while IFS= read -r pid; do
    [ -n "$pid" ] || continue
    [ "$pid" != "$$" ] || continue
    [ "$pid" != "$PPID" ] || continue
    echo "stopping existing TFTP provider pid=$pid"
    kill -TERM "$pid" 2>/dev/null || true
  done
  sleep 1
  pids="$(pgrep -f "$pattern" 2>/dev/null || true)"
  printf '%s\n' "$pids" | while IFS= read -r pid; do
    [ -n "$pid" ] || continue
    [ "$pid" != "$$" ] || continue
    [ "$pid" != "$PPID" ] || continue
    echo "forcing existing TFTP provider down pid=$pid"
    kill -KILL "$pid" 2>/dev/null || true
  done
}

find_dnsmasq() {
  if [ -n "${DNSMASQ:-}" ]; then
    echo "$DNSMASQ"
    return
  fi
  if command -v dnsmasq >/dev/null 2>&1; then
    command -v dnsmasq
    return
  fi
  for candidate in \
    /opt/homebrew/opt/dnsmasq/sbin/dnsmasq \
    /usr/local/opt/dnsmasq/sbin/dnsmasq
  do
    if [ -x "$candidate" ]; then
      echo "$candidate"
      return
    fi
  done
  return 1
}

find_python3() {
  if [ -n "${PYTHON3:-}" ]; then
    echo "$PYTHON3"
    return
  fi
  if command -v python3 >/dev/null 2>&1; then
    command -v python3
    return
  fi
  return 1
}

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
  usage
  exit 0
fi

[ -n "$PREFIX" ] || die "AETHER_TFTP_PREFIX must not be empty"
[ -d "$TFTP_ROOT/$PREFIX" ] || die "TFTP prefix missing: $TFTP_ROOT/$PREFIX (run ./prepare-tftp.sh first)"

case "$PROVIDER" in
  dnsmasq)
    DNSMASQ_BIN="$(find_dnsmasq)" || die "dnsmasq not found. Install with: brew install dnsmasq"
    cmd=(
      "$DNSMASQ_BIN"
      "--no-daemon"
      "--port=0"
      "--interface=$IFACE"
      "--bind-interfaces"
      "--listen-address=$SERVER_IP"
      "--enable-tftp"
      "--tftp-root=$TFTP_ROOT"
      "--log-facility=-"
    )

    if [ "$NO_BLOCKSIZE" = "1" ]; then
      cmd+=("--tftp-no-blocksize")
    fi

    if [ "$SINGLE_PORT" = "1" ]; then
      cmd+=("--tftp-single-port")
    fi

    if [ -n "$TFTP_MTU" ]; then
      cmd+=("--tftp-mtu=$TFTP_MTU")
    fi

    if [ -n "${SUDO_USER:-}" ]; then
      cmd+=("--user=$SUDO_USER")
    fi
    ;;
  aether)
    PYTHON3_BIN="$(find_python3)" || die "python3 not found"
    [ -f "$SCRIPT_DIR/aether_tftp.py" ] || die "aether_tftp.py missing"
    cmd=(
      "$PYTHON3_BIN"
      "$SCRIPT_DIR/aether_tftp.py"
      "--host=$SERVER_IP"
      "--port=69"
      "--root=$TFTP_ROOT"
      "--block-size=${AETHER_TFTP_BLOCK_SIZE:-1468}"
    )
    if [ "$SINGLE_PORT" = "1" ]; then
      cmd+=("--single-port")
    fi
    ;;
  *)
    die "unsupported AETHER_TFTP_PROVIDER=$PROVIDER"
    ;;
esac

if [ "${AETHER_NETBOOT_DRY_RUN:-0}" = "1" ]; then
  echo "TFTP provider: $PROVIDER"
  if [ "$REPLACE_EXISTING" = "1" ]; then
    echo "replace existing AetherKernel TFTP providers: yes"
    echo "replace command: pkill -f dnsmasq.*--tftp-root=\${TFTP_ROOT}"
    echo "replace command: pkill -f tftp-now.*serve.*\${TFTP_ROOT}"
    echo "replace command: pkill -f aether_tftp.py.*\${TFTP_ROOT}"
  fi
  printf 'TFTP command:'
  printf ' %s' "${cmd[@]}"
  printf '\n'
  exit 0
fi

if [ "$PROVIDER" = "dnsmasq" ] && [ ! -x "$DNSMASQ_BIN" ]; then
  die "dnsmasq is not executable: $DNSMASQ_BIN"
fi

echo "serving TFTP root $TFTP_ROOT on $IFACE ($SERVER_IP), prefix $PREFIX/"
echo "TFTP provider: $PROVIDER"
echo "Pi EEPROM should use: TFTP_IP=$SERVER_IP TFTP_PREFIX_STR=$PREFIX/"

if [ "$(id -u)" -ne 0 ]; then
  exec sudo -n \
    AETHER_TFTP_ROOT="$TFTP_ROOT" \
    AETHER_TFTP_PREFIX="$PREFIX" \
    AETHER_NETBOOT_INTERFACE="$IFACE" \
    AETHER_NETBOOT_SERVER_IP="$SERVER_IP" \
    AETHER_TFTP_NO_BLOCKSIZE="$NO_BLOCKSIZE" \
    AETHER_TFTP_SINGLE_PORT="$SINGLE_PORT" \
    AETHER_TFTP_MTU="$TFTP_MTU" \
    AETHER_NETBOOT_REPLACE="$REPLACE_EXISTING" \
    AETHER_TFTP_PROVIDER="$PROVIDER" \
    AETHER_TFTP_BLOCK_SIZE="${AETHER_TFTP_BLOCK_SIZE:-512}" \
    DNSMASQ="${DNSMASQ_BIN:-}" \
    PYTHON3="${PYTHON3_BIN:-}" \
    "$SCRIPT_DIR/serve-netboot.sh" "$IFACE" "$TFTP_ROOT"
fi

if [ "$REPLACE_EXISTING" = "1" ]; then
  echo "replacing existing AetherKernel TFTP providers for $TFTP_ROOT"
  stop_matching_provider "dnsmasq.*--tftp-root=${TFTP_ROOT}"
  stop_matching_provider "tftp-now.*serve.*${TFTP_ROOT}"
  stop_matching_provider "aether_tftp.py.*${TFTP_ROOT}"
  pkill -f "dnsmasq.*--tftp-root=${TFTP_ROOT}" 2>/dev/null || true
  pkill -f "tftp-now.*serve.*${TFTP_ROOT}" 2>/dev/null || true
  pkill -f "aether_tftp.py.*${TFTP_ROOT}" 2>/dev/null || true
  sleep 1
fi

exec "${cmd[@]}"
