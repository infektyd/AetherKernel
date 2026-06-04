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
   enable_uart=1
   dtoverlay=disable-bt
   ```
   *(Note: `dtoverlay=disable-bt` routes the high-quality PL011 UART0 to GPIO14/15 instead of the mini-UART).*
3. **Flash kernel:** Copy the newly built `kernel8.img` onto the root of the boot partition.

## 3. Serial Monitor on macOS
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

## 4. Boot & Expected Output
1. Insert the SD card back into the Raspberry Pi 4B.
2. Connect the Raspberry Pi's USB-C power supply.
3. Within a couple of seconds, the serial terminal should print the kernel's banner.
4. **Liveness Check:** A blinking green ACT LED on the Pi serves as a secondary liveness indicator.

## 5. Troubleshooting
* **No output:**
  * Double-check that the adapter voltage jumper is set to 3.3V.
  * Verify TX/RX crossover wiring (swap RX and TX wires on the Pi header and test again if silent).
  * Confirm that `config.txt` includes `enable_uart=1` and `dtoverlay=disable-bt`.
  * Confirm the serial client baud rate is set to 115200.
  * Verify `kernel8.img` was successfully copied to the boot partition.
* **Garbage/Corrupted characters:** The baud rate is mismatched. Ensure the terminal is set to 115200.
* **Nothing at all (including no LEDs):** Re-seat the SD card and verify that the Pi is getting stable power through its USB-C port.
