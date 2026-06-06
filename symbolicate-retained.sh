#!/usr/bin/env bash
#===----------------------------------------------------------------------===#
# Runtime V27 retained symbol lookup.
#
#   usage: ./symbolicate-retained.sh <address> [mach-o]
#
# Maps a retained ELR/FAR-style address to the nearest symbol in the built
# Mach-O image. This is a lightweight context helper, not a stack unwinder.
# The lookup command is equivalent to: llvm-nm -n <mach-o>
#===----------------------------------------------------------------------===#
set -euo pipefail

ADDRESS="${1:-}"
MACHO="${2:-${AETHER_KERNEL_MACHO:-.build/release/Application}}"
NM_CMD="${AETHER_LLVM_NM:-}"

usage() {
  sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'
}

die() {
  echo "symbolicate-retained: $*" >&2
  exit 1
}

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
  usage
  exit 0
fi

[ -n "$ADDRESS" ] || die "missing address"

if [ -z "$NM_CMD" ]; then
  if command -v llvm-nm >/dev/null 2>&1; then
    NM_CMD="llvm-nm"
  elif command -v xcrun >/dev/null 2>&1; then
    NM_CMD="xcrun llvm-nm"
  else
    die "llvm-nm not found"
  fi
fi

if [ "${AETHER_SYMBOLICATE_RETAINED_DRY_RUN:-0}" = "1" ]; then
  echo "Runtime V27 retained symbol lookup dry run"
  echo "address: $ADDRESS"
  echo "mach-o: $MACHO"
  echo "nm: $NM_CMD"
  echo "$NM_CMD -n $MACHO"
  exit 0
fi

[ -f "$MACHO" ] || die "Mach-O not found: $MACHO"

python3 - "$ADDRESS" "$MACHO" "$NM_CMD" <<'PY'
import shlex
import subprocess
import sys

address_text, macho, nm_cmd = sys.argv[1], sys.argv[2], sys.argv[3]
try:
    address = int(address_text, 0)
except ValueError:
    print(f"symbolicate-retained: invalid address: {address_text}", file=sys.stderr)
    raise SystemExit(1)

proc = subprocess.run(
    shlex.split(nm_cmd) + ["-n", macho],
    text=True,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    check=False,
)
if proc.returncode != 0:
    print(proc.stderr, end="", file=sys.stderr)
    raise SystemExit(proc.returncode)

best_addr = None
best_name = None
for line in proc.stdout.splitlines():
    parts = line.split()
    if len(parts) < 3:
        continue
    try:
        sym_addr = int(parts[0], 16)
    except ValueError:
        continue
    if sym_addr <= address and (best_addr is None or sym_addr >= best_addr):
        best_addr = sym_addr
        best_name = parts[-1]

if best_addr is None:
    print(f"symbol address=0x{address:x} symbol_name=<none> symbol_addr=0x0 symbol_offset=0x0 macho={macho}")
else:
    print(
        f"symbol address=0x{address:x} "
        f"symbol_name={best_name} "
        f"symbol_addr=0x{best_addr:x} "
        f"symbol_offset=0x{address - best_addr:x} "
        f"macho={macho}"
    )
PY
