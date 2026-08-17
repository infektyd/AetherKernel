#!/usr/bin/env python3
# aether-tcp-echo-v119 — echo TCP payloads on 10.42.0.1:41241 for V119/V126.
# Recv timeout is required: a half-open SYN leaves accept() stuck and
# later boots inherit genet15=0. listen(16) + 2s timeouts keep accept live.
import socket

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("10.42.0.1", 41241))
s.listen(16)
s.settimeout(2)
while True:
    try:
        conn, addr = s.accept()
    except socket.timeout:
        continue
    try:
        conn.settimeout(2)
        data = conn.recv(256)
        if data:
            conn.sendall(data)
    except OSError:
        pass
    finally:
        conn.close()
