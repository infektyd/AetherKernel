#!/usr/bin/env python3
# aether-mdns-v121 — publish A aether-v121.local and answer on 5353 for V121.
import os
import socket
import struct
import subprocess
import time

NAME = b"\x0caether-v121\x05local\x00"


def ensure_dnssd() -> None:
    try:
        out = subprocess.check_output(["pgrep", "-f", "dns-sd -P aether-v121"], text=True)
        if out.strip():
            return
    except subprocess.CalledProcessError:
        pass
    log = open("/tmp/aether-dnssd-v121.log", "ab")
    subprocess.Popen(
        [
            "dns-sd",
            "-P",
            "aether-v121",
            "_aetherv121._udp",
            "local",
            "9",
            "aether-v121.local",
            "10.42.0.1",
        ],
        stdout=log,
        stderr=subprocess.STDOUT,
        start_new_session=True,
    )
    time.sleep(0.8)


ensure_dnssd()
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
s.bind(("", 5353))
s.setsockopt(
    socket.IPPROTO_IP,
    socket.IP_ADD_MEMBERSHIP,
    struct.pack("4s4s", socket.inet_aton("224.0.0.251"), socket.inet_aton("10.42.0.1")),
)
print("aether-mdns-v121 listening pid=%d" % os.getpid(), flush=True)
while True:
    data, addr = s.recvfrom(2048)
    if len(data) < 12 or NAME not in data:
        continue
    if data[2] & 0x80:
        continue
    print("query from %s len=%d" % (addr, len(data)), flush=True)
    qoff = data.find(NAME)
    q = data[qoff : qoff + len(NAME) + 4]
    resp = data[:2] + b"\x84\x00\x00\x01\x00\x01\x00\x00\x00\x00"
    resp += q
    resp += b"\xc0\x0c\x00\x01\x00\x01\x00\x00\x00\x78\x00\x04\x0a\x2a\x00\x01"
    s.sendto(resp, addr)
    s.sendto(resp, ("224.0.0.251", 5353))
