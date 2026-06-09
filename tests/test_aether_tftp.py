import importlib.util
import pathlib
import socket
import sys
import threading


ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("aether_tftp", ROOT / "scripts/netboot/aether_tftp.py")
assert SPEC is not None
aether_tftp = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules["aether_tftp"] = aether_tftp
SPEC.loader.exec_module(aether_tftp)


def rrq(filename: str, *options: str) -> bytes:
    parts = [filename.encode("ascii"), b"octet"]
    parts.extend(opt.encode("ascii") for opt in options)
    return b"\x00\x01" + b"\x00".join(parts) + b"\x00"


def ack(block: int) -> bytes:
    return b"\x00\x04" + block.to_bytes(2, "big")


class ScriptedSocket:
    def __init__(self, responses: list[tuple[bytes, tuple[str, int]] | BaseException]) -> None:
        self.responses = list(responses)
        self.sent: list[tuple[bytes, tuple[str, int]]] = []

    def sendto(self, packet: bytes, client: tuple[str, int]) -> None:
        self.sent.append((packet, client))

    def recvfrom(self, _size: int) -> tuple[bytes, tuple[str, int]]:
        if not self.responses:
            raise TimeoutError()
        response = self.responses.pop(0)
        if isinstance(response, BaseException):
            raise response
        return response


def test_parse_rrq_records_option_negotiation() -> None:
    request = aether_tftp.parse_rrq(rrq("aether/start4.elf", "blksize", "1468", "tsize", "0"))

    assert request.filename == "aether/start4.elf"
    assert request.mode == "octet"
    assert request.options == {"blksize": "1468", "tsize": "0"}


def test_oack_clamps_blksize_and_reports_transfer_size() -> None:
    packet = aether_tftp.oack_packet({"blksize": "512", "tsize": "600"})

    assert packet == b"\x00\x06blksize\x00512\x00tsize\x00600\x00"


def test_resolve_request_path_rejects_traversal(tmp_path: pathlib.Path) -> None:
    root = tmp_path / "root"
    root.mkdir()
    (root / "ok.bin").write_bytes(b"ok")

    assert aether_tftp.resolve_request_path(root, "ok.bin") == root / "ok.bin"
    assert aether_tftp.resolve_request_path(root, "../secret") is None
    assert aether_tftp.resolve_request_path(root, "/../secret") is None


def test_make_logger_appends_to_log_file(tmp_path: pathlib.Path) -> None:
    log_file = tmp_path / "aether-tftp.log"
    logger = aether_tftp.make_logger(log_file)

    logger("aether-tftp test line")

    assert log_file.read_text() == "aether-tftp test line\n"


def test_transfer_sockets_default_to_wildcard_bind(tmp_path: pathlib.Path) -> None:
    server = aether_tftp.ReadOnlyTFTPServer(root=tmp_path, host="10.42.0.1", port=0)

    assert server.transfer_host == "0.0.0.0"
    assert not server.single_port


def test_readonly_tftp_server_transfers_512_byte_blocks(tmp_path: pathlib.Path) -> None:
    root = tmp_path / "root"
    root.mkdir()
    payload = bytes([i % 251 for i in range(600)])
    (root / "kernel8.img").write_bytes(payload)

    server = aether_tftp.ReadOnlyTFTPServer(
        root=root,
        host="127.0.0.1",
        port=0,
        block_size=512,
        timeout_s=1.0,
        log=lambda _line: None,
    )
    server.bind()
    thread = threading.Thread(target=server.serve, kwargs={"max_requests": 1}, daemon=True)
    thread.start()

    client = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    client.settimeout(2.0)
    try:
        client.sendto(rrq("kernel8.img", "blksize", "1468"), ("127.0.0.1", server.port))
        packet, transfer_addr = client.recvfrom(2048)
        assert packet == b"\x00\x06blksize\x00512\x00"
        client.sendto(ack(0), transfer_addr)

        packet, transfer_addr = client.recvfrom(2048)
        assert packet[:4] == b"\x00\x03\x00\x01"
        assert packet[4:] == payload[:512]

        client.sendto(ack(1), transfer_addr)
        packet, transfer_addr = client.recvfrom(2048)
        assert packet[:4] == b"\x00\x03\x00\x02"
        assert packet[4:] == payload[512:]

        client.sendto(ack(2), transfer_addr)
        thread.join(timeout=2.0)
        assert not thread.is_alive()
    finally:
        client.close()
        server.close()


