#!/usr/bin/env python3
# aether-sntp-v123 — SNTP server on 10.42.0.1:41251 (0xA123) for V123.
import socket

PORT = 41251
TX_MAGIC = bytes.fromhex("A1230001A1230002")
REPLY_TX = bytes.fromhex("A1230003A1230004")

s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind(("10.42.0.1", PORT))
while True:
    data, addr = s.recvfrom(256)
    if len(data) < 48:
        continue
    if (data[0] & 0x07) != 3:
        continue
    if data[40:48] != TX_MAGIC:
        continue
    reply = bytearray(48)
    reply[0] = 0x24
    reply[1] = 1
    reply[24:32] = data[40:48]
    reply[40:48] = REPLY_TX
    s.sendto(bytes(reply), addr)
