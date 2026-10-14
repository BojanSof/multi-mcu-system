#!/usr/bin/env python3
"""Phase 4 fault operator guide and deterministic simulated trace generator."""

from __future__ import annotations

import argparse
import csv
import json
import pathlib
import random
import sys
from dataclasses import dataclass

MAX_DURATION_MS = 600_000
MIN_DURATION_MS = 10


@dataclass(frozen=True)
class Fault:
    name: str
    injection_id: int
    firmware_name: str
    expected_buggy: str
    expected_fixed: str


FAULTS = {
    "lost_irq": Fault("lost_irq", 2, "SHORT_IRQ", "event_waits_for_uart_poll", "level_irq_delivers_event"),
    "duplicate_operation": Fault("duplicate_operation", 4, "DROP_ACCEPTANCE", "retry_executes_twice", "cached_acceptance_no_reexecution"),
    "early_output": Fault("early_output", 7, "EARLY_VIRTUAL_OUTPUT", "output_before_prerequisite", "prerequisite_before_output"),
    "corrupt_uart": Fault("corrupt_uart", 5, "CORRUPT_UART", "crc_rejected", "clean_frame_accepted"),
    "truncate_uart": Fault("truncate_uart", 6, "TRUNCATE_UART", "partial_frame_times_out", "complete_frame_accepted"),
    "delay_response": Fault("delay_response", 3, "DELAY_RESPONSE", "bounded_retry_after_timeout", "response_before_timeout"),
}

INJECTIONS = {
    1: "EARLY_READY",
    2: "SHORT_IRQ",
    3: "DELAY_RESPONSE",
    4: "DROP_ACCEPTANCE",
    5: "CORRUPT_UART",
    6: "TRUNCATE_UART",
    7: "EARLY_VIRTUAL_OUTPUT",
    8: "TASK_STALL",
}


def duration_ms(value: str) -> int:
    parsed = int(value)
    if not MIN_DURATION_MS <= parsed <= MAX_DURATION_MS:
        raise argparse.ArgumentTypeError(f"duration must be {MIN_DURATION_MS}..{MAX_DURATION_MS} ms")
    return parsed


def operator_steps(injection_id: int) -> list[str]:
    if injection_id == 1:
        return [
            "Build and flash the STM debug-early-ready preset; runtime selection cannot change boot timing.",
            "Attach analyzer probes before resetting either board, using hardware/LOGIC_ANALYZER.md.",
            "Reset the STM32 node and confirm READY asserts before its first UART response.",
            "Reflash the normal debug preset after the lesson; do not use this profile as normal firmware.",
        ]
    return [
        "Flash development builds with MULTI_MCU_DEV_FAULTS enabled; do not use production firmware.",
        "Connect each board by its own USB debug cable. Do not alter inter-board wiring for this command.",
        "Reset the Nordic host to make its runtime fault selection start at 0.",
        f"Long-press the Nordic onboard button for at least 1500 ms exactly {injection_id} time(s), waiting for status after each press.",
        f"Confirm the host log reports requested injection {injection_id} and node enabled_mask=0x{1 << (injection_id - 1):08x}.",
        "Perform one short button press to run the demo operation, then inspect both independent logs.",
    ]


def fault_command(args: argparse.Namespace) -> int:
    if args.action == "list":
        aliases = {name: fault.injection_id for name, fault in FAULTS.items()}
        print(json.dumps({"injections": INJECTIONS, "scenario_aliases": aliases}, indent=2, sort_keys=True))
        return 0
    if args.action == "disable":
        print("OPERATOR_ACTION_REQUIRED: This tool does not directly control firmware.")
        print("Reset the STM32 node with its onboard reset button; runtime injection masks clear at reboot. Wait for Nordic rediscovery and verify enabled_mask=0 in the diagnostics status log. A Nordic long press cycles to another injection and is not a disable-only action.")
        return 0
    if args.action == "status":
        print("OPERATOR_ACTION_REQUIRED: inspect the host log line 'diagnostics status' and record enabled_mask; this CLI has no hardware-control transport.")
        return 0
    token = args.fault_id.strip()
    if token in FAULTS:
        injection_id = FAULTS[token].injection_id
    else:
        normalized = token.upper().replace("-", "_")
        matches = [number for number, name in INJECTIONS.items() if token == str(number) or normalized == name]
        if not matches:
            print(f"error: unknown injection {token!r}; run 'fault list'", file=sys.stderr)
            return 2
        injection_id = matches[0]
    print("OPERATOR_ACTION_REQUIRED: This tool does not directly enable hardware faults.")
    print(f"Selected firmware injection {injection_id}: {INJECTIONS[injection_id]}")
    for index, step in enumerate(operator_steps(injection_id), 1):
        print(f"{index}. {step}")
    return 0


def add_event(events: list[dict[str, object]], time_ms: int, channel: str, signal: str,
              value: int, detail: str) -> None:
    events.append({"provenance": "SIMULATED", "time_ms": time_ms, "channel": channel,
                   "signal": signal, "value": value, "detail": detail})


