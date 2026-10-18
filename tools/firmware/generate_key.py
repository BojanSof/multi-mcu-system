#!/usr/bin/env python3
"""Generate an external development-only P-256 signing key pair."""

import argparse
import pathlib

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("private_key", type=pathlib.Path)
    parser.add_argument("public_key", type=pathlib.Path)
    args = parser.parse_args()
    if args.private_key.exists() or args.public_key.exists():
        parser.error("refusing to overwrite an existing key")
    key = ec.generate_private_key(ec.SECP256R1())
    args.private_key.write_bytes(
        key.private_bytes(
            serialization.Encoding.PEM,
            serialization.PrivateFormat.PKCS8,
            serialization.NoEncryption(),
        )
    )
    args.public_key.write_bytes(
        key.public_key().public_bytes(
            serialization.Encoding.PEM,
            serialization.PublicFormat.SubjectPublicKeyInfo,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
