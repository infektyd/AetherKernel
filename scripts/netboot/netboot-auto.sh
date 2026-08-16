#!/usr/bin/env bash
#===----------------------------------------------------------------------===#
# Unattended AetherKernel netboot loop with Shelly cold power-cycle.
#
# Wraps net-iterate.sh with the Shelly Plug US Gen4 (S4PL-00116US) backend so
# overnight runs self-recover (cold power-cycle re-arms netboot) with no human
# at the bench. Backend name is `shelly` (Gen2+ HTTP RPC; NOT shelly1, NOT wemo).
#
#   usage: ./netboot-auto.sh [tftp-root]
#
# AETHER_POWER_HOST defaults to the proven Gen4 LAN IP (2026-08-16):
#   192.168.179.184  mac ACEBE6F59910  model S4PL-00116US  auth_en=false
# DHCP-reserve that address. Do not reuse the retired Wemo 192.168.179.222.
# Override if the lease moves:
#   export AETHER_POWER_HOST=<ip>
#
# Optional:
#   AETHER_POWER_SHELLY_AUTH    user:pass  (HTTP digest if the Gen4 has auth)
#   AETHER_POWER_DISCHARGE_S    (default 5 — power-off dwell for PSU cap discharge)
#===----------------------------------------------------------------------===#
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

export AETHER_POWER_BACKEND="shelly"
export AETHER_POWER_HOST="${AETHER_POWER_HOST:-192.168.179.184}"
export AETHER_POWER_DISCHARGE_S="${AETHER_POWER_DISCHARGE_S:-5}"

echo "netboot-auto: Shelly Plug US Gen4 / S4PL-00116US (backend=shelly) -> $AETHER_POWER_HOST"
exec "$SCRIPT_DIR/net-iterate.sh" "$@"