def simulate(name: str, mode: str, seed: int, limit_ms: int) -> dict[str, object]:
    fault = FAULTS[name]
    rng = random.Random(seed)
    jitter = rng.randint(1, 7)
    events: list[dict[str, object]] = []
    add_event(events, 0, "HOST_TX", "uart", 1, "request_start")
    if name == "lost_irq":
        add_event(events, 10 + jitter, "NODE_IRQ", "gpio", 0, "event_pending")
        width = 2 if mode == "buggy" else 40
        add_event(events, 10 + jitter + width, "NODE_IRQ", "gpio", 1, "short_pulse" if mode == "buggy" else "event_drained")
        add_event(events, 55 + jitter, "NODE_TX", "uart", 1, "uart_poll_recovers" if mode == "buggy" else "event_delivered")
    elif name == "duplicate_operation":
        add_event(events, 20 + jitter, "NODE_TX", "uart", 0, "acceptance_dropped" if mode == "buggy" else "acceptance")
        add_event(events, 21 + jitter, "NODE_DEBUG", "gpio", 1, "execution_1")
        add_event(events, 100 + jitter, "HOST_TX", "uart", 1, "retry_same_operation_id")
        if mode == "buggy":
            add_event(events, 120 + jitter, "NODE_DEBUG", "gpio", 1, "execution_2")
        else:
            add_event(events, 120 + jitter, "NODE_TX", "uart", 1, "cached_acceptance")
    elif name == "early_output":
        first, second = (("output_enabled", "prerequisite_ready") if mode == "buggy" else
                         ("prerequisite_ready", "output_enabled"))
        add_event(events, 15 + jitter, "NODE_DEBUG", "gpio", 1, first)
        add_event(events, 25 + jitter, "NODE_DEBUG", "gpio", 0, second)
    elif name == "corrupt_uart":
        add_event(events, 20 + jitter, "NODE_TX", "uart", 1, "crc_rejected" if mode == "buggy" else "clean_frame_accepted")
    elif name == "truncate_uart":
        add_event(events, 20 + jitter, "NODE_TX", "uart", 1, "half_frame")
        add_event(events, 100 + jitter, "HOST_TX", "uart", 1, "partial_frame_timeout" if mode == "buggy" else "complete_frame_accepted")
    elif name == "delay_response":
        delay = 650 if mode == "buggy" else 40
        add_event(events, delay + jitter, "NODE_TX", "uart", 1, "late_response" if mode == "buggy" else "response")
    events = [event for event in events if int(event["time_ms"]) <= limit_ms]
    return {
        "schema": "multi-mcu-simulated-fault-v1", "provenance": "SIMULATED",
        "scenario": name, "firmware_injection_id": fault.injection_id, "mode": mode,
        "seed": seed, "duration_limit_ms": limit_ms,
        "outcome": fault.expected_buggy if mode == "buggy" else fault.expected_fixed,
        "events": events,
    }


def write_csv(path: pathlib.Path, result: dict[str, object]) -> None:
    with path.open("w", encoding="utf-8", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=("provenance", "scenario", "mode", "seed", "time_ms", "channel", "signal", "value", "detail"))
        writer.writeheader()
        for event in result["events"]:
            writer.writerow({**event, "scenario": result["scenario"], "mode": result["mode"], "seed": result["seed"]})


def write_vcd(path: pathlib.Path, result: dict[str, object]) -> None:
    channels = {"HOST_TX": "!", "NODE_TX": '"', "NODE_IRQ": "#", "NODE_DEBUG": "$"}
    lines = ["$comment SIMULATED trace; not a hardware capture $end", "$timescale 1ms $end", "$scope module multi_mcu $end"]
    for name, symbol in channels.items():
        lines.append(f"$var wire 1 {symbol} {name} $end")
    lines.extend(("$upscope $end", "$enddefinitions $end", "#0"))
    state = {name: 0 for name in channels}
    for name, symbol in channels.items():
        lines.append(f"0{symbol}")
    for event in sorted(result["events"], key=lambda item: int(item["time_ms"])):
        channel = str(event["channel"])
        if channel not in channels:
            continue
        state[channel] = int(event["value"])
        lines.extend((f"#{event['time_ms']}", f"{state[channel]}{channels[channel]}", f"$comment SIMULATED {event['detail']} $end"))
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def make_parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    commands = result.add_subparsers(dest="command", required=True)
    fault = commands.add_parser("fault", help="print bounded operator guidance; never controls hardware directly")
    fault_commands = fault.add_subparsers(dest="action", required=True)
    fault_commands.add_parser("list")
    enable = fault_commands.add_parser("enable")
    enable.add_argument("fault_id", help="numeric ID, firmware name, or scenario alias")
    fault_commands.add_parser("disable")
    fault_commands.add_parser("status")
    scenario = commands.add_parser("scenario", help="generate deterministic SIMULATED evidence")
    scenario.add_argument("scenario", choices=tuple(FAULTS))
    scenario.add_argument("--mode", choices=("buggy", "fixed"), required=True)
    scenario.add_argument("--seed", type=int, default=1)
    scenario.add_argument("--duration-ms", type=duration_ms, default=1000)
    scenario.add_argument("--csv", type=pathlib.Path)
    scenario.add_argument("--vcd", type=pathlib.Path)
    return result


def main(argv: list[str] | None = None) -> int:
    args = make_parser().parse_args(argv)
    if args.command == "fault":
        return fault_command(args)
    result = simulate(args.scenario, args.mode, args.seed, args.duration_ms)
    if args.csv:
        write_csv(args.csv, result)
    if args.vcd:
        write_vcd(args.vcd, result)
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
