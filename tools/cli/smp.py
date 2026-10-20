"""Bounded MCUmanager SMP v2 serial transport (Zephyr NLIP framing)."""

from __future__ import annotations

import base64
import binascii
import io
import struct
import time
from collections.abc import Callable

import cbor2

START = b"\x06\x09"
CONTINUE = b"\x04\x14"
HEADER = struct.Struct(">BBHHBB")
MAX_PACKET = 8192


class SmpError(RuntimeError):
    """Invalid response or device-side failure."""


class SmpTimeout(SmpError):
    """A response did not arrive within the bounded retry budget."""


def crc16(data: bytes) -> int:
    return binascii.crc_hqx(data, 0)


def encode_serial(packet: bytes, line_length: int = 124) -> bytes:
    if not 8 <= len(packet) <= MAX_PACKET:
        raise SmpError("SMP packet size outside bounds")
    # Each frame is independently base64 encoded; first includes big-endian length.
    raw = struct.pack(">H", len(packet) + 2) + packet + struct.pack(">H", crc16(packet))
    chunk_size = ((line_length - 3) // 4) * 3
    if chunk_size < 3:
        raise ValueError("serial line too short")
    return b"".join(
        (START if offset == 0 else CONTINUE)
        + base64.b64encode(raw[offset : offset + chunk_size])
        + b"\n"
        for offset in range(0, len(raw), chunk_size)
    )


class SerialDecoder:
    """Accept partial UART reads, ignore console lines, check length and CRC."""

    def __init__(self, log: Callable[[bytes], None] | None = None):
        self.line = bytearray()
        self.packet = bytearray()
        self.expected: int | None = None
        self.log = log
        self.errors = 0

    def feed(self, data: bytes) -> list[bytes]:
        packets = []
        self.line.extend(data)
        while b"\n" in self.line:
            line, _, remaining = self.line.partition(b"\n")
            self.line = bytearray(remaining)
            # UART logs may end immediately before a frame marker.
            start = line.find(START)
            continuation = line.find(CONTINUE)
            positions = [position for position in (start, continuation) if position >= 0]
            if not positions:
                if self.log and line:
                    self.log(bytes(line).rstrip(b"\r"))
                continue
            position = min(positions)
            if self.log and position:
                self.log(bytes(line[:position]))
            marker, encoded = bytes(line[position : position + 2]), line[position + 2 :]
            try:
                fragment = base64.b64decode(encoded.rstrip(b"\r"), validate=True)
                if marker == START:
                    if len(fragment) < 2:
                        raise ValueError("missing packet length")
                    self.expected = struct.unpack(">H", fragment[:2])[0]
                    self.packet.clear()
                    fragment = fragment[2:]
                    if not 10 <= self.expected <= MAX_PACKET + 2:
                        raise ValueError("invalid packet length")
                elif self.expected is None:
                    raise ValueError("orphan continuation")
                self.packet.extend(fragment)
                if len(self.packet) > self.expected:
                    raise ValueError("packet too long")
                if len(self.packet) == self.expected:
                    if crc16(self.packet) != 0:
                        raise ValueError("bad CRC")
                    packets.append(bytes(self.packet[:-2]))
                    self.packet.clear()
                    self.expected = None
            except (ValueError, binascii.Error):
                self.errors += 1
                self.packet.clear()
                self.expected = None
        if len(self.line) > MAX_PACKET * 2:
            self.line.clear()
            self.errors += 1
        return packets


def make_packet(operation: int, group: int, command: int, sequence: int, data: dict) -> bytes:
    body = cbor2.dumps(data)
    if len(body) > MAX_PACKET - HEADER.size:
        raise SmpError("request too large")
    # Operation is bits 0..2; SMP version 2 has wire value 1 in bits 3..4.
    return HEADER.pack(operation | 0x08, 0, len(body), group, sequence, command) + body


class SmpClient:
    def __init__(self, serial, timeout: float = 3.0, retries: int = 2, log=None):
        if timeout <= 0 or not 0 <= retries <= 5:
            raise ValueError("invalid timeout/retry budget")
        self.serial = serial
        self.timeout = timeout
        self.retries = retries
        self.sequence = 0
        self.decoder = SerialDecoder(log)

    def request(self, group: int, command: int, data: dict | None = None,
                write: bool = False, retry: bool = True) -> dict:
        operation = 2 if write else 0
        sequence = self.sequence
        self.sequence = (self.sequence + 1) & 255
        framed = encode_serial(make_packet(operation, group, command, sequence, data or {}))
        # Never reconnect here: a mutating request must not be replayed across host boot.
        for _ in range(self.retries + 1 if retry else 1):
            if self.serial.write(framed) != len(framed):
                raise SmpError("short serial write; operation outcome unknown")
            deadline = time.monotonic() + self.timeout
            while time.monotonic() < deadline:
                incoming = self.serial.read(256)
                for packet in self.decoder.feed(incoming):
                    if len(packet) < HEADER.size:
                        continue
                    op, flags, length, response_group, seq, response_command = HEADER.unpack(
                        packet[:HEADER.size]
                    )
                    if (response_group, seq, response_command) != (group, sequence, command):
                        continue
                    if (op & 7) != operation + 1 or (op & 0xF8) != 8 or flags or length != len(packet) - HEADER.size:
                        raise SmpError("invalid SMP response header")
                    try:
                        body = io.BytesIO(packet[HEADER.size:])
                        response = cbor2.CBORDecoder(body).decode()
                        if body.tell() != length:
                            raise ValueError("trailing CBOR bytes")
                    except (ValueError, cbor2.CBORDecodeError) as exc:
                        raise SmpError("invalid CBOR response") from exc
                    if not isinstance(response, dict):
                        raise SmpError("SMP response is not a map")
                    if response.get("rc", 0) or response.get("err"):
                        if group == 64 and response.get("rc") == 8:
                            raise SmpError("host control commands are unavailable; build and flash the phase9 profile")
                        raise SmpError(f"device management error: {response}")
                    if "error" in response:
                        raise SmpError(f"host control error: {response['error']}")
                    return response
        raise SmpTimeout("SMP response timed out; operation outcome may be unknown")
