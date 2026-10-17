#!/usr/bin/env python3
"""Create a deterministic JSON release manifest from verified MMFW packages."""

import argparse
import pathlib

from mmfw import PackageError, deterministic_json, package_record


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=pathlib.Path)
    parser.add_argument("packages", nargs="+", type=pathlib.Path)
    args = parser.parse_args()
    try:
        records = [package_record(path) for path in args.packages]
    except (OSError, PackageError) as error:
        parser.error(str(error))
    records.sort(key=lambda item: (item["target_id"], item["hardware_id"], item["file"]))
    release = {"format": "multi-mcu-release", "format_version": 1, "images": records}
    args.output.write_text(deterministic_json(release), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
