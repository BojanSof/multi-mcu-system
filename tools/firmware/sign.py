#!/usr/bin/env python3
"""Create a deterministic signed MMFW v2 STM32 slot image."""

import argparse
import pathlib

from cryptography.hazmat.primitives import serialization

from mmfw import PackageError, build_signed_image
from pack import pair, version


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    parser.add_argument("--private-key", required=True, type=pathlib.Path)
    parser.add_argument("--slot", required=True, type=int, choices=(0, 1))
    parser.add_argument("--version", required=True, type=version)
    parser.add_argument("--build-id", required=True, type=lambda value: int(value, 0))
    parser.add_argument("--wire", required=True, type=pair)
    parser.add_argument("--management", required=True, type=pair)
    parser.add_argument("--key-id", type=lambda value: int(value, 0), default=1)
    args = parser.parse_args()
    try:
        private_key = serialization.load_pem_private_key(
            args.private_key.read_bytes(), password=None
        )
        vector_address = 0x08010200 if args.slot == 0 else 0x08026200
        image = build_signed_image(
            args.input.read_bytes(),
            private_key,
            slot_id=args.slot,
            target_id=2,
            hardware_id=0x00830001,
            version=args.version,
            build_id=args.build_id,
            load_address=vector_address,
            required_wire=args.wire,
            required_management=args.management,
            key_id=args.key_id,
        )
        args.output.write_bytes(image)
    except (OSError, ValueError, TypeError, PackageError) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
