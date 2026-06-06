# Raspberry Pi 4B Bare-Metal Embedded Swift Runbook

Runbook for flashing and testing the bare-metal Raspberry Pi 4B kernel (`kernel8.img`) built in Embedded Swift.

## 1. Hardware Prep
Prepare the **DSD TECH SH-U09C FT232 USB-TTL adapter** and connect it to the Pi:

* **Voltage Jumper:** Set the voltage-select jumper to **3.3V** (do NOT use 5V; the Pi's GPIO pins operate at 3.3V and 5V will damage the board).
* **Loopback Jumper:** Remove any loopback jumper bridging TXD and RXD.
* **Power:** Do NOT connect the adapter's VCC pin. Power the Pi via its USB-C port.
* **Wiring (with crossover):** Connect exactly 3 wires from the USB-TTL adapter to the Pi GPIO header:
  * **Adapter GND** -> **Pi Header Pin 6** (GND)
  * **Adapter RX**  -> **Pi Header Pin 8**  (GPIO14 / TXD0)
  * **Adapter TX**  -> **Pi Header Pin 10** (GPIO15 / RXD0)

*Note: Pins 6, 8, and 10 are the 3rd, 4th, and 5th pins on the outer (even-numbered) row starting from the pin-1 corner.*

## 2. SD Card Preparation
Prepare the SD card's boot partition (typically mounted on macOS at `/Volumes/boot` or `/Volumes/bootfs`):

1. **Back up existing files:** Save a copy of the existing `kernel8.img` and `config.txt` from the SD boot partition before overwriting.
2. **Configure `config.txt`:** Ensure the `config.txt` file on the boot partition contains at least the following configuration parameters:
   ```ini
   arm_64bit=1
   kernel=kernel8.img
   enable_uart=1
   dtoverlay=disable-bt
   init_uart_clock=48000000
   enable_gic=1
   ```
   *(Note: `dtoverlay=disable-bt` routes the high-quality PL011 UART0 to GPIO14/15 instead of the mini-UART; `init_uart_clock=48000000` matches the kernel's PL011 baud divisor; `enable_gic=1` keeps the timer IRQ path available.)*
3. **Flash kernel:** Run `./flash.sh /Volumes/bootfs` or copy the newly built `kernel8.img` onto the root of the boot partition. The script verifies `kernel8.img` and `config.txt` with SHA-256 after copying.

## 3. Network Iteration over Direct Ethernet

The current bench wiring supports a faster loop: leave the Pi wired to serial
and Ethernet, stage the boot files on the Mac, then reset the Pi. This uses the
Pi 4 EEPROM bootloader's TFTP path; AetherKernel itself still has no Ethernet
driver.

### One-time EEPROM setup

Current AetherKernel cannot run `rpi-eeprom-config`, so this still needs one
Raspberry Pi OS or bootloader-recovery pass. Configure the Pi 4 EEPROM for the
direct Mac-Pi link. The checked-in source of truth is
`netboot-eeprom-config.txt`:

```ini
BOOT_UART=1
BOOT_ORDER=0xf12
TFTP_FILE_TIMEOUT=30000
TFTP_IP=10.42.0.1
CLIENT_IP=10.42.0.2
SUBNET=255.255.255.0
TFTP_PREFIX=1
TFTP_PREFIX_STR=aether/
```

Use `BOOT_ORDER=0xf12` when the SD card remains inserted: network first, then
SD fallback, then restart. `BOOT_ORDER=0xf21` is SD-first and will hide netboot
as long as a bootable SD card is present. Keep `TFTP_FILE_TIMEOUT=30000`; the
minimum `5000` is too tight for reliable firmware fetches on this bench.

### Mac host setup

1. **Set the direct Ethernet interface.** The current Mac interface is `en0`.
   Give it the static server address expected by the EEPROM config:
   ```bash
   sudo networksetup -setmanual Ethernet 10.42.0.1 255.255.255.0
   ifconfig en0
   ```
   `ifconfig en0` should show `status: active` and `inet 10.42.0.1`.
2. **Seed the TFTP tree.** If the SD boot partition is mounted on the Mac, copy
   firmware from that known-working source:
   ```bash
   ./prepare-tftp.sh /Volumes/bootfs
   ```
   If the SD card is still in the Pi, download the minimal Pi 4 firmware set
   instead:
   ```bash
   ./prepare-tftp.sh --download
   ```
   Both modes write into `~/aether-tftp/aether/` and overwrite `config.txt`
   with this repo's current file. The staged tree intentionally uses Pi 4
   `start4.elf`/`fixup4.dat` only. Generic `start.elf`/`fixup.dat` fallback is
   pruned because this bench can hang after loading that fallback path.
3. **Serve TFTP in the foreground:**
   ```bash
   ./serve-netboot.sh en0
   ```
   The script defaults to the repo-owned `aether_tftp.py` in TFTP-only mode.
   It does not advertise DHCP; the EEPROM static-IP config supplies the Pi's IP
   and server IP. UDP port 69 requires root; if sudo credentials are not cached,
   macOS will reject startup until you run it from an admin-authenticated
   terminal. The proven bench defaults are 1468-byte blocks plus single-port
   duplicate-RRQ handling. Homebrew `dnsmasq` remains an explicit fallback:
   `AETHER_TFTP_PROVIDER=dnsmasq ./serve-netboot.sh en0`.

If the EEPROM is already network-booting but still has a bad timeout, stage a
TFTP self-update by placing `pieeprom.sig` and `pieeprom.upd` in
`~/aether-tftp/aether/`. On the next cold boot, the Pi 4 bootloader checks those
files before loading firmware and resets after applying a changed EEPROM image.

### Per-iteration loop

With `serve-netboot.sh` still running in one terminal, the normal loop is:

```bash
./net-iterate.sh
```

It builds, stages `kernel8.img`/`config.txt`, sends the serial reset command,
and waits for two proofs: a Pi TFTP fetch of `aether/kernel8.img` and a fresh
AetherKernel banner plus Runtime V38 marker, `rtv2 fast/slow/long` zero-lines,
the expanded `shell ready` command list, and shell probes for `status`,
`protocol`, request-wrapped `status`, `bootcert`, `canceltest`, `taskcheck`, `channeltest`, `mmu`, `poolcheck`,
`pools`, `heapfrag`, `poolstats`, `bootcheck`, `stress`, `soak`, `kobjects`,
`drivers`, `drivercheck`, `tasks2`, `mailboxes`, `sendtest`, `supervisor`,
`health`, `capcheck`, and `events`.

For repeated proof runs, Runtime V26 host soak harness wraps the same boot path
and records request-wrapped summaries after each cycle:

```bash
AETHER_SOAK_CYCLES=12 ./soak-loop.sh /Users/hansaxelsson/aether-tftp
```

The harness leaves the TFTP provider lifecycle to you. It runs `net-iterate.sh`,
then sends `req id=<n> cmd=status`, `bootcert`, `stress`, `soak`, and `events`
through `serial-probe.sh`, appending `soak summary cycle=...` lines to
`${AETHER_SOAK_LOG:-/tmp/aether-soak.log}`. The hardware proof for this repo
used 3 cycles and ended with `soak result ok=1 cycles=3 completed=3`; the
certificate line remained `bootcert ok=1 version=25 ... events_lost=0` because
V26 is a host harness over the Runtime V25 kernel image.

The first reset after adding this workflow is still physical if the currently
running SD image predates the serial reset hook. For that first proof, use the
guided harness:

```bash
./netboot-doctor.sh
```

It checks the Mac Ethernet address, confirms a TFTP provider is serving the root,
stages the latest image, proves local TFTP access, then tells you exactly when
to reset or power-cycle the Pi. After the staged image has booted once, the
reset step is handled by:

```bash
./serial-reset.sh
```

The expected serial flow is bootloader `TFTP_GET` lines, then the AetherKernel
banner, padded `CurrentEL`, repeating `rtv2 fast/slow/long` cadences, the
Runtime V5 through V38 kernel markers (V26 is host-only), and:

```text
runtime v5: diagnostics shell
runtime v6: retained panic/fault records
runtime v7: memory map + frame allocator
runtime v8: allocator guardrails
runtime v9: bounded memory pressure self-tests
runtime v10: explicit guard probes
runtime v11: boot and soak invariants
runtime v12: kernel object table + task registry
runtime v13: bounded mailbox message queues
runtime v14: deterministic task supervisor
runtime v15: capability-tagged kernel handles
runtime v16: kernel event log ring
runtime v17: deterministic boot certificate
runtime v18: cooperative cancellation tokens
runtime v19: structured aether task spawn
runtime v20: bounded async channels
runtime v21: mmu ownership boundary
runtime v22: guarded typed pools
runtime v23: allocator and pool pressure telemetry
runtime v24: fixed driver registry
runtime v25: scriptable command protocol v2
runtime v27: panic taxonomy and symbolic retained records
runtime v28: swift runtime dependency audit
runtime v29: agent-oriented control session
runtime v30: swift-native kernel substrate certificate
runtime v31: preemptive scheduler substrate
runtime v32: smp secondary-core bring-up
runtime v33: atomics spinlocks per-core run queues
runtime v34: timer-driven smp scheduler dispatch
runtime v35: secondary-owned scheduler workers
runtime v36: timer-fed secondary scheduler workers
runtime v37: timer-fed secondary C scheduler jobs
runtime v38: secondary scheduler wake protocol
handlecheck ok=1 handle_selftest=1 cap_selftest=1
shell ready commands=help,protocol,status,heap,queues,tasks,tasks2,kobjects,drivers,drivercheck,mailboxes,sendtest,supervisor,health,capcheck,events,runtime,agent,certificate,sched,sched2,sched3,sched4,sched5,sched6,cores,locks,runqueues,diag,irqs,timers,memcheck,faults,retained,retained-clear,memmap,mmu,pools,poolcheck,heapfrag,poolstats,frames,heapcheck,framecheck,stress,frameprobe,bootcert,canceltest,taskcheck,channeltest,bootcheck,soak,heap-invalid-free-test,heap-double-free-test,panic-test,fault-test,reboot
```

The current kernel image services UART RX through PL011 receive interrupts into
a fixed byte ring, then wakes the async shell reader. `r` or `R`
still triggers `watchdog_reset_now()` and re-enters the EEPROM boot path. Line
commands can be sent from the Mac:

```bash
./serial-command.sh status
./serial-command.sh protocol
./serial-command.sh --request-id 25 status
./serial-command.sh heap
./serial-command.sh queues
./serial-command.sh tasks
./serial-command.sh tasks2
./serial-command.sh kobjects
./serial-command.sh mailboxes
./serial-command.sh sendtest
./serial-command.sh supervisor
./serial-command.sh health
./serial-command.sh capcheck
./serial-command.sh events
./serial-command.sh diag
./serial-command.sh irqs
./serial-command.sh timers
./serial-command.sh memcheck
./serial-command.sh faults
./serial-command.sh retained
./serial-command.sh memmap
./serial-command.sh mmu
./serial-command.sh pools
./serial-command.sh poolcheck
./serial-command.sh heapfrag
./serial-command.sh poolstats
./serial-command.sh frames
./serial-command.sh heapcheck
./serial-command.sh framecheck
./serial-command.sh stress
./serial-command.sh frameprobe
./serial-command.sh bootcert
./serial-command.sh canceltest
./serial-command.sh taskcheck
./serial-command.sh bootcheck
./serial-command.sh soak
```

Expected response prefixes are `status uptime_ms=`, `heap total=`,
`queues ready=`, `task fast count=` / `task slow count=` / `task long count=`,
`diag version=v5`, `irqs total=`, `timers now=`, `memcheck ok=`, and
`faults seen=`. Runtime V6 adds `retained valid=` and `retained clear ok=1`.
Runtime V7 adds:

```text
memmap valid=1 regions=7 page_size=4096
frames total=14336 free=14336 used=0 reserved=0 base=0x800000 limit=0x4000000 selftest=1
```

Runtime V8 adds non-destructive allocator/frame guard checks:

```text
heapcheck ok=1 error=0 invalid_frees=0 double_frees=0 corruptions=0
framecheck ok=1 total=14336 free=14336 used=0 bad_frees=0 double_frees=0 error=0 stress=1
```

Runtime V9/V10/V11 add bounded pressure, guard-probe, and boot/soak checks:

```text
stress ok=1 heap=1 frames=1 heap_peak=62928 frame_peak=16 heap_leak=0 frame_leak=0
frameprobe ok=1 last_ok=1 bad_frees=1 double_frees=1 error=2 free=14336 used=0
bootcheck ok=1 memmap=1 heap=1 frames=1 retained_valid=0 heap_free=4188320 frame_free=14336
soak ok=1 rounds=3 failures=0 heap_peak=62928 frame_peak=16 heap_leak=0 frame_leak=0
```

Runtime V12 adds fixed object/task registry inspection:

```text
kobjects count=7 capacity=16 active=7 selftest=1
tasks2 count=4 capacity=8 selftest=1 task index=0 name=fast
```

Runtime V13 adds fixed mailbox queues and demo message exchange:

```text
rtv13 mail tx 0x0000000000000000
rtv13 mail rx 0x0000000000000000
mailboxes count=2 capacity=4 queue_capacity=8 selftest=1
sendtest ok=1 mailbox=1 sent=1 received=1 value=0x000000000000132d selftest=1
```

Runtime V14 adds deterministic task supervision:

```text
supervisor count=6 capacity=8 unhealthy=0 total_missed=0 selftest=1
health ok=1 supervised=6 unhealthy=0 total_missed=0 uptime_ms=...
```

Runtime V15 adds capability-tagged kernel object handles:

```text
handlecheck ok=1 handle_selftest=1 cap_selftest=1
kobjects count=... capacity=16 active=... selftest=1 handle_selftest=1 cap_selftest=1
 object index=0 id=1 handle=0x... generation=1 kind=runtime flags=0x1 caps=0x1 name=runtime
capcheck ok=1 inspect=1 denied=1 stale=1 last_error=2
```

Runtime V16 adds a fixed kernel event log ring:

```text
events count=11 capacity=64 lost=0 sequence=11 selftest=1
 event index=0 seq=1 kind=boot ticks=... a0=0x10 a1=0x0 a2=0x0
 event index=4 seq=5 kind=timer ticks=... a0=0x0 a1=0x337f980 a2=0x0
 event index=8 seq=9 kind=mailbox ticks=... a0=0x0 a1=0x0 a2=0x2
```

Runtime V17 adds a deterministic boot certificate:

```text
bootcert ok=1 version=17 memmap=1 heap=1 frames=1 retained_valid=0 kobjects=1 tasks=1 mailboxes=1 supervisor=1 events=1 events_lost=0 heap_free=4185344 frame_free=14336 uptime_ms=...
 event index=0 seq=1 kind=boot ticks=... a0=0x11 a1=0x0 a2=0x0
```

Runtime V18 adds cooperative cancellation tokens:

```text
bootcert ok=1 version=18 memmap=1 heap=1 frames=1 cancellations=1 retained_valid=0 kobjects=1 tasks=1 mailboxes=1 supervisor=1 events=1 events_lost=0 heap_free=... frame_free=14336 uptime_ms=...
canceltest ok=1 capacity=16 active=0 requested=1 completed=1 last_error=0 fast=... slow=... long=...
events count=15 capacity=64 lost=0 sequence=15 selftest=1
```

Runtime V19 adds structured Aether task spawn metadata:

```text
bootcert ok=1 version=19 memmap=1 heap=1 frames=1 taskspawns=1 cancellations=1 retained_valid=0 kobjects=1 tasks=1 mailboxes=1 supervisor=1 events=1 events_lost=0 heap_free=... frame_free=14336 uptime_ms=...
taskcheck ok=1 count=7 capacity=8 spawns=6 completions=0 root_parent=4294967295 selftest=1
tasks2 count=7 capacity=8 selftest=1 task index=0 name=fast
events count=16 capacity=64 lost=0 sequence=16 selftest=1
```

Runtime V20 adds a Swift-facing async channel wrapper over the fixed mailbox queues:

```text
bootcert ok=1 version=20 memmap=1 heap=1 frames=1 channels=1 taskspawns=1 cancellations=1 retained_valid=0 kobjects=1 tasks=1 mailboxes=1 supervisor=1 events=1 events_lost=0 heap_free=... frame_free=14336 uptime_ms=...
channeltest ok=1 mailbox=1 sent=1 received=1 value=0x000000000000c020 depth=0 selftest=1
kobjects count=12 capacity=16 active=12 selftest=1 handle_selftest=1 cap_selftest=1
events count=17 capacity=64 lost=0 sequence=17 selftest=1
```

Runtime V21 adds read-only MMU ownership introspection:

```text
bootcert ok=1 version=21 memmap=1 heap=1 frames=1 mmu=1 channels=1 taskspawns=1 cancellations=1 retained_valid=0 kobjects=1 tasks=1 mailboxes=1 supervisor=1 events=1 events_lost=0 heap_free=... frame_free=14336 uptime_ms=...
mmu ok=1 regions=4 entries=512 block_size=0x40000000 tcr=0x0000000000803519 mair=0x00000000000000ff selftest=1
```

Runtime V22 guarded typed pools add fixed C-owned pool storage beside the heap:

```text
bootcert ok=1 version=22 memmap=1 heap=1 frames=1 pools=1 mmu=1 channels=1 taskspawns=1 cancellations=1 retained_valid=0 kobjects=1 tasks=1 mailboxes=1 supervisor=1 events=1 events_lost=0 heap_free=... frame_free=14336 uptime_ms=...
poolcheck ok=1 pools=3/4 selftest=1 used=0 high_water=8 allocs=9 frees=9 failed=1 bad_frees=1 double_frees=1 last_error=0
pools count=3 capacity=4 selftest=1
```

Runtime V23 allocator/pool pressure telemetry adds heap fragmentation counters
and aggregate pool pressure counters:

```text
bootcert ok=1 version=23 memmap=1 heap=1 frames=1 pressure=1 pools=1 mmu=1 channels=1 taskspawns=1 cancellations=1 retained_valid=0 kobjects=1 tasks=1 mailboxes=1 supervisor=1 events=1 events_lost=0 heap_free=... frame_free=14336 uptime_ms=...
heapfrag ok=1 selftest=1 pressure=1 total=4194304 free=4184112 largest_free=4184112 smallest_free=4184112 free_blocks=1 allocated_blocks=25 fragmentation_permil=0 pressure_peak=62928 pressure_leak=0 pressure_free_blocks=1 pressure_largest_free=4184112
poolstats ok=1 pools=3/4 total_slots=24 used_slots=0 high_water_slots=8 failed_allocs=1 bad_frees=1 double_frees=1 selftest=1
```

Runtime V24 fixed driver registry adds a stable driver object surface for UART0,
CNTP, GIC, and watchdog:

```text
bootcert ok=1 version=24 memmap=1 heap=1 frames=1 drivers=1 pressure=1 pools=1 mmu=1 channels=1 taskspawns=1 cancellations=1 retained_valid=0 kobjects=1 tasks=1 mailboxes=1 supervisor=1 events=1 events_lost=0 heap_free=... frame_free=14336 uptime_ms=...
drivers count=4 capacity=4 selftest=1
drivercheck ok=1 count=4/4 uart_irq=16 timer_irq=689 gic_total=705 watchdog_resets=0 unknown_irq=0 selftest=1
```

Runtime V25 scriptable command protocol v2 adds request-wrapped shell calls for
agent control while preserving direct human commands:

```text
protocol version=2 request=req id_field=id cmd_field=cmd begin_end=1 errors=1 max_line=80
bootcert ok=1 version=25 protocol=1 memmap=1 heap=1 frames=1 drivers=1 pressure=1 pools=1 mmu=1 channels=1 taskspawns=1 cancellations=1 retained_valid=0 kobjects=1 tasks=1 mailboxes=1 supervisor=1 events=1 events_lost=0 heap_free=... frame_free=14336 uptime_ms=...
resp id=25 ok=1 cmd=status end
```

Runtime V26 host soak harness records repeated boot and self-test summaries from
the Mac side:

```text
soak summary cycle=3 command=bootcert id=2622 line=bootcert ok=1 version=25 protocol=1 ... events_lost=0
soak summary cycle=3 command=stress id=2623 line=stress ok=1 heap=1 frames=1 heap_leak=0 frame_leak=0
soak summary cycle=3 command=soak id=2624 line=soak ok=1 rounds=3 failures=0 heap_leak=0 frame_leak=0
soak summary cycle=3 command=events id=2625 line=events count=26 capacity=64 lost=0 sequence=26 selftest=1
soak result ok=1 cycles=3 completed=3 log=/tmp/aether-soak-v26.log
```

Runtime V27 panic taxonomy and symbolic retained records add stable numeric
panic/fault IDs beside the reason text:

```text
bootcert ok=1 version=27 taxonomy=1 protocol=1 memmap=1 heap=1 frames=1 drivers=1 pressure=1 pools=1 mmu=1 channels=1 taskspawns=1 cancellations=1 retained_valid=1 kobjects=1 tasks=1 mailboxes=1 supervisor=1 events=1 events_lost=0 heap_free=... frame_free=14336 uptime_ms=...
retained valid=1 kind=panic kind_id=1 category=1 reason_id=1 seq=1 esr=0x0 elr=0x0 far=0x0 reason=panic-test
retained valid=1 kind=fault kind_id=2 category=2 reason_id=2 seq=1 esr=0xf20000a5 elr=0x92968 far=0x0 reason=sync-fault
symbol address=0x92968 symbol_name=_kernel_trigger_sync_fault symbol_addr=0x92968 symbol_offset=0x0 macho=.build/release/Application
```

Runtime V28 Swift runtime dependency audit exposes the current Swift runtime
boundary from both the kernel shell and the host Mach-O audit:

```text
bootcert ok=1 version=28 runtime=1 taxonomy=1 protocol=1 memmap=1 heap=1 frames=1 drivers=1 pressure=1 pools=1 mmu=1 channels=1 taskspawns=1 cancellations=1 retained_valid=0 kobjects=1 tasks=1 mailboxes=1 supervisor=1 events=1 events_lost=0 heap_free=... frame_free=14336 uptime_ms=...
runtime ok=1 version=28 swift=6.3.2 source_hooks=10 linked_hooks=2 heap_shims=5 linked_heap_shims=3 required_symbols=5 audit=1
runtime-audit ok=1 version=28 source_hooks=10 linked_hooks=2 heap_shims=5 linked_heap_shims=3 required_symbols=5 present=5 missing=none macho=.build/release/Application
```

Runtime V29 agent-oriented control session keeps the V25 request envelope and
adds an `agent` command plus host `agent-session.sh` harness for scriptable
health classification:

```text
bootcert ok=1 version=29 agent=1 runtime=1 taxonomy=1 protocol=1 memmap=1 heap=1 frames=1 drivers=1 pressure=1 pools=1 mmu=1 channels=1 taskspawns=1 cancellations=1 retained_valid=0 kobjects=1 tasks=1 mailboxes=1 supervisor=1 events=1 events_lost=0 heap_free=... frame_free=14336 uptime_ms=...
agent ok=1 version=29 health=green bootcert=1 runtime=1 protocol=2 agent=1 events_lost=0 heap_free=... ready=... delayed=... sleepers=...
agent-session ok=1 version=29 health=green bootcert=1 runtime=1 stress=1 soak=1 events_lost=0 log=/tmp/aether-agent-session.log
```

Runtime V30 Swift-native kernel substrate certificate keeps the V25 request
envelope and adds a `certificate` command plus host `certificate-loop.sh`
harness for repeated milestone proof:

```text
bootcert ok=1 version=30 certificate=1 agent=1 runtime=1 taxonomy=1 protocol=1 memmap=1 heap=1 frames=1 drivers=1 pressure=1 pools=1 mmu=1 channels=1 taskspawns=1 cancellations=1 retained_valid=0 kobjects=1 tasks=1 mailboxes=1 supervisor=1 events=1 events_lost=0 heap_free=... frame_free=14336 uptime_ms=...
certificate ok=1 version=30 substrate=1 bootcert=1 agent=1 runtime=1 protocol=2 memory=1 objects=1 tasks=1 mailboxes=1 supervisor=1 handles=1 events=1 cancellations=1 channels=1 drivers=1 pressure=1 pools=1 mmu=1 swift=6.3.2 events_lost=0 heap_free=... frame_free=14336 uptime_ms=...
certificate-loop ok=1 version=30 cycles=3 completed=3 substrate=1 bootcert=1 agent=1 runtime=1 events_lost=0 log=/tmp/aether-certificate-loop.log
```

Runtime V31 preemptive scheduler substrate keeps the V25 request envelope and
adds a fixed C-owned scheduler timer client plus a `sched` proof command:

```text
bootcert ok=1 version=31 scheduler=1 certificate=1 agent=1 runtime=1 taxonomy=1 protocol=1 memmap=1 heap=1 frames=1 drivers=1 pressure=1 pools=1 mmu=1 channels=1 taskspawns=1 cancellations=1 retained_valid=0 kobjects=1 tasks=1 mailboxes=1 supervisor=1 events=1 events_lost=0 heap_free=... frame_free=14336 uptime_ms=...
certificate ok=1 version=31 substrate=1 bootcert=1 scheduler=1 agent=1 runtime=1 protocol=2 memory=1 objects=1 tasks=1 mailboxes=1 supervisor=1 handles=1 events=1 cancellations=1 channels=1 drivers=1 pressure=1 pools=1 mmu=1 swift=6.3.2 events_lost=0 heap_free=... frame_free=14336 uptime_ms=...
sched ok=1 version=31 active=1 cores=1 core=0 interval_ticks=2700000 ticks=... irq_ticks=... preemptions=... runqueue=0/8 enqueues=4 dequeues=4 selftest=1
```

Runtime V32 SMP secondary-core bring-up keeps secondaries out of Swift and adds
a `cores` proof command for the fixed C-owned per-core records:

```text
bootcert ok=1 version=32 smp=1 scheduler=1 certificate=1 agent=1 runtime=1 taxonomy=1 protocol=1 memmap=1 heap=1 frames=1 drivers=1 pressure=1 pools=1 mmu=1 channels=1 taskspawns=1 cancellations=1 retained_valid=0 kobjects=1 tasks=1 mailboxes=1 supervisor=1 events=1 events_lost=0 heap_free=... frame_free=14336 uptime_ms=...
certificate ok=1 version=32 substrate=1 bootcert=1 smp=1 scheduler=1 agent=1 runtime=1 protocol=2 memory=1 objects=1 tasks=1 mailboxes=1 supervisor=1 handles=1 events=1 cancellations=1 channels=1 drivers=1 pressure=1 pools=1 mmu=1 swift=6.3.2 events_lost=0 heap_free=... frame_free=14336 uptime_ms=...
cores ok=1 version=32 capacity=4 online=4 mask=0xf primary=0 release=0xe selftest=1 core0=1 entries0=1 heartbeat0=1 core1=1 entries1=1 heartbeat1=... core2=1 entries2=1 heartbeat2=... core3=1 entries3=1 heartbeat3=...
```

Runtime V33 atomics, spinlocks, and per-core run queues adds the first bounded
cross-core synchronization substrate plus `locks` and `runqueues` proof commands:

```text
bootcert ok=1 version=33 atomics=1 locks=1 queues=1 smp=1 scheduler=1 certificate=1 agent=1 runtime=1 taxonomy=1 protocol=1 memmap=1 heap=1 frames=1 drivers=1 pressure=1 pools=1 mmu=1 channels=1 taskspawns=1 cancellations=1 retained_valid=0 kobjects=1 tasks=1 mailboxes=1 supervisor=1 events=1 events_lost=0 heap_free=... frame_free=14336 uptime_ms=...
certificate ok=1 version=33 substrate=1 bootcert=1 atomics=1 locks=1 queues=1 smp=1 scheduler=1 agent=1 runtime=1 protocol=2 memory=1 objects=1 tasks=1 mailboxes=1 supervisor=1 handles=1 events=1 cancellations=1 channels=1 drivers=1 pressure=1 pools=1 mmu=1 swift=6.3.2 events_lost=0 heap_free=... frame_free=14336 uptime_ms=...
locks ok=1 version=33 atomics=1 spinlocks=1 acquisitions=2 contentions=0 selftest=1
runqueues ok=1 version=33 cores=4 capacity=8 total=0 core0=0 core1=0 core2=0 core3=0 enqueues0=8 dequeues0=8 selftest=1
```

Runtime V34 timer-driven SMP scheduler dispatch routes bounded dispatch tokens
through each online per-core queue on the scheduler tick and adds a `sched2`
proof command:

```text
bootcert ok=1 version=34 preemptive=1 smp_scheduler=1 atomics=1 locks=1 queues=1 smp=1 scheduler=1 certificate=1 agent=1 runtime=1 taxonomy=1 protocol=1 memmap=1 heap=1 frames=1 drivers=1 pressure=1 pools=1 mmu=1 channels=1 taskspawns=1 cancellations=1 retained_valid=0 kobjects=1 tasks=1 mailboxes=1 supervisor=1 events=1 events_lost=0 heap_free=... frame_free=14336 uptime_ms=...
certificate ok=1 version=34 substrate=1 bootcert=1 preemptive=1 smp_scheduler=1 atomics=1 locks=1 queues=1 smp=1 scheduler=1 agent=1 runtime=1 protocol=2 memory=1 objects=1 tasks=1 mailboxes=1 supervisor=1 handles=1 events=1 cancellations=1 channels=1 drivers=1 pressure=1 pools=1 mmu=1 swift=6.3.2 events_lost=0 heap_free=... frame_free=14336 uptime_ms=...
sched2 ok=1 version=34 preemptive=1 smp_scheduler=1 active=1 cores=4 online=4 dispatches=548 routes=548 min=137 max=137 imbalance=0 core0=137 core1=137 core2=137 core3=137 selftest=1
```

Runtime V35 secondary-owned scheduler workers keep secondaries in C-only code,
but let cores 1-3 drain V35 worker tokens from their own bounded queues:

```text
bootcert ok=1 version=35 secondary_workers=1 preemptive=1 smp_scheduler=1 atomics=1 locks=1 queues=1 smp=1 scheduler=1 certificate=1 agent=1 runtime=1 taxonomy=1 protocol=1 memmap=1 heap=1 frames=1 drivers=1 pressure=1 pools=1 mmu=1 channels=1 taskspawns=1 cancellations=1 retained_valid=0 kobjects=1 tasks=1 mailboxes=1 supervisor=1 events=1 events_lost=0 heap_free=... frame_free=14336 uptime_ms=...
certificate ok=1 version=35 substrate=1 bootcert=1 secondary_workers=1 preemptive=1 smp_scheduler=1 atomics=1 locks=1 queues=1 smp=1 scheduler=1 agent=1 runtime=1 protocol=2 memory=1 objects=1 tasks=1 mailboxes=1 supervisor=1 handles=1 events=1 cancellations=1 channels=1 drivers=1 pressure=1 pools=1 mmu=1 swift=6.3.2 events_lost=0 heap_free=... frame_free=14336 uptime_ms=...
sched3 ok=1 version=35 secondary_workers=1 active=1 cores=4 online=4 worker_drains=3 worker_idles=1396994 min=1 max=1 imbalance=0 core0=0 core1=1 core2=1 core3=1 selftest=1
```

Runtime V36 timer-fed secondary scheduler workers feed bounded worker tokens
from the timer IRQ to secondary per-core queues, and cores 1-3 drain them in
their C-only worker loops. The V36 hardware proof closed on 2026-06-06:

```text
bootcert ok=1 version=36 worker_feed=1 secondary_workers=1 preemptive=1 smp_scheduler=1 atomics=1 locks=1 queues=1 smp=1 scheduler=1 certificate=1 agent=1 runtime=1 taxonomy=1 protocol=1 memmap=1 heap=1 frames=1 drivers=1 pressure=1 pools=1 mmu=1 channels=1 taskspawns=1 cancellations=1 retained_valid=0 kobjects=1 tasks=1 mailboxes=1 supervisor=1 events=1 events_lost=0 heap_free=4184112 frame_free=14336 uptime_ms=3919
certificate ok=1 version=36 substrate=1 bootcert=1 worker_feed=1 secondary_workers=1 preemptive=1 smp_scheduler=1 atomics=1 locks=1 queues=1 smp=1 scheduler=1 agent=1 runtime=1 protocol=2 memory=1 objects=1 tasks=1 mailboxes=1 supervisor=1 handles=1 events=1 cancellations=1 channels=1 drivers=1 pressure=1 pools=1 mmu=1 swift=6.3.2 events_lost=0 heap_free=4184112 frame_free=14336 uptime_ms=7334
sched4 ok=1 version=36 worker_feed=1 secondary_workers=1 feeds=708 drains=711 drops=0 gap=0 feed_imbalance=0 drain_imbalance=0 core0_feed=0 core1_feed=236 core2_feed=236 core3_feed=236 core0_drain=0 core1_drain=237 core2_drain=237 core3_drain=237 selftest=1
```

The 3-cycle repeat kept `sched4` at `693/696`, `684/687`, and `684/687`
feeds/drains with `drops=0 gap=0`; `sched2` stayed balanced and `runqueues`
stayed `total=0`.

Runtime V38 secondary scheduler wake protocol keeps Swift execution on core 0,
parks secondary C-only scheduler loops with WFE between ticks, and wakes them
with bounded SEV signals when timer-fed scheduler jobs are enqueued. The V38
hardware proof closed on 2026-06-06. The live Pi run and a clean 3-cycle repeat
kept `bootcert`/`certificate` at `wake=1`, `sched6 ok=1`, `runqueues total=0`,
and `events_lost=0`. WFE wait/wake imbalance is telemetry, not a pass/fail
gate, because the A72 can resume WFE for architectural events beyond this
scheduler SEV path:

```text
bootcert ok=1 version=38 wake=1 job_exec=1 worker_feed=1 secondary_workers=1 preemptive=1 smp_scheduler=1 atomics=1 locks=1 queues=1 smp=1 scheduler=1 certificate=1 agent=1 runtime=1 taxonomy=1 protocol=1 memmap=1 heap=1 frames=1 drivers=1 pressure=1 pools=1 mmu=1 channels=1 taskspawns=1 cancellations=1 retained_valid=0 kobjects=1 tasks=1 mailboxes=1 supervisor=1 events=1 events_lost=0 heap_free=4184112 frame_free=14336 uptime_ms=3679
certificate ok=1 version=38 substrate=1 bootcert=1 wake=1 job_exec=1 worker_feed=1 secondary_workers=1 preemptive=1 smp_scheduler=1 atomics=1 locks=1 queues=1 smp=1 scheduler=1 agent=1 runtime=1 protocol=2 memory=1 objects=1 tasks=1 mailboxes=1 supervisor=1 handles=1 events=1 cancellations=1 channels=1 drivers=1 pressure=1 pools=1 mmu=1 swift=6.3.2 events_lost=0 heap_free=4184112 frame_free=14336 uptime_ms=7072
sched6 ok=1 version=38 wake=1 job_exec=1 worker_feed=1 signals=825 mask=0xe targets=825 waits=84020214 wakes=84028069 gap=1 imbalance=6818555 core0_wait=0 core1_wait=30418819 core2_wait=23601879 core3_wait=30047539 core0_wake=0 core1_wake=30432773 core2_wake=23612725 core3_wake=30060963 selftest=1
```

Runtime V37 timer-fed secondary C scheduler jobs keep Swift execution on core
0, turn V36's timer-fed secondary worker tokens into typed C-only scheduler
jobs, and report execution/completion/checksum telemetry from secondary cores
1-3. The V37 hardware proof closed on 2026-06-06:

```text
bootcert ok=1 version=37 job_exec=1 worker_feed=1 secondary_workers=1 preemptive=1 smp_scheduler=1 atomics=1 locks=1 queues=1 smp=1 scheduler=1 certificate=1 agent=1 runtime=1 taxonomy=1 protocol=1 memmap=1 heap=1 frames=1 drivers=1 pressure=1 pools=1 mmu=1 channels=1 taskspawns=1 cancellations=1 retained_valid=0 kobjects=1 tasks=1 mailboxes=1 supervisor=1 events=1 events_lost=0 heap_free=4184112 frame_free=14336 uptime_ms=3580
certificate ok=1 version=37 substrate=1 bootcert=1 job_exec=1 worker_feed=1 secondary_workers=1 preemptive=1 smp_scheduler=1 atomics=1 locks=1 queues=1 smp=1 scheduler=1 agent=1 runtime=1 protocol=2 memory=1 objects=1 tasks=1 mailboxes=1 supervisor=1 handles=1 events=1 cancellations=1 channels=1 drivers=1 pressure=1 pools=1 mmu=1 swift=6.3.2 events_lost=0 heap_free=4184112 frame_free=14336 uptime_ms=6995
sched5 ok=1 version=37 job_exec=1 worker_feed=1 secondary_workers=1 executions=756 completions=756 noops=0 checksum=698517273110 gap=0 imbalance=0 core0_exec=0 core1_exec=252 core2_exec=252 core3_exec=252 core0_done=0 core1_done=252 core2_done=252 core3_done=252 selftest=1
```

The 3-cycle repeat kept `sched5` at `738/738`, `699/699`, and `699/699`
executions/completions with `noops=0 gap=0 imbalance=0`; `sched4` stayed at
`666/669`, `627/630`, and `630/633` feeds/drains with `drops=0 gap=0`;
`runqueues` stayed `total=0`.

`serial-probe.sh` sends one command and waits for a matching response line:

```bash
./serial-probe.sh status '^status uptime_ms=.*timer_mask='
./serial-probe.sh protocol '^protocol version=2 .*begin_end=1 .*errors=1'
./serial-probe.sh 'req id=25 cmd=status' '^resp id=25 ok=1 cmd=status end'
./serial-probe.sh bootcert '^bootcert ok=1 version=38 .*wake=1 .*job_exec=1 .*worker_feed=1 .*secondary_workers=1 .*preemptive=1 .*smp_scheduler=1 .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*certificate=1 .*agent=1 .*runtime=1 .*taxonomy=1 .*protocol=1 .*drivers=1 .*pressure=1 .*pools=1 .*mmu=1 .*channels=1 .*taskspawns=1 .*cancellations=1 .*events_lost=0'
./serial-probe.sh runtime '^runtime ok=1 version=28 .*source_hooks=10 .*linked_hooks=2 .*heap_shims=5 .*linked_heap_shims=3 .*required_symbols=5'
./serial-probe.sh agent '^agent ok=1 version=29 health=green .*bootcert=1 .*runtime=1 .*protocol=2 .*events_lost=0'
./serial-probe.sh 'req id=29 cmd=agent' '^resp id=29 ok=1 cmd=agent end'
./serial-probe.sh certificate '^certificate ok=1 version=38 substrate=1 .*bootcert=1 .*wake=1 .*job_exec=1 .*worker_feed=1 .*secondary_workers=1 .*preemptive=1 .*smp_scheduler=1 .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*agent=1 .*runtime=1 .*memory=1 .*objects=1 .*tasks=1 .*mailboxes=1 .*supervisor=1 .*handles=1 .*events=1 .*cancellations=1 .*channels=1 .*drivers=1 .*pressure=1 .*pools=1 .*mmu=1 .*events_lost=0'
./serial-probe.sh 'req id=30 cmd=certificate' '^resp id=30 ok=1 cmd=certificate end'
./serial-probe.sh sched '^sched ok=1 version=31 .*active=1 .*cores=1 .*core=0 .*ticks=[1-9][0-9]* .*irq_ticks=[1-9][0-9]* .*preemptions=[1-9][0-9]* .*selftest=1'
./serial-probe.sh 'req id=31 cmd=sched' '^resp id=31 ok=1 cmd=sched end'
./serial-probe.sh sched2 '^sched2 ok=1 version=34 .*preemptive=1 .*smp_scheduler=1 .*active=1 .*cores=4 .*online=4 .*dispatches=[1-9][0-9]* .*routes=[1-9][0-9]* .*imbalance=[0-9][0-9]* .*selftest=1'
./serial-probe.sh 'req id=35 cmd=sched2' '^resp id=35 ok=1 cmd=sched2 end'
./serial-probe.sh sched3 '^sched3 ok=1 version=35 .*secondary_workers=1 .*active=1 .*cores=4 .*online=4 .*worker_drains=[1-9][0-9]* .*worker_idles=[0-9][0-9]* .*imbalance=[0-9][0-9]* .*selftest=1'
./serial-probe.sh 'req id=36 cmd=sched3' '^resp id=36 ok=1 cmd=sched3 end'
./serial-probe.sh sched4 '^sched4 ok=1 version=36 .*worker_feed=1 .*secondary_workers=1 .*feeds=[1-9][0-9]* .*drains=[1-9][0-9]* .*selftest=1'
./serial-probe.sh 'req id=37 cmd=sched4' '^resp id=37 ok=1 cmd=sched4 end'
./serial-probe.sh sched5 '^sched5 ok=1 version=37 .*job_exec=1 .*worker_feed=1 .*secondary_workers=1 .*executions=[1-9][0-9]* .*completions=[1-9][0-9]* .*checksum=[1-9][0-9]* .*selftest=1'
./serial-probe.sh 'req id=38 cmd=sched5' '^resp id=38 ok=1 cmd=sched5 end'
./serial-probe.sh sched6 '^sched6 ok=1 version=38 .*wake=1 .*job_exec=1 .*worker_feed=1 .*signals=[1-9][0-9]* .*mask=0xe .*targets=[1-9][0-9]* .*waits=[1-9][0-9]* .*wakes=[1-9][0-9]* .*gap=[0-9][0-9]* .*imbalance=[0-9][0-9]* .*core0_wait=0 .*core1_wait=[1-9][0-9]* .*core2_wait=[1-9][0-9]* .*core3_wait=[1-9][0-9]* .*core0_wake=0 .*core1_wake=[1-9][0-9]* .*core2_wake=[1-9][0-9]* .*core3_wake=[1-9][0-9]* .*selftest=1'
./serial-probe.sh 'req id=39 cmd=sched6' '^resp id=39 ok=1 cmd=sched6 end'
./serial-probe.sh cores '^cores ok=1 version=32 .*capacity=4 .*online=4 .*mask=0xf .*primary=0 .*release=0xe .*selftest=1'
./serial-probe.sh 'req id=32 cmd=cores' '^resp id=32 ok=1 cmd=cores end'
./serial-probe.sh locks '^locks ok=1 version=33 .*atomics=1 .*spinlocks=1 .*selftest=1'
./serial-probe.sh 'req id=33 cmd=locks' '^resp id=33 ok=1 cmd=locks end'
./serial-probe.sh runqueues '^runqueues ok=1 version=33 .*cores=4 .*capacity=[1-9][0-9]* .*total=0 .*selftest=1'
./serial-probe.sh 'req id=34 cmd=runqueues' '^resp id=34 ok=1 cmd=runqueues end'
./serial-probe.sh canceltest '^canceltest ok=1 .*completed=1'
./serial-probe.sh taskcheck '^taskcheck ok=1 .*spawns='
./serial-probe.sh channeltest '^channeltest ok=1 .*received=1'
./serial-probe.sh mmu '^mmu ok=1 .*regions=4 .*block_size=0x40000000'
./serial-probe.sh poolcheck '^poolcheck ok=1 .*bad_frees=1 .*double_frees=1'
./serial-probe.sh pools '^pools count=.* capacity=.* selftest=1'
./serial-probe.sh heapfrag '^heapfrag ok=1 .*fragmentation_permil=.*pressure_largest_free='
./serial-probe.sh poolstats '^poolstats ok=1 .*total_slots=.*failed_allocs='
./serial-probe.sh drivers '^drivers count=4 capacity=4 selftest=1'
./serial-probe.sh drivercheck '^drivercheck ok=1 .*uart_irq=.*timer_irq=.*watchdog_resets='
./serial-probe.sh bootcheck '^bootcheck ok=1 .*frame_free='
./serial-probe.sh kobjects '^kobjects count=.* active=.* handle_selftest=1 .*cap_selftest=1'
./serial-probe.sh tasks2 '^tasks2 count=.* task index=.*fast'
./serial-probe.sh mailboxes '^mailboxes count=.* queue_capacity='
./serial-probe.sh sendtest '^sendtest ok=1 .*received=1'
./serial-probe.sh supervisor '^supervisor count=.* unhealthy=0'
./serial-probe.sh health '^health ok=1 .*supervised='
./serial-probe.sh capcheck '^capcheck ok=1 .*denied=1 .*stale=1'
./serial-probe.sh events '^events count=.* lost=0 .*selftest=1'
```

`panic-test` and `fault-test` are intentionally destructive: each writes a
cache-cleaned retained record, prints its diagnostic line, watchdog-resets the
Pi, and then the next boot can report the prior event via `retained`. Do not
use them as part of the normal iteration proof unless you are deliberately
testing retained panic/fault reporting. `heap-invalid-free-test` and
`heap-double-free-test` are also destructive allocator guard probes.
Runtime V27 proof used `panic-test` and `fault-test`; the panic record reported
`retained valid=1 kind=panic kind_id=1 category=1 reason_id=1`, the fault record
reported `kind_id=2 category=2 reason_id=2`, and the retained fault ELR mapped
with `symbol address=0x92968 symbol_name=_kernel_trigger_sync_fault`.

## 4. Serial Monitor on macOS
Open a terminal on macOS to monitor the serial output:

1. **Locate the device path:**
   ```bash
   ls /dev/cu.usbserial-*
   ```
2. **Open the serial connection:** Run the following command (replace `XXXX` with the identifier found above):
   ```bash
   screen /dev/cu.usbserial-XXXX 115200
   ```
   *(Configured for 115200 baud, 8 data bits, no parity, 1 stop bit / 8-N-1).*
3. **Exit the screen session:** Press `Ctrl-A`, then `K`, then press `Y` to confirm.

## 5. Boot & Expected Output
1. Insert the SD card back into the Raspberry Pi 4B.
2. Connect the Raspberry Pi's USB-C power supply.
3. Within a couple of seconds, the serial terminal should print the kernel's banner, `CurrentEL = 0x0000000000000004`, Runtime V4 through V38 markers, repeating `rtv2 fast/slow/long` lines, `bootcheck ok=1`, `canceltest ok=1`, `taskcheck ok=1`, `channeltest ok=1`, `mmu ok=1`, `poolcheck ok=1`, `heapfrag ok=1`, `poolstats ok=1`, `drivers count=4 capacity=4 selftest=1`, `drivercheck ok=1`, `protocol version=2`, `agent ok=1 version=29 health=green`, `certificate ok=1 version=38 substrate=1`, `sched ok=1 version=31`, `sched2 ok=1 version=34`, `sched3 ok=1 version=35`, `sched4 ok=1 version=36`, `sched5 ok=1 version=37`, `sched6 ok=1 version=38`, `cores ok=1 version=32`, `locks ok=1 version=33`, `runqueues ok=1 version=33`, and `shell ready`.
4. **Liveness Check:** Current liveness is the serial Runtime V38 cadence output plus UART shell diagnostic responses, especially `bootcert ok=1 version=38 wake=1 job_exec=1 worker_feed=1 secondary_workers=1 preemptive=1 smp_scheduler=1 atomics=1 locks=1 queues=1 smp=1 scheduler=1 certificate=1 agent=1 runtime=1`, `certificate ok=1 version=38 substrate=1 bootcert=1 wake=1 job_exec=1 worker_feed=1 secondary_workers=1 preemptive=1 smp_scheduler=1 atomics=1 locks=1 queues=1 smp=1 scheduler=1`, `sched ok=1 version=31 active=1`, `sched2 ok=1 version=34 preemptive=1 smp_scheduler=1 active=1 cores=4 online=4`, `sched3 ok=1 version=35 secondary_workers=1 active=1 cores=4 online=4`, `sched4 ok=1 version=36 worker_feed=1 secondary_workers=1`, `sched5 ok=1 version=37 job_exec=1 worker_feed=1 secondary_workers=1`, `sched6 ok=1 version=38 wake=1 job_exec=1 worker_feed=1`, `cores ok=1 version=32 capacity=4 online=4`, `locks ok=1 version=33 atomics=1 spinlocks=1`, `runqueues ok=1 version=33 cores=4`, `agent ok=1 version=29 health=green`, `agent-session ok=1 version=29 health=green`, `protocol version=2`, `resp id=30 ok=1 cmd=certificate end`, `resp id=31 ok=1 cmd=sched end`, `resp id=35 ok=1 cmd=sched2 end`, `resp id=36 ok=1 cmd=sched3 end`, `resp id=37 ok=1 cmd=sched4 end`, `resp id=38 ok=1 cmd=sched5 end`, `resp id=39 ok=1 cmd=sched6 end`, `resp id=33 ok=1 cmd=locks end`, `resp id=34 ok=1 cmd=runqueues end`, `drivercheck ok=1`, `canceltest ok=1`, `taskcheck ok=1`, `channeltest ok=1`, `mmu ok=1`, `poolcheck ok=1`, `heapfrag ok=1`, and `poolstats ok=1`. GPIO42 ACT-LED blink code remains as historical bring-up support, but the current app does not drive it.

## 6. Troubleshooting
* **No output:**
  * Double-check that the adapter voltage jumper is set to 3.3V.
  * Verify TX/RX crossover wiring (swap RX and TX wires on the Pi header and test again if silent).
  * Confirm that `config.txt` includes `arm_64bit=1`, `kernel=kernel8.img`, `enable_uart=1`, `dtoverlay=disable-bt`, `init_uart_clock=48000000`, and `enable_gic=1`.
  * Confirm the serial client baud rate is set to 115200.
  * Verify `kernel8.img` was successfully copied to the boot partition.
  * For netboot, confirm `serve-netboot.sh` is running, `en0` has `10.42.0.1`, and the EEPROM boot order is network-first if the SD card is still inserted.
* **Garbage/Corrupted characters:** The baud rate is mismatched. Ensure the terminal is set to 115200.
* **Nothing at all (including no LEDs):** Re-seat the SD card and verify that the Pi is getting stable power through its USB-C port.
