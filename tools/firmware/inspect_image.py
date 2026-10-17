#!/usr/bin/env python3
"""Inspect and integrity-check an MMFW package."""

import argparse
import pathlib

from mmfw import PackageError, deterministic_json, parse_package


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=pathlib.Path)
    args = parser.parse_args()
    try:
        manifest, _ = parse_package(args.package.read_bytes())
    except (OSError, PackageError) as error:
        parser.error(str(error))
    print(deterministic_json(manifest.to_dict()), end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
