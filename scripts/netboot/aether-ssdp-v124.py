#!/usr/bin/env python3
# aether-ssdp-v124 — SSDP M-SEARCH on 10.42.0.1:41252 (0xA124) for V124.
import socket

PORT = 41252
ST = b"urn:aether:device:v124"
BODY = bytes.fromhex("A1240001A1240002A1240003A1240004")
RESP = b"HTTP/1.1 200 OK\r\nST: urn:aether:device:v124\r\n\r\n" + BODY

s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind(("10.42.0.1", PORT))
while True:
    data, addr = s.recvfrom(512)
    if b"M-SEARCH" in data and ST in data:
        s.sendto(RESP, addr)
