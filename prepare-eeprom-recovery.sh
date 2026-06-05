#!/usr/bin/env bash
#===----------------------------------------------------------------------===#
# Stage a Raspberry Pi 4 EEPROM recovery/update overlay for AetherKernel.
#
#   usage: ./prepare-eeprom-recovery.sh [boot-mount-path]
#
# The script preserves the existing SD boot partition contents and adds only:
#   recovery.bin, pieeprom.upd, pieeprom.sig
#===----------------------------------------------------------------------===#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BOOT="${1:-/Volumes/bootfs}"
CONFIG="${AETHER_EEPROM_CONFIG:-$SCRIPT_DIR/netboot-eeprom-config.txt}"
REPO_API="https://api.github.com/repos/raspberrypi/rpi-eeprom/contents/firmware-2711/default?ref=master"
TOOL_URL="https://raw.githubusercontent.com/raspberrypi/rpi-eeprom/master/rpi-eeprom-config"

die() {
  echo "prepare-eeprom-recovery: $*" >&2
  exit 1
}

sha256_file() {
  shasum -a 256 "$1" | awk '{print $1}'
}

verify_copy() {
  local src="$1"
  local dst="$2"
  local src_hash
  local dst_hash
  src_hash="$(sha256_file "$src")"
  dst_hash="$(sha256_file "$dst")"
  if [ "$src_hash" != "$dst_hash" ]; then
    echo "verify failed for $(basename "$dst")"
    echo "  source: $src_hash"
    echo "  target: $dst_hash"
    exit 1
  fi
  echo "verified $(basename "$dst") sha256 $dst_hash"
}

usage() {
  sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'
}

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
  usage
  exit 0
fi

[ -d "$BOOT" ] || die "boot partition not found: $BOOT"
[ -f "$CONFIG" ] || die "EEPROM config not found: $CONFIG"
command -v python3 >/dev/null || die "python3 not found"
command -v shasum >/dev/null || die "shasum not found"

WORK="$(mktemp -d "${TMPDIR:-/tmp}/aether-eeprom.XXXXXX")"
cleanup() {
  rm -rf "$WORK"
}
trap cleanup EXIT

export WORK REPO_API TOOL_URL CONFIG

python3 <<'PY'
import json
import os
import stat
import sys
import urllib.request

work = os.environ["WORK"]
repo_api = os.environ["REPO_API"]
tool_url = os.environ["TOOL_URL"]
config_path = os.environ["CONFIG"]

def fetch_json(url):
    with urllib.request.urlopen(url, timeout=30) as response:
        return json.load(response)

def fetch_file(url, path):
    with urllib.request.urlopen(url, timeout=30) as response:
        data = response.read()
    with open(path, "wb") as handle:
        handle.write(data)

items = fetch_json(repo_api)
by_name = {item["name"]: item for item in items}
eeproms = sorted(
    item for item in items
    if item["name"].startswith("pieeprom-") and item["name"].endswith(".bin")
)
if "recovery.bin" not in by_name:
    raise SystemExit("official firmware-2711/default recovery.bin not found")
if not eeproms:
    raise SystemExit("official firmware-2711/default pieeprom-*.bin not found")

eeprom = eeproms[-1]
fetch_file(tool_url, os.path.join(work, "rpi-eeprom-config"))
fetch_file(by_name["recovery.bin"]["download_url"], os.path.join(work, "recovery.bin"))
fetch_file(eeprom["download_url"], os.path.join(work, "pieeprom.bin"))
os.chmod(os.path.join(work, "rpi-eeprom-config"), stat.S_IRUSR | stat.S_IWUSR | stat.S_IXUSR)

print(f"using EEPROM image: {eeprom['name']} ({eeprom['size']} bytes)")
print(f"using recovery.bin: {by_name['recovery.bin']['size']} bytes")

overrides = []
with open(config_path, "r", encoding="utf-8") as handle:
    for raw in handle:
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            raise SystemExit(f"invalid EEPROM config line: {raw.rstrip()}")
        key, value = line.split("=", 1)
        overrides.append((key.strip(), value.strip()))

if not overrides:
    raise SystemExit("EEPROM config has no key=value entries")

print("applying EEPROM overrides:")
for key, value in overrides:
    print(f"  {key}={value}")

with open(os.path.join(work, "overrides.env"), "w", encoding="utf-8") as handle:
    for key, value in overrides:
        handle.write(f"{key}={value}\n")
PY

python3 "$WORK/rpi-eeprom-config" "$WORK/pieeprom.bin" > "$WORK/boot.conf.base"

python3 <<'PY'
import os

work = os.environ["WORK"]
base_path = os.path.join(work, "boot.conf.base")
overrides_path = os.path.join(work, "overrides.env")
out_path = os.path.join(work, "boot.conf")

overrides = []
with open(overrides_path, "r", encoding="utf-8") as handle:
    for raw in handle:
        key, value = raw.rstrip("\n").split("=", 1)
        overrides.append((key, value))

override_map = dict(overrides)
seen = set()
out = []
with open(base_path, "r", encoding="utf-8") as handle:
    for raw in handle:
        stripped = raw.strip()
        if stripped and not stripped.startswith("#") and "=" in stripped:
            key = stripped.split("=", 1)[0].strip()
            if key in override_map:
                out.append(f"{key}={override_map[key]}\n")
                seen.add(key)
                continue
        out.append(raw)

missing = [(key, value) for key, value in overrides if key not in seen]
if missing:
    if out and out[-1].strip():
        out.append("\n")
    out.append("# AetherKernel netboot overrides\n")
    for key, value in missing:
        out.append(f"{key}={value}\n")

with open(out_path, "w", encoding="utf-8") as handle:
    handle.writelines(out)
PY

python3 "$WORK/rpi-eeprom-config" --config "$WORK/boot.conf" --out "$WORK/pieeprom.upd" "$WORK/pieeprom.bin"
shasum -a 256 "$WORK/pieeprom.upd" | awk '{print $1}' > "$WORK/pieeprom.sig"

if [ -f "$BOOT/recovery.bin" ] && [ ! -f "$BOOT/recovery.bin.aether-backup" ]; then
  cp "$BOOT/recovery.bin" "$BOOT/recovery.bin.aether-backup"
  echo "backed up existing recovery.bin -> recovery.bin.aether-backup"
fi

cp "$WORK/recovery.bin" "$BOOT/recovery.bin"
cp "$WORK/pieeprom.upd" "$BOOT/pieeprom.upd"
cp "$WORK/pieeprom.sig" "$BOOT/pieeprom.sig"
sync

verify_copy "$WORK/recovery.bin" "$BOOT/recovery.bin"
verify_copy "$WORK/pieeprom.upd" "$BOOT/pieeprom.upd"
verify_copy "$WORK/pieeprom.sig" "$BOOT/pieeprom.sig"

echo "staged EEPROM recovery overlay -> $BOOT"
echo "on next Pi power-on, BCM2711 ROM should run recovery.bin before AetherKernel"
echo "expected after success: recovery.bin is renamed to RECOVERY.000, then the board reboots"
