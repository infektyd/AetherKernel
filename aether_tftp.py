#!/usr/bin/env python3
"""Tiny read-only TFTP server for the AetherKernel Pi bench."""

from __future__ import annotations

import argparse
import os
import pathlib
import socket
import sys
from dataclasses import dataclass
from typing import Callable


OP_RRQ = 1
OP_DATA = 3
OP_ACK = 4
OP_ERROR = 5
OP_OACK = 6

ERR_NOT_FOUND = 1
ERR_ACCESS = 2
ERR_ILLEGAL = 4


@dataclass(frozen=True)
class TFTPRequest:
    filename: str
    mode: str
    options: dict[str, str]


@dataclass(frozen=True)
class TransferRestart:
    request: TFTPRequest
    client: tuple[str, int]


def parse_rrq(packet: bytes) -> TFTPRequest:
    if len(packet) < 4 or int.from_bytes(packet[:2], "big") != OP_RRQ:
        raise ValueError("not an RRQ packet")

    fields = packet[2:].split(b"\x00")
    if fields and fields[-1] == b"":
        fields = fields[:-1]
    if len(fields) < 2:
        raise ValueError("malformed RRQ")

    filename = fields[0].decode("ascii", "strict")
    mode = fields[1].decode("ascii", "strict").lower()
    options: dict[str, str] = {}
    option_fields = fields[2:]
    for i in range(0, len(option_fields) - 1, 2):
        key = option_fields[i].decode("ascii", "strict").lower()
        value = option_fields[i + 1].decode("ascii", "strict")
        options[key] = value

    return TFTPRequest(filename=filename, mode=mode, options=options)


def data_packet(block: int, payload: bytes) -> bytes:
    return OP_DATA.to_bytes(2, "big") + block.to_bytes(2, "big") + payload


def error_packet(code: int, message: str) -> bytes:
    return OP_ERROR.to_bytes(2, "big") + code.to_bytes(2, "big") + message.encode("ascii", "replace") + b"\x00"


def oack_packet(options: dict[str, str]) -> bytes:
    packet = OP_OACK.to_bytes(2, "big")
    for key, value in options.items():
        packet += key.encode("ascii") + b"\x00" + value.encode("ascii") + b"\x00"
    return packet


def endpoint_text(endpoint: tuple[str, int]) -> str:
    return f"{endpoint[0]}:{endpoint[1]}"


def packet_summary(packet: bytes) -> str:
    if len(packet) < 2:
        return "op=short"
    op = int.from_bytes(packet[:2], "big")
    summary = f"op={op}"
    if op in (OP_DATA, OP_ACK) and len(packet) >= 4:
        summary += f" ack_block={int.from_bytes(packet[2:4], 'big')}"
    if op == OP_ERROR and len(packet) >= 4:
        code = int.from_bytes(packet[2:4], "big")
        message = packet[4:].split(b"\x00", 1)[0].decode("ascii", "replace")
        summary += f" error_code={code} error={message!r}"
    return summary


def duplicate_rrq(packet: bytes, *, filename: str, current_client: tuple[str, int], packet_client: tuple[str, int]) -> TFTPRequest | None:
    if current_client[0] != packet_client[0]:
        return None
    try:
        request = parse_rrq(packet)
    except (UnicodeDecodeError, ValueError):
        return None
    if request.filename != filename:
        return None
    return request


def resolve_request_path(root: pathlib.Path, filename: str) -> pathlib.Path | None:
    relative = pathlib.PurePosixPath(filename.lstrip("/"))
    if relative.is_absolute() or ".." in relative.parts or str(relative) in ("", "."):
        return None

    root_resolved = root.resolve()
    candidate = (root_resolved / pathlib.Path(*relative.parts)).resolve()
    try:
        candidate.relative_to(root_resolved)
    except ValueError:
        return None
    return candidate


