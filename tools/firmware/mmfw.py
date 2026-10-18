#!/usr/bin/env python3
"""Deterministic MMFW v1 and signed-v2 image parsing and construction."""

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
SIGNED_FORMAT_MAJOR = 2
SIGNED_FORMAT_MINOR = 0
SIGNED_HEADER_SIZE = 512
SIGNED_PREFIX_SIZE = 80
SIGNATURE_SIZE = 64
SIGNED_FLAG_AUTHENTICATED = 1
SIGNATURE_ECDSA_P256_SHA256 = 1
DEVELOPMENT_KEY_ID = 1
P256_ORDER = 0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551
SIGNED_PREFIX = struct.Struct("<4sBBHIIHHHHIIIBBHHBBI32s")
SIGNED_SLOT_PROFILES = {
    0: (0x08010000, 0x08010200),
    1: (0x08026000, 0x08026200),
}
SIGNED_MAX_PAYLOAD_SIZE = 0x16000 - SIGNED_HEADER_SIZE


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


@dataclass(frozen=True)
class SignedManifest(Manifest):
    slot_id: int
    signature_algorithm: int
    key_id: int
    signature: str


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


def _signed_profile(slot_id: int, load_address: int, payload_size: int) -> None:
    try:
        _, expected_vector = SIGNED_SLOT_PROFILES[slot_id]
    except KeyError as error:
        raise PackageError(f"invalid slot_id {slot_id}") from error
    if load_address != expected_vector:
        raise PackageError("load_address is incompatible with slot_id")
    if payload_size > SIGNED_MAX_PAYLOAD_SIZE:
        raise PackageError("payload exceeds signed slot region")


def _signed_prefix(
    payload: bytes,
    *,
    slot_id: int,
    target_id: int,
    hardware_id: int,
    version: tuple[int, int, int],
    build_id: int,
    load_address: int,
    required_wire: tuple[int, int],
    required_management: tuple[int, int],
    key_id: int,
) -> bytes:
    if target_id != 2 or hardware_id != 0x00830001:
        raise PackageError("signed v2 supports only the STM32U083 application target")
    _signed_profile(slot_id, load_address, len(payload))
    values = (
        MAGIC,
        SIGNED_FORMAT_MAJOR,
        SIGNED_FORMAT_MINOR,
        SIGNED_HEADER_SIZE,
        _bounded(target_id, 32, "target_id"),
        _bounded(hardware_id, 32, "hardware_id"),
        *(_bounded(item, 16, "version") for item in version),
        SIGNED_FLAG_AUTHENTICATED,
        _bounded(build_id, 32, "build_id"),
        _bounded(load_address, 32, "load_address"),
        _bounded(len(payload), 32, "payload_size"),
        _bounded(required_wire[0], 8, "wire major"),
        _bounded(required_wire[1], 8, "wire minor"),
        _bounded(required_management[0], 16, "management major"),
        _bounded(required_management[1], 16, "management minor"),
        _bounded(slot_id, 8, "slot_id"),
        SIGNATURE_ECDSA_P256_SHA256,
        _bounded(key_id, 32, "key_id"),
        hashlib.sha256(payload).digest(),
    )
    prefix = SIGNED_PREFIX.pack(*values)
    if len(prefix) != SIGNED_PREFIX_SIZE:
        raise AssertionError("internal signed MMFW prefix layout mismatch")
    return prefix


def build_signed_image(
    payload: bytes,
    private_key: Any,
    *,
    slot_id: int,
    target_id: int,
    hardware_id: int,
    version: tuple[int, int, int],
    build_id: int,
    load_address: int,
    required_wire: tuple[int, int],
    required_management: tuple[int, int],
    key_id: int = DEVELOPMENT_KEY_ID,
) -> bytes:
    from cryptography.hazmat.primitives import hashes
    from cryptography.hazmat.primitives.asymmetric import ec
    from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature

    if not isinstance(private_key, ec.EllipticCurvePrivateKey) or not isinstance(
        private_key.curve, ec.SECP256R1
    ):
        raise PackageError("private key must be ECDSA P-256")
    prefix = _signed_prefix(
        payload,
        slot_id=slot_id,
        target_id=target_id,
        hardware_id=hardware_id,
        version=version,
        build_id=build_id,
        load_address=load_address,
        required_wire=required_wire,
        required_management=required_management,
        key_id=key_id,
    )
    der = private_key.sign(prefix, ec.ECDSA(hashes.SHA256(), deterministic_signing=True))
    r, s = decode_dss_signature(der)
    s = min(s, P256_ORDER - s)
    signature = r.to_bytes(32, "big") + s.to_bytes(32, "big")
    return prefix + signature + bytes(SIGNED_HEADER_SIZE - SIGNED_PREFIX_SIZE - SIGNATURE_SIZE) + payload


