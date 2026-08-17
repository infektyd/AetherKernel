#!/usr/bin/env bash
#===----------------------------------------------------------------------===#
# Guided AetherKernel netboot bring-up check.
#
#   usage: ./netboot-doctor.sh [tftp-root]
#
# Use this for the current bench wedge: the old SD image cannot reset itself,
# so the script stages the latest image, proves the Mac/TFTP side, asks for one
# physical reset, then watches for Pi TFTP + fresh serial boot evidence.
#===----------------------------------------------------------------------===#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TFTP_ROOT="${1:-${AETHER_TFTP_ROOT:-$HOME/aether-tftp}}"
PREFIX="${AETHER_TFTP_PREFIX:-aether}"
PREFIX="${PREFIX#/}"
PREFIX="${PREFIX%/}"
IFACE="${AETHER_NETBOOT_INTERFACE:-en0}"
SERVER_IP="${AETHER_NETBOOT_SERVER_IP:-10.42.0.1}"
SERIAL_LOG="${AETHER_SERIAL_LOG:-/tmp/aether-serial.log}"
DNSMASQ_LOG="${AETHER_DNSMASQ_LOG:-${AETHER_TFTP_LOG:-/tmp/aether-dnsmasq.log}}"
TIMEOUT_S="${AETHER_NETBOOT_DOCTOR_TIMEOUT:-180}"

usage() {
  sed -n '2,11p' "$0" | sed 's/^# \{0,1\}//'
}

die() {
  echo "netboot-doctor: $*" >&2
  exit 1
}

