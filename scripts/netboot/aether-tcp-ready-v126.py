#!/usr/bin/env python3
# aether-tcp-ready-v126 — stall/probe the V119 TCP echo helper.
# pgrep-alive is not accept-ready: a hung recv still matches pgrep.
import socket
import sys
import time

HOST = "10.42.0.1"
PORT = 41241
PROBE = b"A126LIVE"


def probe() -> bool:
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(2)
    try:
        s.connect((HOST, PORT))
        s.sendall(PROBE)
        return s.recv(16) == PROBE
    except OSError:
        return False
    finally:
        s.close()


def stall() -> bool:
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(4)
    try:
        s.connect((HOST, PORT))
        time.sleep(2.2)
    except OSError:
        pass
    finally:
        s.close()
    return True


cmd = sys.argv[1] if len(sys.argv) > 1 else "probe"
ok = probe() if cmd == "probe" else stall()
sys.exit(0 if ok else 1)
