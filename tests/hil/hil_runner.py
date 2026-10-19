"""Safe serial observation and deterministic HIL result evaluation.

This module only opens already-present serial devices and reads bytes.  It does
not flash firmware, reset a board, or modify STM option bytes.
"""

from __future__ import annotations

import json
import os
import pathlib
import selectors
import termios
import time
from dataclasses import asdict, dataclass
from typing import Mapping, Sequence


VALID_STATUSES = {
    "PASS",
    "FAIL",
    "BLOCKED_TOOL",
    "BLOCKED_ELECTRICAL",
    "NOT_RUN_HARDWARE",
}


@dataclass(frozen=True)
class Result:
    scenario: str
    status: str
    execution: str
    detail: str
    missing: Mapping[str, Sequence[str]]
    elapsed_seconds: float

    def to_json(self) -> str:
        return json.dumps(asdict(self), sort_keys=True)


def load_matrix(path: pathlib.Path) -> dict[str, dict]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict) or not value:
        raise ValueError("scenario matrix must be a non-empty object")
    for name, scenario in value.items():
        if not isinstance(name, str) or not isinstance(scenario, dict):
            raise ValueError("invalid scenario entry")
        expect = scenario.get("expect")
        if not isinstance(expect, dict) or set(expect) != {"host", "node"}:
            raise ValueError(f"scenario {name!r} must define host and node expectations")
        for stream in ("host", "node"):
            if not isinstance(expect[stream], list) or not all(
                isinstance(pattern, str) and pattern for pattern in expect[stream]
            ):
                raise ValueError(f"scenario {name!r} has invalid {stream} expectations")
    return value


def evaluate(scenario_name: str, scenario: Mapping, logs: Mapping[str, str], execution: str,
             elapsed_seconds: float = 0.0) -> Result:
    missing = {
        stream: [pattern for pattern in scenario["expect"][stream] if pattern not in logs.get(stream, "")]
        for stream in ("host", "node")
    }
    missing = {stream: patterns for stream, patterns in missing.items() if patterns}
    status = "FAIL" if missing else "PASS"
    detail = "required log markers observed" if not missing else "required log markers missing"
    return Result(scenario_name, status, execution, detail, missing, elapsed_seconds)


def open_serial_readonly(device: pathlib.Path, baud: int = 115200) -> int:
    if baud != 115200:
        raise ValueError("only the project UART rate of 115200 baud is supported")
    fd = os.open(device, os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK)
    attrs = termios.tcgetattr(fd)
    attrs[0] = 0
    attrs[1] = 0
    attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    attrs[3] = 0
    attrs[4] = termios.B115200
    attrs[5] = termios.B115200
    attrs[6][termios.VMIN] = 0
    attrs[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    return fd


def observe_serial(devices: Mapping[str, pathlib.Path], duration: float) -> tuple[dict[str, str], float]:
    selector = selectors.DefaultSelector()
    descriptors: list[int] = []
    chunks: dict[str, list[bytes]] = {name: [] for name in devices}
    started = time.monotonic()
    try:
        for name, device in devices.items():
            fd = open_serial_readonly(device)
            descriptors.append(fd)
            selector.register(fd, selectors.EVENT_READ, name)
        deadline = started + duration
        while time.monotonic() < deadline:
            for key, _ in selector.select(min(0.2, max(0.0, deadline - time.monotonic()))):
                try:
                    data = os.read(key.fd, 4096)
                except BlockingIOError:
                    continue
                if data:
                    chunks[key.data].append(data)
    finally:
        selector.close()
        for fd in descriptors:
            os.close(fd)
    elapsed = time.monotonic() - started
    return ({name: b"".join(parts).decode("utf-8", errors="replace") for name, parts in chunks.items()}, elapsed)
