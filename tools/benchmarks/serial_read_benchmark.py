#!/usr/bin/env python3
"""Informational pyserial loopback comparison; not MCU/UART throughput evidence."""

import argparse
import json
import pathlib
import platform
import statistics
import sys
import time

import serial

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from cli.smp import _read_available


def benchmark(iterations):
    payload = b"example-small-smp-reply\n"
    rows = []
    for name in ("fixed_read_256", "available_byte_read"):
        samples = []
        for _ in range(iterations):
            with serial.serial_for_url("loop://", timeout=.1) as connection:
                connection.write(payload)
                start = time.perf_counter()
                received = bytearray()
                while len(received) < len(payload):
                    received.extend(connection.read(256) if name == "fixed_read_256"
                                    else _read_available(connection))
                elapsed = (time.perf_counter() - start) * 1000
                if received != payload:
                    raise RuntimeError("loopback payload mismatch")
                samples.append(elapsed)
        rows.append({"strategy": name, "median_ms": statistics.median(samples),
                     "min_ms": min(samples), "max_ms": max(samples)})
    return {"mode": "host_loopback", "hardware_tested": False,
            "python": platform.python_version(), "pyserial": serial.VERSION,
            "iterations": iterations, "payload_bytes": len(payload),
            "serial_timeout_ms": 100, "results": rows,
            "note": "Host in-memory short-read overhead only; not firmware update speedup."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iterations", type=int, default=10)
    args = parser.parse_args()
    if not 1 <= args.iterations <= 1000:
        parser.error("iterations must be 1..1000")
    print(json.dumps(benchmark(args.iterations), sort_keys=True, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