class ReadOnlyTFTPServer:
    def __init__(
        self,
        *,
        root: pathlib.Path,
        host: str,
        port: int,
        block_size: int = 512,
        timeout_s: float = 2.0,
        transfer_host: str = "0.0.0.0",
        single_port: bool = False,
        log: Callable[[str], None] | None = None,
    ) -> None:
        if block_size <= 0 or block_size > 65464:
            raise ValueError("block_size must be between 1 and 65464")
        self.root = pathlib.Path(root)
        self.host = host
        self.requested_port = port
        self.block_size = block_size
        self.timeout_s = timeout_s
        self.transfer_host = transfer_host
        self.single_port = single_port
        self.log = log or print
        self.sock: socket.socket | None = None
        self.port = port
        self._closed = False

    def bind(self) -> None:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind((self.host, self.requested_port))
        sock.settimeout(self.timeout_s)
        self.sock = sock
        self.port = sock.getsockname()[1]

    def close(self) -> None:
        self._closed = True
        if self.sock is not None:
            self.sock.close()
            self.sock = None

    def serve(self, max_requests: int | None = None) -> None:
        if self.sock is None:
            self.bind()
        assert self.sock is not None

        handled = 0
        self.log(f"aether-tftp serving root={self.root} host={self.host} port={self.port} block_size={self.block_size}")
        while not self._closed:
            if max_requests is not None and handled >= max_requests:
                return
            try:
                packet, client = self.sock.recvfrom(2048)
            except TimeoutError:
                continue
            except OSError:
                return

            handled += 1
            try:
                request = parse_rrq(packet)
            except (UnicodeDecodeError, ValueError) as exc:
                self.log(f"aether-tftp error reason=bad_rrq client={client[0]}:{client[1]} detail={exc}")
                try:
                    self.sock.sendto(error_packet(ERR_ILLEGAL, "bad rrq"), client)
                except OSError as send_error:
                    self.log(f"aether-tftp error reason=send_error client={client[0]}:{client[1]} detail={send_error}")
                continue
            self._handle_rrq(request, client)

    def _handle_rrq(self, request: TFTPRequest, client: tuple[str, int]) -> None:
        if request.mode != "octet":
            self.log(f"aether-tftp error file={request.filename} reason=unsupported_mode mode={request.mode}")
            self._send_error(client, ERR_ILLEGAL, "octet only")
            return

        path = resolve_request_path(self.root, request.filename)
        if path is None:
            self.log(f"aether-tftp error file={request.filename} reason=bad_path")
            self._send_error(client, ERR_ACCESS, "bad path")
            return
        if not path.is_file():
            self.log(f"aether-tftp error file={request.filename} reason=not_found")
            self._send_error(client, ERR_NOT_FOUND, "not found")
            return

        size = path.stat().st_size
        if request.options:
            option_text = ",".join(f"{key}={value}" for key, value in sorted(request.options.items()))
            self.log(f"aether-tftp rrq file={request.filename} size={size} options={option_text} action=oack")
        else:
            self.log(f"aether-tftp rrq file={request.filename} size={size}")

        while True:
            if self.single_port:
                assert self.sock is not None
                transfer = self.sock
            else:
                transfer = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                transfer.settimeout(self.timeout_s)
                transfer.bind((self.transfer_host, 0))
            try:
                accepted_options = self._accepted_options(request, size)
                if accepted_options:
                    ok, client = self._send_oack(transfer, client, request.filename, accepted_options)
                    if not ok:
                        return
                restart = self._send_file(transfer, client, path, request.filename)
                if restart is None:
                    return
                request = restart.request
                client = restart.client
                path = resolve_request_path(self.root, request.filename)
                if path is None or not path.is_file():
                    self._send_error(client, ERR_NOT_FOUND, "not found")
                    return
                size = path.stat().st_size
                self.log(
                    "aether-tftp restart "
                    f"file={request.filename} size={size} client={endpoint_text(client)}"
                )
            finally:
                if not self.single_port:
                    transfer.close()

    def _send_error(self, client: tuple[str, int], code: int, message: str) -> None:
        if self.sock is not None:
            try:
                self.sock.sendto(error_packet(code, message), client)
            except OSError as send_error:
                self.log(f"aether-tftp error reason=send_error client={client[0]}:{client[1]} detail={send_error}")

    def _accepted_options(self, request: TFTPRequest, size: int) -> dict[str, str]:
        accepted: dict[str, str] = {}
        if "blksize" in request.options:
            accepted["blksize"] = str(self.block_size)
        if "tsize" in request.options:
            accepted["tsize"] = str(size)
        return accepted

    def _send_oack(self, transfer: socket.socket, client: tuple[str, int], filename: str, options: dict[str, str]) -> tuple[bool, tuple[str, int]]:
        packet = oack_packet(options)
        for attempt in range(1, 6):
            try:
                transfer.sendto(packet, client)
                ack, ack_client = transfer.recvfrom(2048)
            except (TimeoutError, OSError):
                self.log(f"aether-tftp retry file={filename} block=0 attempt={attempt}")
                continue
            if ack_client != client:
                retry = duplicate_rrq(ack, filename=filename, current_client=client, packet_client=ack_client)
                if retry is not None:
                    self.log(
                        "aether-tftp retry "
                        f"file={filename} block=0 reason=duplicate_rrq "
                        f"expected={endpoint_text(client)} actual={endpoint_text(ack_client)}"
                    )
                    client = ack_client
                    continue
                self.log(
                    "aether-tftp ignore "
                    f"file={filename} block=0 reason=unexpected_client "
                    f"expected={endpoint_text(client)} actual={endpoint_text(ack_client)} {packet_summary(ack)}"
                )
                continue
            if len(ack) >= 4 and int.from_bytes(ack[:2], "big") == OP_ACK and int.from_bytes(ack[2:4], "big") == 0:
                return True, client
            self.log(f"aether-tftp ignore file={filename} block=0 reason=unexpected_packet {packet_summary(ack)}")
        self.log(f"aether-tftp timeout file={filename} block=0 sent=0")
        return False, client

    def _send_file(self, transfer: socket.socket, client: tuple[str, int], path: pathlib.Path, filename: str) -> TransferRestart | None:
        block = 1
        total = 0
        with path.open("rb") as file:
            while True:
                chunk = file.read(self.block_size)
                packet = data_packet(block, chunk)
                for attempt in range(1, 6):
                    try:
                        transfer.sendto(packet, client)
                        ack, ack_client = transfer.recvfrom(2048)
                    except (TimeoutError, OSError):
                        self.log(f"aether-tftp retry file={filename} block={block} attempt={attempt}")
                        continue
                    if ack_client != client:
                        retry = duplicate_rrq(ack, filename=filename, current_client=client, packet_client=ack_client)
                        if retry is not None:
                            self.log(
                                "aether-tftp retry "
                                f"file={filename} block={block} reason=duplicate_rrq "
                                f"expected={endpoint_text(client)} actual={endpoint_text(ack_client)}"
                            )
                            return TransferRestart(request=retry, client=ack_client)
                        self.log(
                            "aether-tftp ignore "
                            f"file={filename} block={block} reason=unexpected_client "
                            f"expected={endpoint_text(client)} actual={endpoint_text(ack_client)} {packet_summary(ack)}"
                        )
                        continue
                    if len(ack) >= 4 and int.from_bytes(ack[:2], "big") == OP_ACK and int.from_bytes(ack[2:4], "big") == block:
                        break
                    self.log(f"aether-tftp ignore file={filename} block={block} reason=unexpected_packet {packet_summary(ack)}")
                else:
                    self.log(f"aether-tftp timeout file={filename} block={block} sent={total}")
                    return

                total += len(chunk)
                if len(chunk) < self.block_size:
                    self.log(f"aether-tftp complete file={filename} bytes={total}")
                    return
                block = (block + 1) & 0xFFFF


