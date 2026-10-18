#!/usr/bin/env python3
"""Verify a signed MMFW v2 image using an external P-256 public key."""

import argparse
import pathlib

from cryptography.hazmat.primitives import serialization

from mmfw import PackageError, parse_signed_image


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=pathlib.Path)
    parser.add_argument("--public-key", required=True, type=pathlib.Path)
    args = parser.parse_args()
    try:
        public_key = serialization.load_pem_public_key(args.public_key.read_bytes())
        manifest, _ = parse_signed_image(args.image.read_bytes(), public_key=public_key)
    except (OSError, ValueError, TypeError, PackageError) as error:
        print(f"FAIL: {error}")
        return 1
    print(
        f"PASS: signed MMFW {manifest.version}, slot {manifest.slot_id}, "
        f"payload {manifest.payload_size} bytes"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