print_tftp_diagnostics() {
  local dns_delta="$1"

  if printf '%s' "$dns_delta" | grep -Eq "failed sending .*/start4\\.elf|timeout sending .*/start4\\.elf"; then
    echo "diagnostic: Pi bootloader did not reliably receive start4.elf over TFTP."
    echo "diagnostic: kernel was not reached; try restarting serve-netboot with AETHER_TFTP_NO_BLOCKSIZE=1 for an A/B test."
  fi

  if printf '%s' "$dns_delta" | grep -Eq "failed sending .*/kernel8\\.img|timeout sending .*/kernel8\\.img"; then
    echo "diagnostic: kernel8.img transfer was attempted but not cleanly completed before fallback/retry."
  fi
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

if [ "${AETHER_NETBOOT_DOCTOR_DRY_RUN:-0}" = "1" ]; then
  echo "check interface: $IFACE at $SERVER_IP"
  echo "check TFTP root: $TFTP_ROOT"
  echo "stage latest image: ./netflash.sh $TFTP_ROOT"
  echo "ACTION: reset or power-cycle the Pi once"
  echo "watch TFTP prefix: $PREFIX/"
  echo "watch serial log: $SERIAL_LOG"
  echo "watch TFTP log: $DNSMASQ_LOG"
  exit 0
fi

[ -n "$PREFIX" ] || die "AETHER_TFTP_PREFIX must not be empty"
[ -f "$SERIAL_LOG" ] || die "serial log missing: $SERIAL_LOG"
[ -f "$DNSMASQ_LOG" ] || die "TFTP log missing: $DNSMASQ_LOG"

if ! ifconfig "$IFACE" | grep -q "status: active"; then
  die "$IFACE is not active"
fi
if ! ifconfig "$IFACE" | grep -q "inet $SERVER_IP "; then
  die "$IFACE does not have $SERVER_IP"
fi

tftp_server_running || die "TFTP provider does not appear to be serving $TFTP_ROOT"

"$SCRIPT_DIR/netflash.sh" "$TFTP_ROOT"

[ -f "$TFTP_ROOT/$PREFIX/kernel8.img" ] || die "staged kernel missing"
[ -f "$TFTP_ROOT/$PREFIX/config.txt" ] || die "staged config missing"

tmp="$(mktemp -d)"
cleanup() {
  rm -rf "$tmp"
}
trap cleanup EXIT
(
  cd "$tmp"
  printf 'binary\nget %s/config.txt\nquit\n' "$PREFIX" | tftp "$SERVER_IP" >/tmp/aether-doctor-tftp.out 2>&1
)
echo "local TFTP check:"
cat /tmp/aether-doctor-tftp.out

serial_start="$(file_size "$SERIAL_LOG")"
dns_start="$(file_size "$DNSMASQ_LOG")"

echo
echo "ACTION: reset or power-cycle the Pi once now."
echo "I am watching for: TFTP sends $PREFIX/kernel8.img + serial prints fresh Runtime V41 shell markers."
echo "Timeout: ${TIMEOUT_S}s"

deadline=$((SECONDS + TIMEOUT_S))
while [ "$SECONDS" -lt "$deadline" ]; do
  dns_delta="$(file_delta "$DNSMASQ_LOG" "$dns_start")"
  serial_delta="$(file_delta "$SERIAL_LOG" "$serial_start")"

  if printf '%s' "$dns_delta" | grep -q "$PREFIX/.*kernel8.img" \
    && printf '%s' "$serial_delta" | grep -q "=== AetherKernel ===" \
    && printf '%s' "$serial_delta" | grep -q "runtime v4: irq-backed uart shell" \
    && printf '%s' "$serial_delta" | grep -q "runtime v5: diagnostics shell" \
    && printf '%s' "$serial_delta" | grep -q "runtime v6: retained panic/fault records" \
    && printf '%s' "$serial_delta" | grep -q "runtime v7: memory map + frame allocator" \
    && printf '%s' "$serial_delta" | grep -q "runtime v8: allocator guardrails" \
    && printf '%s' "$serial_delta" | grep -q "runtime v9: bounded memory pressure self-tests" \
    && printf '%s' "$serial_delta" | grep -q "runtime v10: explicit guard probes" \
    && printf '%s' "$serial_delta" | grep -q "runtime v11: boot and soak invariants" \
    && printf '%s' "$serial_delta" | grep -q "runtime v12: kernel object table + task registry" \
    && printf '%s' "$serial_delta" | grep -q "runtime v13: bounded mailbox message queues" \
    && printf '%s' "$serial_delta" | grep -q "runtime v14: deterministic task supervisor" \
    && printf '%s' "$serial_delta" | grep -q "runtime v15: capability-tagged kernel handles" \
    && printf '%s' "$serial_delta" | grep -q "runtime v16: kernel event log ring" \
    && printf '%s' "$serial_delta" | grep -q "runtime v17: deterministic boot certificate" \
    && printf '%s' "$serial_delta" | grep -q "runtime v18: cooperative cancellation tokens" \
    && printf '%s' "$serial_delta" | grep -q "runtime v19: structured aether task spawn" \
    && printf '%s' "$serial_delta" | grep -q "runtime v20: bounded async channels" \
    && printf '%s' "$serial_delta" | grep -q "runtime v21: mmu ownership boundary" \
    && printf '%s' "$serial_delta" | grep -q "runtime v22: guarded typed pools" \
    && printf '%s' "$serial_delta" | grep -q "runtime v23: allocator and pool pressure telemetry" \
    && printf '%s' "$serial_delta" | grep -q "runtime v24: fixed driver registry" \
    && printf '%s' "$serial_delta" | grep -q "runtime v25: scriptable command protocol v2" \
    && printf '%s' "$serial_delta" | grep -q "runtime v27: panic taxonomy and symbolic retained records" \
    && printf '%s' "$serial_delta" | grep -q "runtime v28: swift runtime dependency audit" \
    && printf '%s' "$serial_delta" | grep -q "runtime v29: agent-oriented control session" \
    && printf '%s' "$serial_delta" | grep -q "runtime v30: swift-native kernel substrate certificate" \
    && printf '%s' "$serial_delta" | grep -q "schedselftest ok=1 version=31" \
    && printf '%s' "$serial_delta" | grep -q "schedselftest ok=1 version=32" \
      && printf '%s' "$serial_delta" | grep -q "schedselftest ok=1 version=33" \
      && printf '%s' "$serial_delta" | grep -q "schedselftest ok=1 version=34" \
      && printf '%s' "$serial_delta" | grep -q "schedselftest ok=1 version=35" \
      && printf '%s' "$serial_delta" | grep -q "schedselftest ok=1 version=36" \
      && printf '%s' "$serial_delta" | grep -q "schedselftest ok=1 version=37" \
      && printf '%s' "$serial_delta" | grep -q "schedselftest ok=1 version=38" \
      && printf '%s' "$serial_delta" | grep -q "schedselftest ok=1 version=39" \
      && printf '%s' "$serial_delta" | grep -q "schedselftest ok=1 version=40" \
      && printf '%s' "$serial_delta" | grep -q "schedselftest ok=1 version=41" \
      && printf '%s' "$serial_delta" | grep -q "schedselftest ok=1 version=42" \
      && printf '%s' "$serial_delta" | grep -q "schedselftest ok=1 version=43" \
      && printf '%s' "$serial_delta" | grep -q "schedselftest ok=1 version=44" \
      && printf '%s' "$serial_delta" | grep -q "runtime v45: dynamic virtual memory (page tables + TLB)" \
      && printf '%s' "$serial_delta" | grep -q "runtime v46: kernel/user address-space split (isolated page tables)" \
      && printf '%s' "$serial_delta" | grep -q "runtime v47: EL0 entry/exit and context save/restore" \
      && printf '%s' "$serial_delta" | grep -q "runtime v48: syscall ABI via SVC from EL0" \
      && printf '%s' "$serial_delta" | grep -q "runtime v49: fault-safe copy_from_user / copy_to_user" \
      && printf '%s' "$serial_delta" | grep -q "runtime v50: EPIC A capstone" \
      && printf '%s' "$serial_delta" | grep -q "runtime v51: process abstraction" \
      && printf '%s' "$serial_delta" | grep -q "runtime v52: user binary loader" \
      && printf '%s' "$serial_delta" | grep -q "runtime v53: multi-process user execution" \
      && printf '%s' "$serial_delta" | grep -q "runtime v54: BCM2711 EMMC2/SDHCI register probe" \
      && printf '%s' "$serial_delta" | grep -q "runtime v55: SD card identification (CMD0/CMD8/ACMD41/CMD2/CMD3)" \
      && printf '%s' "$serial_delta" | grep -q "runtime v56: single block read CMD17 + MBR 0x55AA verification" \
      && printf '%s' "$serial_delta" | grep -q "runtime v57: FAT32 file read (config.txt bytes + checksum)" \
      && printf '%s' "$serial_delta" | grep -q "runtime v58: VideoCore mailbox property interface (firmware revision)" \
      && printf '%s' "$serial_delta" | grep -q "runtime v59: VideoCore framebuffer allocation (width/height/pitch/addr)" \
      && printf '%s' "$serial_delta" | grep -q "runtime v60: text console (8x8 font blit + readback proof)" \
      && printf '%s' "$serial_delta" | grep -q "runtime v61: BCM2711 PCIe RC bring-up" \
      && printf '%s' "$serial_delta" | grep -q "runtime v62: VL805 USB 3.0 xHCI config-space probe" \
      && printf '%s' "$serial_delta" | grep -q "runtime v63: xHCI capability register probe" \
      && printf '%s' "$serial_delta" | grep -q "sdhci ok=1 version=54" \
      && printf '%s' "$serial_delta" | grep -q "card ok=1 version=55" \
      && printf '%s' "$serial_delta" | grep -q "block ok=1 version=56" \
      && printf '%s' "$serial_delta" | grep -q "fat32 ok=1 version=57" \
      && printf '%s' "$serial_delta" | grep -q "mailbox ok=1 version=58" \
      && printf '%s' "$serial_delta" | grep -q "framebuf ok=1 version=59" \
      && printf '%s' "$serial_delta" | grep -q "console ok=1 version=60" \
      && printf '%s' "$serial_delta" | grep -q "pcie ok=1 version=61" \
      && printf '%s' "$serial_delta" | grep -q "vl805 ok=1 version=62" \
      && printf '%s' "$serial_delta" | grep -q "xhci ok=1 version=63" \
      && printf '%s' "$serial_delta" | grep -q "runtime v64: xHCI controller init" \
      && printf '%s' "$serial_delta" | grep -q "xhci_run ok=1 version=64 ports_connected=.*" \
      && printf '%s' "$serial_delta" | grep -q "runtime v65: USB device enumeration" \
      && printf '%s' "$serial_delta" | grep -q "usb_enum ok=[01] version=65 addr=.* vendor=.* product=.* class=.* stage=.* portsc=.* portscR=.* slot_raw=.* ad_raw=" \
      && printf '%s' "$serial_delta" | grep -q "runtime v66: HID boot-protocol keyboard" \
      && printf '%s' "$serial_delta" | grep -q "kbd ok=[01] version=66 keycode=.* char=" \
      && printf '%s' "$serial_delta" | grep -q "hubwalk ok=[01] version=66 ports=.* connected=.* hid=" \
      && printf '%s' "$serial_delta" | grep -q "runtime v67: GENET register probe" \
      && printf '%s' "$serial_delta" | grep -q "genet ok=1 version=67 rev=.* mdio=.* link=" \
      && printf '%s' "$serial_delta" | grep -q "runtime v68: GENET UMAC MAC and RX MIB" \
      && printf '%s' "$serial_delta" | grep -q "genet2 ok=1 version=68 mac=.* rx=.* frames=.* bytes=" \
      && printf '%s' "$serial_delta" | grep -q "runtime v69: GENET mailbox station MAC" \
      && printf '%s' "$serial_delta" | grep -q "genet3 ok=1 version=69 mac=.* mbox=.* umac=" \
      && printf '%s' "$serial_delta" | grep -q "runtime v70: GENET mailbox board serial" \
      && printf '%s' "$serial_delta" | grep -q "genet4 ok=1 version=70 serial=.* mbox=.* mac=" \
      && printf '%s' "$serial_delta" | grep -q "runtime v71: GPIO register probe" \
      && printf '%s' "$serial_delta" | grep -q "gpio ok=1 version=71 fsel=.* pup=.* uart=" \
      && printf '%s' "$serial_delta" | grep -q "runtime v72: GENET leftover-RX stop and NC RX ring" \
      && printf '%s' "$serial_delta" | grep -q "genet5 ok=1 version=72 stop=.* ring=.* rx=.* frames=" \
      && printf '%s' "$serial_delta" | grep -q "runtime v73: GENET UMAC MAC and TX ARP" \
      && printf '%s' "$serial_delta" | grep -q "genet6 ok=1 version=73 mac=.* tx=.* frames=" \
      && printf '%s' "$serial_delta" | grep -q "runtime v74: GENET Linux ring-16 and TX CONS" \
      && printf '%s' "$serial_delta" | grep -q "genet7 ok=1 version=74 ring=.* tx=.* cons=.* prod=.* frames=" \
      && printf '%s' "$serial_delta" | grep -q "runtime v75: GENET v4 TDMA PROD doorbell" \
      && printf '%s' "$serial_delta" | grep -q "genet8 ok=1 version=75 prod=.* cons=.* tx=.* frames=" \
      && printf '%s' "$serial_delta" | grep -q "runtime v76: GENET ARP or ICMP reply" \
      && printf '%s' "$serial_delta" | grep -q "genet9 ok=1 version=76 rx=.* tx=.* kind=" \
      && printf '%s' "$serial_delta" | grep -q "runtime v77: GENET bounded multi-reply poll" \
      && printf '%s' "$serial_delta" | grep -q "genet10 ok=1 version=77 rx=.* tx=.* replies=.* kind=" \
      && printf '%s' "$serial_delta" | grep -q "runtime v78: GENET bounded UDP echo" \
      && printf '%s' "$serial_delta" | grep -q "genet11 ok=1 version=78 rx=.* tx=.* replies=.* kind=" \
      && printf '%s' "$serial_delta" | grep -q "runtime v79: GENET bounded TCP echo" \
      && printf '%s' "$serial_delta" | grep -q "genet12 ok=1 version=79 rx=.* tx=.* replies=.* kind=" \
      && printf '%s' "$serial_delta" | grep -q "runtime v80: I2C and SPI register probe" \
      && printf '%s' "$serial_delta" | grep -q "i2c ok=1 version=80 bsc=.* div=.* spi=" \
      && printf '%s' "$serial_delta" | grep -q "runtime v81: PWM register probe" \
      && printf '%s' "$serial_delta" | grep -q "pwm ok=1 version=81 ctl=.* sta=.* pwm1=" \
      && printf '%s' "$serial_delta" | grep -q "runtime v82: I2C no-ACK transfer" \
      && printf '%s' "$serial_delta" | grep -q "i2c2 ok=1 version=82 nack=1 addr=.* sta=" \
      && printf '%s' "$serial_delta" | grep -q "runtime v83: SPI0 bounded transfer" \
      && printf '%s' "$serial_delta" | grep -q "spi2 ok=1 version=83 done=1 loop=.* rx=" \
      && printf '%s' "$serial_delta" | grep -q "runtime v84: system timer register probe" \
      && printf '%s' "$serial_delta" | grep -q "stimer ok=1 version=84 clo=.* chi=.* chans=" \
      && printf '%s' "$serial_delta" | grep -q "runtime v85: SD config.txt reload" \
      && printf '%s' "$serial_delta" | grep -q "sdload ok=1 version=85 file=config.txt bytes=.* checksum=.* match=1" \
      && printf '%s' "$serial_delta" | grep -q "runtime v86: FAT32 root list" \
      && printf '%s' "$serial_delta" | grep -q "sdls ok=1 version=86 files=.* config=1 other=" \
      && printf '%s' "$serial_delta" | grep -q "runtime v87: FAT32 second file" \
      && printf '%s' "$serial_delta" | grep -q "sdfile ok=1 version=87 name=.* bytes=.* checksum=" \
      && printf '%s' "$serial_delta" | grep -q "runtime v88: FAT32 overlays walk" \
      && printf '%s' "$serial_delta" | grep -q "sdovl ok=1 version=88 files=.* name=" \
      && printf '%s' "$serial_delta" | grep -q "runtime v89: FAT32 overlay file" \
      && printf '%s' "$serial_delta" | grep -q "sdovf ok=1 version=89 name=.* bytes=.* checksum=" \
      && printf '%s' "$serial_delta" | grep -q "runtime v90: FAT32 issue.txt" \
      && printf '%s' "$serial_delta" | grep -q "sdiss ok=[01] version=90 present=[01] name=.* bytes=.* checksum=" \
      && printf '%s' "$serial_delta" | grep -q "runtime v91: GPIO output readback" \
      && printf '%s' "$serial_delta" | grep -q "gpio2 ok=1 version=91 pin=42 set=1 clr=1" \
      && printf '%s' "$serial_delta" | grep -q "runtime v92: system timer C1 match" \
      && printf '%s' "$serial_delta" | grep -q "stimer2 ok=1 version=92 chan=1 match=1" \
      && printf '%s' "$serial_delta" | grep -q "runtime v93: PWM clock enable" \
      && printf '%s' "$serial_delta" | grep -q "pwm2 ok=1 version=93 clk=1 en=1" \
      && printf '%s' "$serial_delta" | grep -q "runtime v94: GPIO PUP readback" \
      && printf '%s' "$serial_delta" | grep -q "gpio3 ok=1 version=94 pin=26 up=1 dn=1" \
      && printf '%s' "$serial_delta" | grep -q "runtime v95: system timer C3 match" \
      && printf '%s' "$serial_delta" | grep -q "stimer3 ok=1 version=95 chan=3 match=1" \
      && printf '%s' "$serial_delta" | grep -q "runtime v96: mailbox temperature" \
      && printf '%s' "$serial_delta" | grep -q "mboxt ok=1 version=96 temp=" \
      && printf '%s' "$serial_delta" | grep -q "runtime v97: mailbox clock rate" \
      && printf '%s' "$serial_delta" | grep -q "mboxc ok=1 version=97 clk=3 hz=" \
      && printf '%s' "$serial_delta" | grep -q "runtime v98: watchdog remaining" \
      && printf '%s' "$serial_delta" | grep -q "wdog2 ok=1 version=98 armed=1 off=1 remain=" \
      && printf '%s' "$serial_delta" | grep -q "runtime v99: mailbox voltage" \
      && printf '%s' "$serial_delta" | grep -q "mboxv ok=1 version=99 id=1 uv=" \
      && printf '%s' "$serial_delta" | grep -q "runtime v100: RNG200 word" \
      && printf '%s' "$serial_delta" | grep -q "rng ok=1 version=100 ready=1 data=" \
      && printf '%s' "$serial_delta" | grep -q "runtime v101: DMA memcpy" \
      && printf '%s' "$serial_delta" | grep -q "dma2 ok=1 version=101 chan=4 match=1 bytes=32" \
      && printf '%s' "$serial_delta" | grep -q "runtime v102: SD free-cluster write" \
      && printf '%s' "$serial_delta" | grep -q "sdwr ok=1 version=102 match=1 bytes=512 clus=" \
      && printf '%s' "$serial_delta" | grep -q "runtime v103: FAT32 scratch create" \
      && printf '%s' "$serial_delta" | grep -q "sdmk ok=1 version=103 match=1 created=" \
      && printf '%s' "$serial_delta" | grep -q "runtime v104: FAT32 scratch reread" \
      && printf '%s' "$serial_delta" | grep -q "sdrd ok=1 version=104 match=1 present=1 name=" \
      && printf '%s' "$serial_delta" | grep -q "runtime v105: SDHCI card status" \
      && printf '%s' "$serial_delta" | grep -q "sdst ok=1 version=105 state=4 ready=1 rca=" \
      && printf '%s' "$serial_delta" | grep -q "runtime v106: SDHCI send SCR" \
      && printf '%s' "$serial_delta" | grep -q "sdscr ok=1 version=106 spec=" \
      && printf '%s' "$serial_delta" | grep -q "runtime v107: SDHCI SD_STATUS" \
      && printf '%s' "$serial_delta" | grep -q "sdss ok=1 version=107 type=" \
      && printf '%s' "$serial_delta" | grep -q "runtime v108: SDHCI bus width" \
      && printf '%s' "$serial_delta" | grep -q "sdbus ok=1 version=108 bits=" \
      && printf '%s' "$serial_delta" | grep -q "runtime v109: SDHCI multi-block" \
      && printf '%s' "$serial_delta" | grep -q "sdmb ok=1 version=109 blocks=" \
      && printf '%s' "$serial_delta" | grep -q "runtime v110: SDHCI switch check" \
      && printf '%s' "$serial_delta" | grep -q "sdsw ok=1 version=110 grp1=" \
      && printf '%s' "$serial_delta" | grep -q "runtime v111: SDHCI multi-block write" \
      && printf '%s' "$serial_delta" | grep -q "sdmw ok=1 version=111 match=" \
      && printf '%s' "$serial_delta" | grep -q "runtime v112: SDHCI set block count" \
      && printf '%s' "$serial_delta" | grep -q "sdbc ok=1 version=112 count=" \
      && printf '%s' "$serial_delta" | grep -q "vmmcheck ok=1" \
      && printf '%s' "$serial_delta" | grep -q "asplit ok=1 version=46" \
      && printf '%s' "$serial_delta" | grep -q "el0 ok=1 version=47" \
      && printf '%s' "$serial_delta" | grep -q "syscall ok=1 version=48 abi=48 table=1 dispatched=1 num=1 ret=0x0000000000482026" \
      && printf '%s' "$serial_delta" | grep -q "uaccess ok=1 version=49" \
      && printf '%s' "$serial_delta" | grep -q "usermode ok=1 version=50 fault_contained=1" \
      && printf '%s' "$serial_delta" | grep -q "process ok=1 version=51" \
      && printf '%s' "$serial_delta" | grep -q "processes ok=1 version=52" \
      && printf '%s' "$serial_delta" | grep -q "multiprocess ok=1 version=53" \
    && printf '%s' "$serial_delta" | grep -q "handlecheck ok=1 .*handle_selftest=1 .*cap_selftest=1" \
    && printf '%s' "$serial_delta" | grep -q "shell ready commands=help,protocol,status,heap,queues,tasks,tasks2,kobjects,drivers,drivercheck,mailboxes,sendtest,supervisor,health,handlecheck,capcheck,events,runtime,agent,certificate,sched,sched2,sched3,sched4,sched5,sched6,sched7,sched8,sched9,sched10,sched11,sched12,cores,locks,runqueues,diag,irqs,timers,memcheck,faults,retained,retained-clear,memmap,mmu,pools,poolcheck,heapfrag,poolstats,frames,heapcheck,framecheck,stress,frameprobe,bootcert,canceltest,taskcheck,channeltest,bootcheck,soak,heap-invalid-free-test,heap-double-free-test,panic-test,fault-test,reboot,vmm,asplit,el0,syscall,uaccess,usermode,process,loader,multiprocess,sdhci,card,block,fat32,mailbox,framebuf,console,pcie,vl805,xhci"; then
    echo "netboot bring-up verified"
    echo "--- TFTP delta ---"
    printf '%s\n' "$dns_delta" | tail -n 80
    echo "--- serial delta ---"
    printf '%s\n' "$serial_delta" | tail -n 120
    exit 0
  fi
  sleep 1
done

echo "netboot bring-up did not verify within ${TIMEOUT_S}s"
print_tftp_diagnostics "$(file_delta "$DNSMASQ_LOG" "$dns_start")"
echo "--- TFTP delta ---"
file_delta "$DNSMASQ_LOG" "$dns_start" | tail -n 80
echo "--- serial delta ---"
file_delta "$SERIAL_LOG" "$serial_start" | tail -n 120
exit 1
