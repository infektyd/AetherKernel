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
2. **Install the TFTP host dependency once:**
   ```bash
   brew install dnsmasq
   ```
3. **Seed the TFTP tree.** If the SD boot partition is mounted on the Mac, copy
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
4. **Serve TFTP in the foreground:**
   ```bash
   ./serve-netboot.sh en0
   ```
   The script runs `dnsmasq` in TFTP-only mode. It does not advertise DHCP; the
   EEPROM static-IP config supplies the Pi's IP and server IP. UDP port 69
   requires root; if sudo credentials are not cached, macOS will reject startup
   until you run it from an admin-authenticated terminal. Leave dnsmasq blocksize
   negotiation enabled by default. If bootloader logs repeatedly show
   `failed sending .../start4.elf`, `timeout sending .../start4.elf`, or
   `Read aether/start4.elf failed`, restart `serve-netboot.sh` with
   `AETHER_TFTP_NO_BLOCKSIZE=1` for a clean A/B test before suspecting the
   kernel image.

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
AetherKernel banner plus Runtime V16 marker, `rtv2 fast/slow/long` zero-lines,
the expanded `shell ready` command list, and shell probes for `status`,
`bootcheck`, `stress`, `soak`, `kobjects`, `tasks2`, `mailboxes`, `sendtest`,
`supervisor`, `health`, `capcheck`, and `events`.

The first reset after adding this workflow is still physical if the currently
running SD image predates the serial reset hook. For that first proof, use the
guided harness:

```bash
./netboot-doctor.sh
```

It checks the Mac Ethernet address, confirms `dnsmasq` is serving the TFTP root,
stages the latest image, proves local TFTP access, then tells you exactly when
to reset or power-cycle the Pi. After the staged image has booted once, the
reset step is handled by:

```bash
./serial-reset.sh
```

The expected serial flow is bootloader `TFTP_GET` lines, then the AetherKernel
banner, padded `CurrentEL`, repeating `rtv2 fast/slow/long` cadences, the
Runtime V5 through V15 markers, and:

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
handlecheck ok=1 handle_selftest=1 cap_selftest=1
shell ready commands=help,status,heap,queues,tasks,tasks2,kobjects,mailboxes,sendtest,supervisor,health,capcheck,events,diag,irqs,timers,memcheck,faults,retained,retained-clear,memmap,frames,heapcheck,framecheck,stress,frameprobe,bootcheck,soak,heap-invalid-free-test,heap-double-free-test,panic-test,fault-test,reboot
```

The current kernel image services UART RX through PL011 receive interrupts into
a fixed byte ring, then wakes the async shell reader. `r` or `R`
still triggers `watchdog_reset_now()` and re-enters the EEPROM boot path. Line
commands can be sent from the Mac:

```bash
./serial-command.sh status
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
./serial-command.sh frames
./serial-command.sh heapcheck
./serial-command.sh framecheck
./serial-command.sh stress
./serial-command.sh frameprobe
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

`serial-probe.sh` sends one command and waits for a matching response line:

```bash
./serial-probe.sh status '^status uptime_ms=.*timer_mask='
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
3. Within a couple of seconds, the serial terminal should print the kernel's banner, `CurrentEL = 0x0000000000000004`, Runtime V4 through V11 markers, repeating `rtv2 fast/slow/long` lines, `bootcheck ok=1`, and `shell ready`.
4. **Liveness Check:** Current liveness is the serial Runtime V11 cadence output plus UART shell diagnostic responses. GPIO42 ACT-LED blink code remains as historical bring-up support, but the current app does not drive it.

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
