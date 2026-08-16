#!/usr/bin/env python3
"""Append UART bytes to $AETHER_SERIAL_LOG. Bench helper for net-iterate."""
import os
import select
import sys
import termios

port = os.environ.get("AETHER_SERIAL_PORT", "/dev/cu.usbserial-B0044J1V")
log_path = os.environ.get("AETHER_SERIAL_LOG", "/tmp/aether-serial.log")

fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
try:
    attrs = termios.tcgetattr(fd)
    attrs[0] = 0
    attrs[1] = 0
    attrs[2] = termios.CREAD | termios.CLOCAL | termios.CS8
    attrs[3] = 0
    attrs[4] = termios.B115200
    attrs[5] = termios.B115200
    attrs[6][termios.VMIN] = 0
    attrs[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    with open(log_path, "ab", buffering=0) as log:
        while True:
            ready, _, _ = select.select([fd], [], [], 1.0)
            if not ready:
                continue
            data = os.read(fd, 4096)
            if data:
                log.write(data)
                log.flush()
finally:
    os.close(fd)