def test_send_file_uses_512_byte_blocks_without_blksize_negotiation(tmp_path: pathlib.Path) -> None:
    root = tmp_path / "root"
    root.mkdir()
    payload = bytes([i % 251 for i in range(600)])
    path = root / "kernel8.img"
    path.write_bytes(payload)
    client = ("10.42.0.2", 49154)
    transfer = ScriptedSocket([(ack(1), client), (ack(2), client)])
    server = aether_tftp.ReadOnlyTFTPServer(
        root=root,
        host="127.0.0.1",
        port=0,
        block_size=1468,
        timeout_s=1.0,
        log=lambda _line: None,
    )

    server._send_file(transfer, client, path, "kernel8.img")

    payloads = [packet[4:] for packet, _client in transfer.sent]
    assert len(payloads) == 2
    assert payloads[0] == payload[:512]
    assert payloads[1] == payload[512:]


def test_single_port_mode_replies_from_listener_port(tmp_path: pathlib.Path) -> None:
    root = tmp_path / "root"
    root.mkdir()
    payload = b"config=ok\n"
    (root / "config.txt").write_bytes(payload)

    server = aether_tftp.ReadOnlyTFTPServer(
        root=root,
        host="127.0.0.1",
        port=0,
        block_size=512,
        timeout_s=1.0,
        single_port=True,
        log=lambda _line: None,
    )
    server.bind()
    thread = threading.Thread(target=server.serve, kwargs={"max_requests": 1}, daemon=True)
    thread.start()

    client = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    client.settimeout(2.0)
    try:
        client.sendto(rrq("config.txt", "blksize", "512"), ("127.0.0.1", server.port))
        packet, transfer_addr = client.recvfrom(2048)
        assert transfer_addr[1] == server.port
        assert packet == b"\x00\x06blksize\x00512\x00"
        client.sendto(ack(0), transfer_addr)

        packet, transfer_addr = client.recvfrom(2048)
        assert transfer_addr[1] == server.port
        assert packet[:4] == b"\x00\x03\x00\x01"
        assert packet[4:] == payload
        client.sendto(ack(1), transfer_addr)
        thread.join(timeout=2.0)
        assert not thread.is_alive()
    finally:
        client.close()
        server.close()


def test_send_file_logs_wrong_client_and_unexpected_packets(tmp_path: pathlib.Path) -> None:
    root = tmp_path / "root"
    root.mkdir()
    payload = b"config=ok\n"
    path = root / "config.txt"
    path.write_bytes(payload)
    client = ("10.42.0.2", 1234)
    logs: list[str] = []
    transfer = ScriptedSocket(
        [
            (ack(1), ("10.42.0.2", 9999)),
            (rrq("config.txt"), client),
            (ack(1), client),
        ]
    )
    server = aether_tftp.ReadOnlyTFTPServer(
        root=root,
        host="127.0.0.1",
        port=0,
        block_size=512,
        timeout_s=1.0,
        log=logs.append,
    )

    server._send_file(transfer, client, path, "config.txt")

    assert any("reason=unexpected_client" in line and "actual=10.42.0.2:9999" in line for line in logs)
    assert any("reason=unexpected_packet" in line and "op=1" in line and "block=1" in line for line in logs)
    assert any("complete file=config.txt bytes=10" in line for line in logs)


