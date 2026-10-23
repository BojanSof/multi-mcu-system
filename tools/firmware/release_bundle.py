#!/usr/bin/env python3
"""Create/verify a bounded Phase 11 release bundle; never open a device or flash."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import pathlib
import re
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from firmware.mmfw import PackageError, deterministic_json, parse_signed_image


BOARD = "nrf54l15dk+nucleo-u083rc"
BASE = 0x08000000
LIMIT = 0x10000
TOP_FIELDS = {"format", "format_version", "board", "system_version", "api", "wire",
              "management", "image_format", "nordic", "bootloader", "slots"}


@dataclass(frozen=True)
class ValidatedBundle:
    manifest: dict
    bootloader: bytes
    slot_images: tuple[bytes, bytes]
    slot_manifests: tuple[object, object]


def _fields(value, fields, what):
    if type(value) is not dict or set(value) != fields:
        raise PackageError(f"{what}: unexpected or missing fields")


def _integer(value, minimum, maximum, what):
    if type(value) is not int or not minimum <= value <= maximum:
        raise PackageError(f"{what}: invalid integer")


def _version(value):
    if type(value) is not str or not re.fullmatch(r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)", value):
        raise PackageError("system_version must be major.minor.patch")
    if any(int(part) > 65535 for part in value.split(".")):
        raise PackageError("system_version exceeds u16 components")


def _artifact(root, record):
    name = record["file"]
    if type(name) is not str or not name or "\\" in name:
        raise PackageError("artifact path must be a relative POSIX path")
    relative = pathlib.PurePosixPath(name)
    if (relative.is_absolute() or pathlib.PureWindowsPath(name).drive or
            any(part in ("..", ".", "") for part in name.split("/"))):
        raise PackageError("artifact path traversal or absolute path")
    path = (root / name).resolve()
    if not path.is_relative_to(root):
        raise PackageError("artifact symlink escapes bundle directory")
    digest = record["sha256"]
    if type(digest) is not str or not re.fullmatch(r"[0-9a-f]{64}", digest):
        raise PackageError("artifact SHA-256 must be lowercase hex")
    if not path.is_file() or path.stat().st_size > 2 * 1024 * 1024:
        raise PackageError("artifact must be a regular file of at most 2 MiB")
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != digest:
        raise PackageError(f"artifact hash mismatch: {name}")
    return data


def _nordic(image):
    # Reuse the established MCUboot bounds/hash validator. This verifies image
    # structure/integrity, not the Nordic signature against a provisioned key.
    from cli.mmctl import inspect_nordic_image
    try:
        inspect_nordic_image(image)
    except RuntimeError as error:
        raise PackageError(str(error)) from error
    _, _, header, protected, body, _ = struct.unpack("<IIHHII", image[:20])
    start = header + body + protected
    _, size = struct.unpack("<HH", image[start:start + 4])
    offset, end = start + 4, start + size
    signatures = []
    while offset < end:
        kind, length = struct.unpack("<HH", image[offset:offset + 4])
        offset += 4
        if kind in (0x20, 0x22, 0x23, 0x24):
            signatures.append((kind, length))
        offset += length
    lengths = {0x20: {256}, 0x22: set(range(64, 81)), 0x23: {384}, 0x24: {64}}
    if len(signatures) != 1 or signatures[0][1] not in lengths[signatures[0][0]]:
        raise PackageError("Nordic artifact must contain one MCUboot signature TLV")


def _loader(image):
    if not 8 <= len(image) <= LIMIT or image.startswith((b"\x7fELF", b":")):
        raise PackageError("bootloader must be a raw binary of 8..65536 bytes")
    stack, reset = struct.unpack("<II", image[:8])
    if not 0x20000000 < stack <= 0x2000A000 or stack % 8:
        raise PackageError("bootloader stack vector is outside 40 KiB SRAM")
    if not reset & 1 or not BASE <= (reset & ~1) < BASE + len(image):
        raise PackageError("bootloader reset vector is outside its raw image")


def validate(document, root, public_key, key_id=1):
    """Validate every artifact before any device connection; return frozen bytes."""
    from cryptography.hazmat.primitives.serialization import load_pem_public_key
    _fields(document, TOP_FIELDS, "release")
    for field, expected in (("format", "multi-mcu-release"), ("format_version", 2),
                            ("board", BOARD), ("api", 1), ("wire", "1.0"),
                            ("management", "1.0"), ("image_format", 2)):
        if type(document[field]) is not type(expected) or document[field] != expected:
            raise PackageError(f"incompatible release {field}")
    _version(document["system_version"])
    _integer(key_id, 0, 65535, "trusted key ID")
    _fields(document["nordic"], {"file", "sha256"}, "nordic")
    _fields(document["bootloader"], {"file", "sha256", "address", "size", "key_id"}, "bootloader")
    loader = document["bootloader"]
    _integer(loader["address"], BASE, BASE, "bootloader address")
    _integer(loader["size"], 8, LIMIT, "bootloader size")
    _integer(loader["key_id"], key_id, key_id, "bootloader key ID")
    slots = document["slots"]
    if type(slots) is not list or len(slots) != 2:
        raise PackageError("release must contain exactly Slot 0 and Slot 1")
    for slot, record in enumerate(slots):
        _fields(record, {"file", "sha256", "slot"}, "slot")
        _integer(record["slot"], slot, slot, "slot ID/order")
    names = [document["nordic"]["file"], loader["file"], *(record["file"] for record in slots)]
    if any(type(name) is not str for name in names) or len(set(names)) != len(names):
        raise PackageError("artifact paths must be distinct strings")
    root = pathlib.Path(root).resolve()
    key = load_pem_public_key(pathlib.Path(public_key).read_bytes())
    nordic = _artifact(root, document["nordic"])
    _nordic(nordic)
    raw_loader = _artifact(root, loader)
    if len(raw_loader) != loader["size"]:
        raise PackageError("bootloader size mismatch")
    _loader(raw_loader)
    images, manifests = [], []
    for slot, record in enumerate(slots):
        image = _artifact(root, record)
        manifest, _ = parse_signed_image(image, public_key=key)
        if (manifest.slot_id != slot or manifest.key_id != key_id or
                manifest.version != document["system_version"] or
                (manifest.required_wire_major, manifest.required_wire_minor,
                 manifest.required_management_major, manifest.required_management_minor) != (1, 0, 1, 0)):
            raise PackageError("slot/version/key/protocol mismatch")
        images.append(image)
        manifests.append(manifest)
    return ValidatedBundle(document, raw_loader, tuple(images), tuple(manifests))


def _unique(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise PackageError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def load(path, public_key, key_id=1):
    path = pathlib.Path(path).resolve()
    document = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=_unique)
    return validate(document, path.parent, public_key, key_id)


def create(output, nordic, bootloader, slot0, slot1, system_version, public_key, key_id=1):
    output = pathlib.Path(output).resolve()
    root = output.parent
    def record(path):
        path = pathlib.Path(path).resolve()
        if not path.is_relative_to(root):
            raise PackageError("artifacts must reside inside the manifest directory")
        return {"file": path.relative_to(root).as_posix(),
                "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
    document = {"format": "multi-mcu-release", "format_version": 2, "board": BOARD,
                "system_version": system_version, "api": 1, "wire": "1.0",
                "management": "1.0", "image_format": 2, "nordic": record(nordic),
                "bootloader": {**record(bootloader), "address": BASE,
                               "size": pathlib.Path(bootloader).stat().st_size, "key_id": key_id},
                "slots": [{**record(slot0), "slot": 0}, {**record(slot1), "slot": 1}]}
    validate(document, root, public_key, key_id)
    output.write_text(deterministic_json(document), encoding="utf-8")
    return document


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="action", required=True)
    verify = commands.add_parser("verify")
    verify.add_argument("manifest", type=pathlib.Path)
    build = commands.add_parser("create")
    build.add_argument("manifest", type=pathlib.Path)
    for name in ("nordic", "bootloader", "slot0", "slot1"):
        build.add_argument("--" + name, required=True, type=pathlib.Path)
    build.add_argument("--system-version", required=True)
    for command in (verify, build):
        command.add_argument("--public-key", required=True, type=pathlib.Path)
        command.add_argument("--key-id", type=lambda value: int(value, 0), default=1)
    args = parser.parse_args(argv)
    try:
        if args.action == "create":
            document = create(args.manifest, args.nordic, args.bootloader, args.slot0,
                              args.slot1, args.system_version, args.public_key, args.key_id)
        else:
            document = load(args.manifest, args.public_key, args.key_id).manifest
        print(deterministic_json({"verified": True, "system_version": document["system_version"]}), end="")
        return 0
    except (OSError, ValueError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    raise SystemExit(main())
