#!/usr/bin/env python3
"""Convert an external P-256 public PEM key to a bootloader C header."""

import argparse
import pathlib

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("public_key", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    parser.add_argument("--key-id", type=lambda value: int(value, 0), default=1)
    args = parser.parse_args()
    if args.output.exists():
        parser.error("refusing to overwrite output")
    key = serialization.load_pem_public_key(args.public_key.read_bytes())
    if not isinstance(key, ec.EllipticCurvePublicKey) or not isinstance(
        key.curve, ec.SECP256R1
    ):
        parser.error("public key must use P-256 (secp256r1)")
    numbers = key.public_numbers()
    raw = numbers.x.to_bytes(32, "big") + numbers.y.to_bytes(32, "big")
    rows = [
        "    " + ", ".join(f"0x{byte:02X}U" for byte in raw[i : i + 8])
        for i in range(0, len(raw), 8)
    ]
    body = ", \\\n".join(rows)
    args.output.write_text(
        "#ifndef MULTI_MCU_BOOT_PUBLIC_KEY_H\n"
        "#define MULTI_MCU_BOOT_PUBLIC_KEY_H\n"
        f"#define BOOTLOADER_PUBLIC_KEY_ID UINT32_C({args.key_id})\n"
        "#define BOOTLOADER_PUBLIC_KEY_BYTES { \\\n"
        f"{body} \\\n"
        "}\n"
        "#endif\n",
        encoding="ascii",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
