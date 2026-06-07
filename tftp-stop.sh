#!/usr/bin/env bash
#===----------------------------------------------------------------------===#
# tftp-stop.sh — stop any AetherKernel netboot TFTP server.
#
# Deliberately NARROW so it can be granted passwordless sudo safely: it only
# terminates the repo's own TFTP providers (aether_tftp.py, or a dnsmasq bound to
# our aether-tftp root). It cannot kill anything else.
#
# Companion to serve-netboot.sh. A stale TFTP server launched under sudo runs as
# root and holds UDP/69, blocking the next serve-netboot bind; the caller can't
# kill a root process without privilege, so this scoped helper exists to do it.
#
#   usage:  sudo ./tftp-stop.sh
#===----------------------------------------------------------------------===#
set -uo pipefail

killed=0
for pat in 'aether_tftp\.py' 'dnsmasq.*aether-tftp'; do
  pids="$(pgrep -f "$pat" 2>/dev/null || true)"
  if [ -n "$pids" ]; then
    echo "tftp-stop: terminating [$pat]: $pids"
    pkill -f "$pat" 2>/dev/null || true
    killed=1
  fi
done

[ "$killed" = "0" ] && echo "tftp-stop: no AetherKernel TFTP server running (port 69 already free)"
exit 0
