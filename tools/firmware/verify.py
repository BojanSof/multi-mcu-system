#!/usr/bin/env python3
"""Verify MMFW structure, exact length, and payload SHA-256."""

import argparse
import pathlib

from mmfw import PackageError, parse_package


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=pathlib.Path)
    args = parser.parse_args()
    try:
        manifest, _ = parse_package(args.package.read_bytes())
    except (OSError, PackageError) as error:
        print(f"FAIL: {error}")
        return 1
    print(f"PASS: MMFW {manifest.version}, payload {manifest.payload_size} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