def parse_signed_image(
    image: bytes, *, verify_hash: bool = True, public_key: Any | None = None
) -> tuple[SignedManifest, bytes]:
    if len(image) < SIGNED_HEADER_SIZE:
        raise PackageError("image is shorter than the 512-byte signed header")
    fields = SIGNED_PREFIX.unpack(image[:SIGNED_PREFIX_SIZE])
    if fields[0] != MAGIC:
        raise PackageError("bad MMFW magic")
    if fields[1:3] != (SIGNED_FORMAT_MAJOR, SIGNED_FORMAT_MINOR):
        raise PackageError(f"unsupported format {fields[1]}.{fields[2]}")
    if fields[3] != SIGNED_HEADER_SIZE:
        raise PackageError("signed header size must be 512")
    if fields[9] != SIGNED_FLAG_AUTHENTICATED:
        raise PackageError("signed flags must be AUTHENTICATED")
    slot_id, algorithm, key_id = fields[17:20]
    if slot_id not in SIGNED_SLOT_PROFILES:
        raise PackageError(f"invalid slot_id {slot_id}")
    if algorithm != SIGNATURE_ECDSA_P256_SHA256:
        raise PackageError(f"unsupported signature algorithm {algorithm}")
    if any(image[144:SIGNED_HEADER_SIZE]):
        raise PackageError("signed header padding must be zero")
    payload_size = fields[12]
    if len(image) != SIGNED_HEADER_SIZE + payload_size:
        raise PackageError("image size does not exactly match payload_size")
    payload = image[SIGNED_HEADER_SIZE:]
    if verify_hash and hashlib.sha256(payload).digest() != fields[20]:
        raise PackageError("payload SHA-256 mismatch")
    _signed_profile(slot_id, fields[11], payload_size)
    signature = image[SIGNED_PREFIX_SIZE:SIGNED_PREFIX_SIZE + SIGNATURE_SIZE]
    r = int.from_bytes(signature[:32], "big")
    s = int.from_bytes(signature[32:], "big")
    if not (0 < r < P256_ORDER and 0 < s <= P256_ORDER // 2):
        raise PackageError("signature is not canonical low-S P-256")
    if public_key is not None:
        from cryptography.exceptions import InvalidSignature
        from cryptography.hazmat.primitives import hashes
        from cryptography.hazmat.primitives.asymmetric import ec
        from cryptography.hazmat.primitives.asymmetric.utils import encode_dss_signature

        if not isinstance(public_key, ec.EllipticCurvePublicKey) or not isinstance(
            public_key.curve, ec.SECP256R1
        ):
            raise PackageError("public key must be ECDSA P-256")
        try:
            public_key.verify(
                encode_dss_signature(r, s),
                image[:SIGNED_PREFIX_SIZE],
                ec.ECDSA(hashes.SHA256()),
            )
        except InvalidSignature as error:
            raise PackageError("signature mismatch") from error
    manifest = SignedManifest(
        target_id=fields[4], hardware_id=fields[5],
        version_major=fields[6], version_minor=fields[7], version_patch=fields[8],
        flags=fields[9], build_id=fields[10], load_address=fields[11],
        payload_size=payload_size, required_wire_major=fields[13],
        required_wire_minor=fields[14], required_management_major=fields[15],
        required_management_minor=fields[16], payload_sha256=fields[20].hex(),
        slot_id=slot_id, signature_algorithm=algorithm, key_id=key_id,
        signature=signature.hex(),
    )
    if manifest.target_id != 2 or manifest.hardware_id != 0x00830001:
        raise PackageError("signed image target or hardware is incompatible")
    return manifest, payload
