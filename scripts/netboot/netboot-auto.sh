#!/usr/bin/env bash
#===----------------------------------------------------------------------===#
# Unattended AetherKernel netboot loop with Wemo cold power-cycle.
#
# Wraps net-iterate.sh with the verified Wemo WSP080 backend so overnight runs
# self-recover (cold power-cycle re-arms netboot) with no human at the bench.
#
#   usage: ./netboot-auto.sh [tftp-root]
#
# Override any of these on the command line if the plug's IP changes:
#   AETHER_POWER_HOST       (default 192.168.179.222 — RESERVE THIS IN DHCP)
#   AETHER_POWER_WEMO_PORT  (default 49152 — WSP080 uses 49152, not 49153)
#   AETHER_POWER_DISCHARGE_S(default 5 — power-off dwell for PSU cap discharge)
#===----------------------------------------------------------------------===#
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

export AETHER_POWER_BACKEND="wemo"
export AETHER_POWER_HOST="${AETHER_POWER_HOST:-192.168.179.222}"
export AETHER_POWER_WEMO_PORT="${AETHER_POWER_WEMO_PORT:-49152}"
export AETHER_POWER_DISCHARGE_S="${AETHER_POWER_DISCHARGE_S:-5}"

echo "netboot-auto: Wemo cold-cycle backend -> $AETHER_POWER_HOST:$AETHER_POWER_WEMO_PORT"
exec "$SCRIPT_DIR/net-iterate.sh" "$@"