def positive_int(value: str) -> int:
    parsed = int(value, 10)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("must be positive")
    return parsed


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Serve a read-only TFTP root for AetherKernel netboot diagnostics.")
    parser.add_argument("--host", default=os.environ.get("AETHER_NETBOOT_SERVER_IP", "10.42.0.1"))
    parser.add_argument("--port", type=positive_int, default=int(os.environ.get("AETHER_TFTP_PORT", "69")))
    parser.add_argument("--root", type=pathlib.Path, default=pathlib.Path(os.environ.get("AETHER_TFTP_ROOT", "~/aether-tftp")).expanduser())
    parser.add_argument("--block-size", type=positive_int, default=int(os.environ.get("AETHER_TFTP_BLOCK_SIZE", "512")))
    parser.add_argument("--timeout", type=float, default=float(os.environ.get("AETHER_TFTP_TIMEOUT", "2.0")))
    parser.add_argument("--single-port", action="store_true", default=os.environ.get("AETHER_TFTP_SINGLE_PORT", "0") == "1")
    args = parser.parse_args(argv)

    server = ReadOnlyTFTPServer(
        root=args.root,
        host=args.host,
        port=args.port,
        block_size=args.block_size,
        timeout_s=args.timeout,
        single_port=args.single_port,
        log=lambda line: print(line, flush=True),
    )
    try:
        server.serve()
    except KeyboardInterrupt:
        return 130
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
