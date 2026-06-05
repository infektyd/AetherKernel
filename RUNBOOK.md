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
AetherKernel banner plus Runtime V4 marker, `rtv2 fast/slow/long` zero-lines,
and `shell ready`.

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
banner, padded `CurrentEL`, repeating `rtv2 fast/slow/long` cadences, and:

```text
shell ready commands=help,status,heap,queues,tasks,reboot
```

The current kernel image services UART RX through PL011 receive interrupts into
a fixed byte ring, then wakes the Runtime V4 async shell reader. `r` or `R`
still triggers `watchdog_reset_now()` and re-enters the EEPROM boot path. Line
commands can be sent from the Mac:

```bash
./serial-command.sh status
./serial-command.sh heap
./serial-command.sh queues
./serial-command.sh tasks
```

Expected response prefixes are `status uptime_ms=`, `heap total=`,
`queues ready=`, and `task fast count=` / `task slow count=` / `task long count=`.

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
3. Within a couple of seconds, the serial terminal should print the kernel's banner, `CurrentEL = 0x0000000000000004`, `runtime v4: irq-backed uart shell`, repeating `rtv2 fast/slow/long` lines, and `shell ready`.
4. **Liveness Check:** Current liveness is the serial Runtime V4 cadence output plus UART shell responses. GPIO42 ACT-LED blink code remains as historical bring-up support, but the current app does not drive it.

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
