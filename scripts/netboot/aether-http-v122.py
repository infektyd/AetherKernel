#!/usr/bin/env python3
# aether-http-v122 — HTTP/1.0 GET /aether/v122.txt on 10.42.0.1:41250 for V122.
import socket

BODY = bytes.fromhex("A1220001A1220002A1220003A1220004")
RESP = b"HTTP/1.0 200 OK\r\nContent-Length: 16\r\n\r\n" + BODY

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("10.42.0.1", 41250))
s.listen(1)
while True:
    conn, addr = s.accept()
    try:
        data = b""
        conn.settimeout(2.0)
        while b"\r\n\r\n" not in data and len(data) < 512:
            chunk = conn.recv(256)
            if not chunk:
                break
            data += chunk
        if b"GET /aether/v122.txt" in data:
            conn.sendall(RESP)
    except OSError:
        pass
    finally:
        conn.close()
