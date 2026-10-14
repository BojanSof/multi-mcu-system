#!/usr/bin/env python3
"""Collect and correlate bounded host/node logs without assuming synchronized MCU clocks."""

from __future__ import annotations

import argparse
import base64
import csv
import datetime as dt
import json
import os
import pathlib
import re
import selectors
import sys
import time
from dataclasses import dataclass
from typing import BinaryIO, Iterator, TextIO

MAX_DURATION_SECONDS = 3600.0
MAX_LINE_BYTES = 4096
CORRELATION_PATTERN = re.compile(
    rb"(?:correlation(?:_id)?|operation(?:_id)?|op_id)[=: ]+(0x[0-9a-fA-F]+|[0-9]+)"
)


@dataclass
class Input:
    source: str
    spec: str
    stream: BinaryIO
    close: bool
    buffer: bytearray


def validate_duration(value: str) -> float:
    duration = float(value)
    if not 0.0 < duration <= MAX_DURATION_SECONDS:
        raise argparse.ArgumentTypeError(f"duration must be > 0 and <= {MAX_DURATION_SECONDS:g} seconds")
    return duration


def extract_correlation(raw: bytes) -> int | None:
    match = CORRELATION_PATTERN.search(raw)
    return int(match.group(1), 0) if match else None


def record(source: str, raw: bytes, started_ns: int) -> dict[str, object]:
    now_ns = time.monotonic_ns()
    return {
        "record_type": "log",
        "collector_elapsed_ns": now_ns - started_ns,
        "collector_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "source": source,
        "correlation_id": extract_correlation(raw),
        "raw_line": raw.decode("utf-8", errors="replace"),
        "raw_base64": base64.b64encode(raw).decode("ascii"),
        "clock_synchronized": False,
    }


def metadata(duration: float) -> dict[str, object]:
    return {
        "record_type": "metadata",
        "schema": "multi-mcu-log-v1",
        "provenance": "COLLECTED",
        "duration_limit_seconds": duration,
        "clock_synchronized": False,
        "clock_note": "Collector receipt times correlate streams; MCU clocks were not synchronized.",
    }


def open_input(source: str, spec: str, baud: int) -> Input:
    kind, separator, value = spec.partition(":")
    if not separator or kind not in {"file", "serial"}:
        value = spec
        kind = "file" if pathlib.Path(spec).is_file() else "serial"
    if kind == "file":
        return Input(source, spec, open(value, "rb"), True, bytearray())
    if os.name != "posix":
        raise RuntimeError("stdlib serial input is supported on POSIX only; use file input on this host")
    import termios

    speeds = {9600: termios.B9600, 115200: termios.B115200}
    if baud not in speeds:
        raise ValueError("stdlib serial mode supports baud rates 9600 and 115200")
    descriptor = os.open(value, os.O_RDONLY | os.O_NONBLOCK | os.O_NOCTTY)
    attributes = termios.tcgetattr(descriptor)
    attributes[0] = 0
    attributes[1] = 0
    attributes[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    attributes[3] = 0
    attributes[4] = speeds[baud]
    attributes[5] = speeds[baud]
    termios.tcsetattr(descriptor, termios.TCSANOW, attributes)
    return Input(source, spec, os.fdopen(descriptor, "rb", buffering=0), True, bytearray())


def _lines_from_chunk(item: Input, chunk: bytes, eof: bool = False) -> Iterator[bytes]:
    item.buffer.extend(chunk)
    while b"\n" in item.buffer:
        raw, _, remainder = item.buffer.partition(b"\n")
        item.buffer[:] = remainder
        yield raw[:MAX_LINE_BYTES]
    if len(item.buffer) > MAX_LINE_BYTES:
        yield bytes(item.buffer[:MAX_LINE_BYTES])
        item.buffer.clear()
    if eof and item.buffer:
        yield bytes(item.buffer)
        item.buffer.clear()


def collect(inputs: list[Input], duration: float) -> Iterator[dict[str, object]]:
    started_ns = time.monotonic_ns()
    deadline = time.monotonic() + duration
    # SelectSelector supports both regular files and serial descriptors on POSIX.
    selector = selectors.SelectSelector()
    active = list(inputs)
    for item in active:
        selector.register(item.stream, selectors.EVENT_READ, item)
    try:
        while active and time.monotonic() < deadline:
            remaining = max(0.0, deadline - time.monotonic())
            for key, _ in selector.select(timeout=min(remaining, 0.05)):
                item = key.data
                chunk = item.stream.read(4096)
                if chunk:
                    for raw in _lines_from_chunk(item, chunk):
                        yield record(item.source, raw, started_ns)
                else:
                    for raw in _lines_from_chunk(item, b"", eof=True):
                        yield record(item.source, raw, started_ns)
                    selector.unregister(item.stream)
                    active.remove(item)
    finally:
        selector.close()
        for item in inputs:
            if item.close:
                item.stream.close()


def write_jsonl(output: TextIO, header: dict[str, object], records: Iterator[dict[str, object]]) -> None:
    output.write(json.dumps(header, sort_keys=True) + "\n")
    for item in records:
        output.write(json.dumps(item, sort_keys=True) + "\n")


def write_csv(output: TextIO, header: dict[str, object], records: Iterator[dict[str, object]]) -> None:
    fields = [
        "record_type", "schema", "provenance", "collector_elapsed_ns", "collector_utc",
        "source", "correlation_id", "raw_line", "raw_base64", "clock_synchronized", "clock_note",
    ]
    writer = csv.DictWriter(output, fieldnames=fields, extrasaction="ignore")
    writer.writeheader()
    writer.writerow(header)
    for item in records:
        writer.writerow(item)


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--host", required=True, help="host file/port, optionally file:PATH or serial:PORT")
    result.add_argument("--node", required=True, help="node file/port, optionally file:PATH or serial:PORT")
    result.add_argument("--duration", type=validate_duration, default=30.0)
    result.add_argument("--baud", type=int, default=115200)
    result.add_argument("--format", choices=("jsonl", "csv"), default="jsonl")
    result.add_argument("--output", default="-", help="output path or - for stdout")
    return result


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    try:
        inputs = [open_input("host", args.host, args.baud), open_input("node", args.node, args.baud)]
    except (OSError, RuntimeError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    output = sys.stdout if args.output == "-" else open(args.output, "w", encoding="utf-8", newline="")
    try:
        records = collect(inputs, args.duration)
        (write_jsonl if args.format == "jsonl" else write_csv)(output, metadata(args.duration), records)
    finally:
        if output is not sys.stdout:
            output.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
