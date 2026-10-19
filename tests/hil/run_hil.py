#!/usr/bin/env python3
"""Run a simulated check or explicitly authorized, observation-only HIL check."""

from __future__ import annotations

import argparse
import json
import pathlib
import sys

from hil_runner import Result, evaluate, load_matrix, observe_serial


HERE = pathlib.Path(__file__).resolve().parent


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Observe two serial logs; this command never flashes or resets hardware."
    )
    parser.add_argument("--matrix", type=pathlib.Path, default=HERE / "matrix.json")
    parser.add_argument("--scenario", required=True)
    parser.add_argument("--run-hardware", action="store_true",
                        help="explicitly opt in to opening the two serial devices")
    parser.add_argument("--electrical-verified", action="store_true",
                        help="confirm wiring and compatible 3.3 V Nordic VDD:IO were checked")
    parser.add_argument("--host-port", type=pathlib.Path)
    parser.add_argument("--node-port", type=pathlib.Path)
    parser.add_argument("--duration", type=float)
    parser.add_argument("--fixture-host", type=pathlib.Path,
                        help="simulation only: host log fixture")
    parser.add_argument("--fixture-node", type=pathlib.Path,
                        help="simulation only: node log fixture")
    parser.add_argument("--output", type=pathlib.Path,
                        help="optional JSON result path (logs are never written)")
    return parser.parse_args()


def emit(result: Result, output: pathlib.Path | None) -> int:
    line = result.to_json()
    print(line)
    if output is not None:
        output.write_text(line + "\n", encoding="utf-8")
    return 0 if result.status in {"PASS", "NOT_RUN_HARDWARE"} else 1


def main() -> int:
    args = parse_args()
    try:
        matrix = load_matrix(args.matrix)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        return emit(Result(args.scenario, "BLOCKED_TOOL", "none", str(error), {}, 0.0), args.output)
    if args.scenario not in matrix:
        return emit(Result(args.scenario, "BLOCKED_TOOL", "none", "unknown scenario", {}, 0.0), args.output)
    scenario = matrix[args.scenario]
    fixtures = args.fixture_host is not None or args.fixture_node is not None
    if fixtures:
        if args.run_hardware or args.fixture_host is None or args.fixture_node is None:
            return emit(Result(args.scenario, "BLOCKED_TOOL", "simulated",
                               "provide both fixtures and do not combine fixtures with --run-hardware",
                               {}, 0.0), args.output)
        try:
            logs = {
                "host": args.fixture_host.read_text(encoding="utf-8"),
                "node": args.fixture_node.read_text(encoding="utf-8"),
            }
        except OSError as error:
            return emit(Result(args.scenario, "BLOCKED_TOOL", "simulated", str(error), {}, 0.0), args.output)
        return emit(evaluate(args.scenario, scenario, logs, "simulated fixture"), args.output)
    if not args.run_hardware:
        return emit(Result(args.scenario, "NOT_RUN_HARDWARE", "none",
                           "pass --run-hardware with explicit --host-port and --node-port",
                           {}, 0.0), args.output)
    if not args.electrical_verified:
        return emit(Result(args.scenario, "BLOCKED_ELECTRICAL", "hardware",
                           "verify common GND and Nordic VDD:IO at 3.3 V first", {}, 0.0), args.output)
    if args.host_port is None or args.node_port is None or args.host_port == args.node_port:
        return emit(Result(args.scenario, "BLOCKED_TOOL", "hardware",
                           "two distinct explicit serial-device paths are required", {}, 0.0), args.output)
    duration = args.duration if args.duration is not None else float(scenario["duration_seconds"])
    if not 0.1 <= duration <= 300.0:
        return emit(Result(args.scenario, "BLOCKED_TOOL", "hardware",
                           "duration must be between 0.1 and 300 seconds", {}, 0.0), args.output)
    try:
        logs, elapsed = observe_serial({"host": args.host_port, "node": args.node_port}, duration)
    except (OSError, ValueError) as error:
        return emit(Result(args.scenario, "BLOCKED_TOOL", "hardware", str(error), {}, 0.0), args.output)
    return emit(evaluate(args.scenario, scenario, logs, "hardware serial observation", elapsed), args.output)


if __name__ == "__main__":
    sys.exit(main())