def test_send_oack_logs_wrong_client_and_unexpected_ack_block(tmp_path: pathlib.Path) -> None:
    client = ("10.42.0.2", 1234)
    logs: list[str] = []
    transfer = ScriptedSocket(
        [
            (ack(0), ("10.42.0.2", 9999)),
            (ack(1), client),
            (ack(0), client),
        ]
    )
    server = aether_tftp.ReadOnlyTFTPServer(root=tmp_path, host="127.0.0.1", port=0, log=logs.append)

    ok, final_client = server._send_oack(transfer, client, "kernel8.img", {"blksize": "1468"})

    assert ok
    assert final_client == client
    assert any("reason=unexpected_client" in line and "actual=10.42.0.2:9999" in line for line in logs)
    assert any("reason=unexpected_packet" in line and "op=4" in line and "ack_block=1" in line for line in logs)


def test_send_oack_switches_to_duplicate_rrq_client(tmp_path: pathlib.Path) -> None:
    client = ("10.42.0.2", 49154)
    retry_client = ("10.42.0.2", 49155)
    logs: list[str] = []
    transfer = ScriptedSocket(
        [
            (rrq("config.txt", "blksize", "1468", "tsize", "0"), retry_client),
            (ack(0), retry_client),
        ]
    )
    server = aether_tftp.ReadOnlyTFTPServer(root=tmp_path, host="127.0.0.1", port=0, log=logs.append)

    ok, final_client = server._send_oack(transfer, client, "config.txt", {"blksize": "1468", "tsize": "558"})

    assert ok
    assert final_client == retry_client
    assert any("reason=duplicate_rrq" in line and "actual=10.42.0.2:49155" in line for line in logs)
    assert len(transfer.sent) == 2
    assert transfer.sent[0][1] == client
    assert transfer.sent[1][1] == retry_client


def test_send_file_returns_restart_for_duplicate_rrq(tmp_path: pathlib.Path) -> None:
    root = tmp_path / "root"
    root.mkdir()
    path = root / "config.txt"
    path.write_bytes(b"config=ok\n")
    client = ("10.42.0.2", 49154)
    retry_client = ("10.42.0.2", 49155)
    logs: list[str] = []
    transfer = ScriptedSocket(
        [
            (rrq("config.txt", "blksize", "1468", "tsize", "0"), retry_client),
        ]
    )
    server = aether_tftp.ReadOnlyTFTPServer(root=root, host="127.0.0.1", port=0, log=logs.append)

    restart = server._send_file(transfer, client, path, "config.txt")

    assert restart is not None
    assert restart.client == retry_client
    assert restart.request.filename == "config.txt"
    assert restart.request.options == {"blksize": "1468", "tsize": "0"}
    assert any("reason=duplicate_rrq" in line and "actual=10.42.0.2:49155" in line for line in logs)


def test_handle_rrq_reopens_transfer_socket_across_duplicate_retry(tmp_path: pathlib.Path) -> None:
    root = tmp_path / "root"
    root.mkdir()
    (root / "config.txt").write_bytes(b"config=ok\n")
    logs: list[str] = []

    class RestartOnceServer(aether_tftp.ReadOnlyTFTPServer):
        def __init__(self) -> None:
            super().__init__(root=root, host="127.0.0.1", port=0, log=logs.append)
            self.calls = 0
            self.closed_seen_during_retry = False

        def _send_oack(self, transfer, client, filename, options):  # type: ignore[no-untyped-def]
            return True, client

        def _send_file(self, transfer, client, path, filename, *, block_size=512):  # type: ignore[no-untyped-def]
            self.calls += 1
            if self.calls == 1:
                return aether_tftp.TransferRestart(
                    request=aether_tftp.TFTPRequest(filename="config.txt", mode="octet", options={}),
                    client=("10.42.0.2", 49155),
                )
            self.closed_seen_during_retry = getattr(transfer, "_closed", False)
            return None

    server = RestartOnceServer()
    server._handle_rrq(
        aether_tftp.TFTPRequest(filename="config.txt", mode="octet", options={}),
        ("10.42.0.2", 49154),
    )

    assert server.calls == 2
    assert not server.closed_seen_during_retry
