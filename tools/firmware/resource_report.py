#!/usr/bin/env python3
"""Report actual ELF sections and individual compiler stack frames, not HIL timings."""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import subprocess


def parse_size(output):
    rows = []
    for line in output.splitlines():
        if not line.strip() or re.match(r"\s*text\s+data\s+bss\b", line):
            continue
        match = re.fullmatch(r"\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+([0-9a-fA-F]+)\s+(.+?)\s*", line)
        if not match:
            raise ValueError("unrecognized size-tool output")
        text, data, bss, total = map(int, match.groups()[:4])
        if total != text + data + bss or int(match[5], 16) != total:
            raise ValueError("inconsistent size-tool totals")
        rows.append({"text": text, "data": data, "bss": bss,
                     "flash_bytes": text + data, "ram_bytes": data + bss})
    if len(rows) != 1:
        raise ValueError("expected exactly one ELF size record")
    return rows[0]


def parse_stack(path):
    frames = []
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip():
            continue
        parts = line.rsplit("\t", 2)
        if len(parts) != 3 or not parts[1].isdigit() or parts[2] not in (
                "static", "dynamic,bounded", "dynamic"):
            raise ValueError(f"malformed stack-usage record: {path}:{line_number}")
        location = re.fullmatch(r"(.+):(\d+):(\d+):(.+)", parts[0])
        if not location:
            raise ValueError(f"malformed stack location: {path}:{line_number}")
        frames.append({"source": location[1], "line": int(location[2]),
                       "function": location[4], "frame_bytes": int(parts[1]),
                       "kind": parts[2], "usage_file": str(path)})
    return frames


def report(elfs, size_tool="arm-none-eabi-size", stack_dir=None):
    if not elfs or len({name for name, _ in elfs}) != len(elfs):
        raise ValueError("at least one uniquely named ELF required")
    results = []
    for name, raw_path in elfs:
        if not name or not re.fullmatch(r"[A-Za-z0-9_.-]+", name):
            raise ValueError("invalid ELF name")
        path = pathlib.Path(raw_path)
        if not path.is_file():
            raise ValueError(f"ELF missing or invalid: {path}")
        with path.open("rb") as binary:
            if binary.read(4) != b"\x7fELF":
                raise ValueError(f"ELF missing or invalid: {path}")
        process = subprocess.run([str(size_tool), "--", str(path)], capture_output=True, text=True,
                                 timeout=30, check=True)
        results.append({"name": name, "file": str(path), **parse_size(process.stdout)})
    frames = []
    if stack_dir is not None:
        directory = pathlib.Path(stack_dir)
        if not directory.is_dir():
            raise ValueError("stack directory missing")
        paths = sorted(directory.rglob("*.su"))
        if not paths:
            raise ValueError("stack directory contains no .su reports")
        for path in paths:
            frames.extend(parse_stack(path))
    return {"format": "multi-mcu-resources", "format_version": 1,
            "mode": "static_build_artifacts", "elfs": results,
            "stack_frames": sorted(frames, key=lambda value: (-value["frame_bytes"],
                                                              value["source"], value["function"])),
            "stack_note": "Individual compiler frames only; not call-chain bounds or runtime high-water measurements.",
            "hardware_timings": "NOT_RUN_HARDWARE"}


def elf_argument(value):
    if "=" not in value:
        raise argparse.ArgumentTypeError("use NAME=PATH")
    name, path = value.split("=", 1)
    if not name or not path:
        raise argparse.ArgumentTypeError("use NAME=PATH")
    return name, pathlib.Path(path)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", action="append", type=elf_argument, required=True)
    parser.add_argument("--size-tool", default="arm-none-eabi-size")
    parser.add_argument("--stack-dir", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args(argv)
    try:
        result = report(args.elf, args.size_tool, args.stack_dir)
        output = json.dumps(result, sort_keys=True, indent=2) + "\n"
        if args.output:
            inputs = {path.resolve() for _, path in args.elf}
            if args.stack_dir:
                inputs.update(path.resolve() for path in args.stack_dir.rglob("*.su"))
            if args.output.is_symlink() or args.output.resolve() in inputs:
                raise ValueError("report output must not overwrite an input or symlink")
            args.output.write_text(output, encoding="utf-8")
        else:
            print(output, end="")
        return 0
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    raise SystemExit(main())
