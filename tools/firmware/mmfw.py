#!/usr/bin/env python3
"""Deterministic Phase 6 MMFW package parser and builder (standard library only)."""

from __future__ import annotations

import hashlib
import json
import pathlib
import struct
from dataclasses import asdict, dataclass
from typing import Any

MAGIC = b"MMFW"
FORMAT_MAJOR = 1
FORMAT_MINOR = 0
HEADER_SIZE = 80
HEADER = struct.Struct("<4sBBHIIHHHHIII BBHH 6x 32s")
TARGET_PROFILES = {
    1: (0x54150001, 0x00011000, 696 * 1024),
    2: (0x00830001, 0x08000000, 256 * 1024),
}


class PackageError(ValueError):
    """The package is malformed or fails an integrity check."""


@dataclass(frozen=True)
class Manifest:
    target_id: int
    hardware_id: int
    version_major: int
    version_minor: int
    version_patch: int
    flags: int
    build_id: int
    load_address: int
    payload_size: int
    required_wire_major: int
    required_wire_minor: int
    required_management_major: int
    required_management_minor: int
    payload_sha256: str

    @property
    def version(self) -> str:
        return f"{self.version_major}.{self.version_minor}.{self.version_patch}"

    def to_dict(self) -> dict[str, Any]:
        result = asdict(self)
        result["version"] = self.version
        return result


def _bounded(value: int, bits: int, name: str) -> int:
    if not 0 <= value < (1 << bits):
        raise PackageError(f"{name} must fit in uint{bits}")
    return value


def _validate_profile_fields(
    target_id: int, hardware_id: int, load_address: int, payload_size: int
) -> None:
    try:
        expected_hardware, expected_address, maximum_size = TARGET_PROFILES[target_id]
    except KeyError as error:
        raise PackageError(f"unsupported target_id {target_id}") from error
    if hardware_id != expected_hardware:
        raise PackageError("hardware_id is incompatible with target_id")
    if load_address != expected_address:
        raise PackageError("load_address is incompatible with target_id")
    if payload_size > maximum_size:
        raise PackageError("payload exceeds target region")


def build_package(
    payload: bytes,
    *,
    target_id: int,
    hardware_id: int,
    version: tuple[int, int, int],
    build_id: int,
    load_address: int,
    required_wire: tuple[int, int],
    required_management: tuple[int, int],
) -> bytes:
    values = (
        _bounded(target_id, 32, "target_id"),
        _bounded(hardware_id, 32, "hardware_id"),
        *(_bounded(item, 16, "version") for item in version),
        0,
        _bounded(build_id, 32, "build_id"),
        _bounded(load_address, 32, "load_address"),
        _bounded(len(payload), 32, "payload_size"),
        _bounded(required_wire[0], 8, "wire major"),
        _bounded(required_wire[1], 8, "wire minor"),
        _bounded(required_management[0], 16, "management major"),
        _bounded(required_management[1], 16, "management minor"),
        hashlib.sha256(payload).digest(),
    )
    _validate_profile_fields(values[0], values[1], values[7], values[8])
    header = HEADER.pack(MAGIC, FORMAT_MAJOR, FORMAT_MINOR, HEADER_SIZE, *values)
    if len(header) != HEADER_SIZE:
        raise AssertionError("internal MMFW header layout mismatch")
    return header + payload


def parse_package(package: bytes, *, verify_hash: bool = True) -> tuple[Manifest, bytes]:
    if len(package) < HEADER_SIZE:
        raise PackageError("package is shorter than the 80-byte header")
    fields = HEADER.unpack(package[:HEADER_SIZE])
    if fields[0] != MAGIC:
        raise PackageError("bad MMFW magic")
    if fields[1:3] != (FORMAT_MAJOR, FORMAT_MINOR):
        raise PackageError(f"unsupported format {fields[1]}.{fields[2]}")
    if fields[3] != HEADER_SIZE:
        raise PackageError("header size must be 80")
    flags = fields[9]
    payload_size = fields[12]
    if flags != 0:
        raise PackageError("Phase 6 flags must be zero")
    # struct padding does not expose the six reserved bytes.
    if any(package[42:48]):
        raise PackageError("reserved header bytes must be zero")
    if len(package) != HEADER_SIZE + payload_size:
        raise PackageError("package size does not exactly match payload_size")
    payload = package[HEADER_SIZE:]
    digest = hashlib.sha256(payload).digest()
    if verify_hash and digest != fields[17]:
        raise PackageError("payload SHA-256 mismatch")
    manifest = Manifest(
        target_id=fields[4],
        hardware_id=fields[5],
        version_major=fields[6],
        version_minor=fields[7],
        version_patch=fields[8],
        flags=flags,
        build_id=fields[10],
        load_address=fields[11],
        payload_size=payload_size,
        required_wire_major=fields[13],
        required_wire_minor=fields[14],
        required_management_major=fields[15],
        required_management_minor=fields[16],
        payload_sha256=fields[17].hex(),
    )
    _validate_profile_fields(
        manifest.target_id, manifest.hardware_id, manifest.load_address, manifest.payload_size
    )
    return manifest, payload


def package_record(path: pathlib.Path) -> dict[str, Any]:
    data = path.read_bytes()
    manifest, _ = parse_package(data)
    return {
        "file": path.name,
        "package_sha256": hashlib.sha256(data).hexdigest(),
        **manifest.to_dict(),
    }


def deterministic_json(value: Any) -> str:
    return json.dumps(value, indent=2, sort_keys=True, separators=(",", ": ")) + "\n"
