#!/usr/bin/env python3
# aether-udp-echo-v118 — echo UDP payloads on 10.42.0.1:41240 for V118.
import socket

s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind(("10.42.0.1", 41240))
while True:
    data, addr = s.recvfrom(256)
    s.sendto(data, addr)
