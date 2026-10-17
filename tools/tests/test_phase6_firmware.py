import hashlib
import pathlib
import subprocess
import sys

import pytest

FIRMWARE_TOOLS = pathlib.Path(__file__).parents[1] / "firmware"
sys.path.insert(0, str(FIRMWARE_TOOLS))

from mmfw import HEADER_SIZE, PackageError, build_package, package_record, parse_package  # noqa: E402


def arguments(payload: bytes = b"abc") -> tuple[bytes, dict]:
    return payload, {
        "target_id": 2,
        "hardware_id": 0x00830001,
        "version": (1, 2, 3),
        "build_id": 0x12345678,
        "load_address": 0x08000000,
        "required_wire": (1, 0),
        "required_management": (1, 2),
    }


def test_golden_layout_and_determinism() -> None:
    payload, fields = arguments()
    first = build_package(payload, **fields)
    second = build_package(payload, **fields)
    assert first == second
    assert len(first) == HEADER_SIZE + len(payload)
    assert first[:8] == b"MMFW\x01\x00\x50\x00"
    assert first[42:48] == bytes(6)
    assert first[48:80] == hashlib.sha256(payload).digest()
    manifest, extracted = parse_package(first)
    assert extracted == payload
    assert manifest.version == "1.2.3"
    assert manifest.build_id == 0x12345678


@pytest.mark.parametrize(
    ("mutator", "message"),
    [
        (lambda value: value[:79], "shorter"),
        (lambda value: b"BAD!" + value[4:], "magic"),
        (lambda value: value[:5] + b"\x01" + value[6:], "unsupported"),
        (lambda value: value[:6] + b"\x51\x00" + value[8:], "header size"),
        (lambda value: value[:22] + b"\x01" + value[23:], "flags"),
        (lambda value: value[:42] + b"\x01" + value[43:], "reserved"),
        (lambda value: value[:-1], "size"),
        (lambda value: value[:-1] + bytes([value[-1] ^ 1]), "SHA-256"),
    ],
)
def test_malformed_packages(mutator, message: str) -> None:
    payload, fields = arguments()
    with pytest.raises(PackageError, match=message):
        parse_package(mutator(build_package(payload, **fields)))


def test_numeric_overflow_rejected() -> None:
    payload, fields = arguments()
    fields["target_id"] = 1 << 32
    with pytest.raises(PackageError, match="uint32"):
        build_package(payload, **fields)


@pytest.mark.parametrize(
    ("field", "value", "message"),
    [
        ("target_id", 99, "unsupported target"),
        ("hardware_id", 0xDEADBEEF, "hardware_id"),
        ("load_address", 0x12345678, "load_address"),
    ],
)
def test_incompatible_profile_rejected(field: str, value: int, message: str) -> None:
    payload, fields = arguments()
    fields[field] = value
    with pytest.raises(PackageError, match=message):
        build_package(payload, **fields)


def test_oversized_profile_rejected() -> None:
    _, fields = arguments()
    with pytest.raises(PackageError, match="target region"):
        build_package(bytes((256 * 1024) + 1), **fields)


def test_cli_and_release_manifest_are_deterministic(tmp_path: pathlib.Path) -> None:
    payload = tmp_path / "app.bin"
    package_a = tmp_path / "a.mmfw"
    package_b = tmp_path / "b.mmfw"
    release_a = tmp_path / "release-a.json"
    release_b = tmp_path / "release-b.json"
    payload.write_bytes(b"phase-six\x00payload")
    command = [
        sys.executable,
        str(FIRMWARE_TOOLS / "pack.py"),
        str(payload),
        str(package_a),
        "--target-id", "2",
        "--hardware-id", "0x00830001",
        "--version", "1.0.7",
        "--build-id", "7",
        "--load-address", "0x08000000",
        "--wire", "1.0",
        "--management", "1.0",
    ]
    subprocess.run(command, check=True)
    command[3] = str(package_b)
    subprocess.run(command, check=True)
    assert package_a.read_bytes() == package_b.read_bytes()
    verify = subprocess.run(
        [sys.executable, str(FIRMWARE_TOOLS / "verify.py"), str(package_a)],
        check=True,
        capture_output=True,
        text=True,
    )
    assert verify.stdout.startswith("PASS:")
    for output in (release_a, release_b):
        subprocess.run(
            [
                sys.executable,
                str(FIRMWARE_TOOLS / "release_manifest.py"),
                str(output),
                str(package_a),
            ],
            check=True,
        )
    assert release_a.read_bytes() == release_b.read_bytes()
    assert package_record(package_a)["package_sha256"] == hashlib.sha256(
        package_a.read_bytes()
    ).hexdigest()
