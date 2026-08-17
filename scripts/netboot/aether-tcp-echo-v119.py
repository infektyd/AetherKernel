#!/usr/bin/env python3
# aether-tcp-echo-v119 — echo TCP payloads on 10.42.0.1:41241 for V119.
import socket

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("10.42.0.1", 41241))
s.listen(1)
while True:
    conn, addr = s.accept()
    try:
        data = conn.recv(256)
        if data:
            conn.sendall(data)
    finally:
        conn.close()
