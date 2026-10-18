import hashlib
import pathlib
import subprocess
import sys

import pytest
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec

FIRMWARE_TOOLS = pathlib.Path(__file__).parents[1] / "firmware"
sys.path.insert(0, str(FIRMWARE_TOOLS))

from mmfw import (  # noqa: E402
    P256_ORDER,
    SIGNED_HEADER_SIZE,
    PackageError,
    build_signed_image,
    parse_signed_image,
)


def private_key():
    return ec.derive_private_key(0x123456789ABCDEF, ec.SECP256R1())


def signed_image(slot: int = 0) -> bytes:
    return build_signed_image(
        b"phase-seven\x00payload",
        private_key(),
        slot_id=slot,
        target_id=2,
        hardware_id=0x00830001,
        version=(2, 0, 1),
        build_id=0x00070001,
        load_address=0x08010200 if slot == 0 else 0x08026200,
        required_wire=(1, 0),
        required_management=(1, 0),
    )


def test_signed_golden_vector_is_deterministic_and_low_s() -> None:
    first = signed_image()
    second = signed_image()
    assert first == second
    assert hashlib.sha256(first).hexdigest() == (
        "c771face0596bb059b3a092ead6d72752be045aa63cacf20460b2b2f0d8c6ff7"
    )
    assert first[:8] == b"MMFW\x02\x00\x00\x02"
    assert first[144:SIGNED_HEADER_SIZE] == bytes(SIGNED_HEADER_SIZE - 144)
    assert int.from_bytes(first[112:144], "big") <= P256_ORDER // 2
    manifest, payload = parse_signed_image(first, public_key=private_key().public_key())
    assert payload == b"phase-seven\x00payload"
    assert manifest.slot_id == 0
    assert manifest.load_address == 0x08010200
    assert manifest.signature_algorithm == 1
    assert manifest.key_id == 1


@pytest.mark.parametrize("offset", [8, 28, 42, 44, 48, 80, 143])
def test_signed_canonical_and_signature_tamper_rejected(offset: int) -> None:
    image = bytearray(signed_image())
    image[offset] ^= 1
    with pytest.raises(PackageError):
        parse_signed_image(bytes(image), public_key=private_key().public_key())


def test_signed_payload_hash_and_padding_tamper_rejected() -> None:
    payload_tamper = bytearray(signed_image())
    payload_tamper[-1] ^= 1
    with pytest.raises(PackageError, match="SHA-256"):
        parse_signed_image(bytes(payload_tamper), public_key=private_key().public_key())

    padding_tamper = bytearray(signed_image())
    padding_tamper[200] = 1
    with pytest.raises(PackageError, match="padding"):
        parse_signed_image(bytes(padding_tamper), public_key=private_key().public_key())


def test_high_s_and_wrong_slot_address_rejected() -> None:
    high_s = bytearray(signed_image())
    low_s = int.from_bytes(high_s[112:144], "big")
    high_s[112:144] = (P256_ORDER - low_s).to_bytes(32, "big")
    with pytest.raises(PackageError, match="low-S"):
        parse_signed_image(bytes(high_s))

    with pytest.raises(PackageError, match="load_address"):
        build_signed_image(
            b"payload", private_key(), slot_id=1, target_id=2,
            hardware_id=0x00830001, version=(1, 0, 0), build_id=1,
            load_address=0x08010200, required_wire=(1, 0),
            required_management=(1, 0),
        )


def test_sign_and_verify_cli_require_external_keys(tmp_path: pathlib.Path) -> None:
    key = private_key()
    private_path = tmp_path / "private.pem"
    public_path = tmp_path / "public.pem"
    payload_path = tmp_path / "app.bin"
    image_a = tmp_path / "slot1-a.mmfw"
    image_b = tmp_path / "slot1-b.mmfw"
    private_path.write_bytes(
        key.private_bytes(
            serialization.Encoding.PEM,
            serialization.PrivateFormat.PKCS8,
            serialization.NoEncryption(),
        )
    )
    public_path.write_bytes(
        key.public_key().public_bytes(
            serialization.Encoding.PEM,
            serialization.PublicFormat.SubjectPublicKeyInfo,
        )
    )
    payload_path.write_bytes(b"slot-one-payload")
    command = [
        sys.executable, str(FIRMWARE_TOOLS / "sign.py"), str(payload_path), str(image_a),
        "--private-key", str(private_path), "--slot", "1", "--version", "2.1.0",
        "--build-id", "0x70100", "--wire", "1.0", "--management", "1.0",
    ]
    subprocess.run(command, check=True)
    command[3] = str(image_b)
    subprocess.run(command, check=True)
    assert image_a.read_bytes() == image_b.read_bytes()
    result = subprocess.run(
        [sys.executable, str(FIRMWARE_TOOLS / "verify_signed.py"), str(image_a),
         "--public-key", str(public_path)],
        check=True, capture_output=True, text=True,
    )
    assert result.stdout.startswith("PASS: signed MMFW 2.1.0, slot 1")


def test_key_generation_and_bootloader_header(tmp_path: pathlib.Path) -> None:
    private_path = tmp_path / "private.pem"
    public_path = tmp_path / "public.pem"
    header_path = tmp_path / "public.h"
    subprocess.run(
        [sys.executable, str(FIRMWARE_TOOLS / "generate_key.py"),
         str(private_path), str(public_path)],
        check=True,
    )
    subprocess.run(
        [sys.executable, str(FIRMWARE_TOOLS / "public_key_header.py"),
         str(public_path), str(header_path), "--key-id", "7"],
        check=True,
    )
    header = header_path.read_text(encoding="ascii")
    assert "BOOTLOADER_PUBLIC_KEY_ID UINT32_C(7)" in header
    assert header.count("0x") == 64
    overwrite = subprocess.run(
        [sys.executable, str(FIRMWARE_TOOLS / "public_key_header.py"),
         str(public_path), str(header_path)],
        capture_output=True,
    )
    assert overwrite.returncode != 0
