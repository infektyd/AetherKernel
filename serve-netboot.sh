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
# This is intentionally TFTP-only. The EEPROM config supplies CLIENT_IP/TFTP_IP,
# so dnsmasq must not advertise DHCP on the bench network.
# Blocksize negotiation is enabled by default. Set AETHER_TFTP_NO_BLOCKSIZE=1
# only as a diagnostic fallback if a specific bootloader/server pair needs it.
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

usage() {
  sed -n '2,17p' "$0" | sed 's/^# \{0,1\}//'
}

die() {
  echo "serve-netboot: $*" >&2
  exit 1
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

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
  usage
  exit 0
fi

[ -n "$PREFIX" ] || die "AETHER_TFTP_PREFIX must not be empty"
[ -d "$TFTP_ROOT/$PREFIX" ] || die "TFTP prefix missing: $TFTP_ROOT/$PREFIX (run ./prepare-tftp.sh first)"

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

if [ -n "${SUDO_USER:-}" ]; then
  cmd+=("--user=$SUDO_USER")
fi

if [ "${AETHER_NETBOOT_DRY_RUN:-0}" = "1" ]; then
  printf 'dnsmasq command:'
  printf ' %s' "${cmd[@]}"
  printf '\n'
  exit 0
fi

if [ ! -x "$DNSMASQ_BIN" ]; then
  die "dnsmasq is not executable: $DNSMASQ_BIN"
fi

echo "serving TFTP root $TFTP_ROOT on $IFACE ($SERVER_IP), prefix $PREFIX/"
echo "Pi EEPROM should use: TFTP_IP=$SERVER_IP TFTP_PREFIX_STR=$PREFIX/"

if [ "$(id -u)" -ne 0 ]; then
  exec sudo -n \
    AETHER_TFTP_ROOT="$TFTP_ROOT" \
    AETHER_TFTP_PREFIX="$PREFIX" \
    AETHER_NETBOOT_INTERFACE="$IFACE" \
    AETHER_NETBOOT_SERVER_IP="$SERVER_IP" \
    AETHER_TFTP_NO_BLOCKSIZE="$NO_BLOCKSIZE" \
    DNSMASQ="$DNSMASQ_BIN" \
    "$SCRIPT_DIR/serve-netboot.sh" "$IFACE" "$TFTP_ROOT"
fi

exec "${cmd[@]}"
