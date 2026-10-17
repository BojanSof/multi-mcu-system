#!/usr/bin/env python3
"""Pack a raw binary into an unsigned Phase 6 MMFW container."""

import argparse
import pathlib

from mmfw import PackageError, build_package


def pair(value: str) -> tuple[int, int]:
    parts = value.split(".")
    if len(parts) != 2:
        raise argparse.ArgumentTypeError("expected MAJOR.MINOR")
    return int(parts[0], 0), int(parts[1], 0)


def version(value: str) -> tuple[int, int, int]:
    parts = value.split(".")
    if len(parts) != 3:
        raise argparse.ArgumentTypeError("expected MAJOR.MINOR.PATCH")
    return tuple(int(part, 0) for part in parts)  # type: ignore[return-value]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    parser.add_argument("--target-id", required=True, type=lambda value: int(value, 0))
    parser.add_argument("--hardware-id", required=True, type=lambda value: int(value, 0))
    parser.add_argument("--version", required=True, type=version)
    parser.add_argument("--build-id", required=True, type=lambda value: int(value, 0))
    parser.add_argument("--load-address", required=True, type=lambda value: int(value, 0))
    parser.add_argument("--wire", required=True, type=pair)
    parser.add_argument("--management", required=True, type=pair)
    args = parser.parse_args()
    try:
        result = build_package(
            args.input.read_bytes(),
            target_id=args.target_id,
            hardware_id=args.hardware_id,
            version=args.version,
            build_id=args.build_id,
            load_address=args.load_address,
            required_wire=args.wire,
            required_management=args.management,
        )
    except (OSError, PackageError) as error:
        parser.error(str(error))
    args.output.write_bytes(result)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
