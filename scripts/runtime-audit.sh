#!/usr/bin/env bash
#===----------------------------------------------------------------------===#
# Runtime V28 Swift runtime dependency audit.
#
#   usage: ./runtime-audit.sh [mach-o]
#
# Verifies the load-bearing Swift runtime hooks and heap shims in the built
# Mach-O image with llvm-nm -n. Source-owned fallback hooks are recorded by
# kernel_runtime_audit.c; this host tool proves the linked subset.
#===----------------------------------------------------------------------===#
set -euo pipefail

MACHO="${1:-${AETHER_KERNEL_MACHO:-.build/release/Application}}"
NM_CMD="${AETHER_LLVM_NM:-}"

usage() {
  sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'
}

die() {
  echo "runtime-audit: $*" >&2
  exit 1
}

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
  usage
  exit 0
fi

if [ -z "$NM_CMD" ]; then
  if command -v llvm-nm >/dev/null 2>&1; then
    NM_CMD="llvm-nm"
  elif command -v xcrun >/dev/null 2>&1; then
    NM_CMD="xcrun llvm-nm"
  else
    die "llvm-nm not found"
  fi
fi

if [ "${AETHER_RUNTIME_AUDIT_DRY_RUN:-0}" = "1" ]; then
  echo "Runtime V28 Swift runtime dependency audit dry run"
  echo "mach-o: $MACHO"
  echo "nm: $NM_CMD"
  echo "$NM_CMD -n $MACHO"
  exit 0
fi

[ -f "$MACHO" ] || die "Mach-O not found: $MACHO"

python3 - "$MACHO" "$NM_CMD" <<'PY'
import shlex
import subprocess
import sys

macho, nm_cmd = sys.argv[1], sys.argv[2]

source_hooks = [
    "swift_task_enqueueGlobalImpl",
    "swift_task_enqueueMainExecutorImpl",
    "swift_task_enqueueGlobalWithDelayImpl",
    "swift_task_enqueueGlobalWithDeadlineImpl",
    "swift_task_getMainExecutorImpl",
    "swift_task_isMainExecutorImpl",
    "swift_task_checkIsolatedImpl",
    "swift_task_isIsolatingCurrentContextImpl",
    "swift_task_donateThreadToGlobalExecutorUntilImpl",
    "swift_task_asyncMainDrainQueueImpl",
]
linked_hooks = [
    "swift_task_enqueueGlobalImpl",
    "swift_task_asyncMainDrainQueueImpl",
]
heap_shims = ["malloc", "free", "calloc", "realloc", "posix_memalign"]
linked_heap_shims = ["malloc", "free", "posix_memalign"]
required = linked_hooks + linked_heap_shims

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

symbols = set()
for line in proc.stdout.splitlines():
    parts = line.split()
    if len(parts) < 3:
        continue
    name = parts[-1]
    if name.startswith("_"):
        name = name[1:]
    symbols.add(name)

missing = [name for name in required if name not in symbols]
ok = 0 if missing else 1
missing_text = ",".join(missing) if missing else "none"
print(
    f"runtime-audit ok={ok} version=28 "
    f"source_hooks={len(source_hooks)} linked_hooks={len(linked_hooks)} "
    f"heap_shims={len(heap_shims)} linked_heap_shims={len(linked_heap_shims)} "
    f"required_symbols={len(required)} present={len(required) - len(missing)} "
    f"missing={missing_text} macho={macho}"
)
if missing:
    raise SystemExit(1)
PY
