#!/usr/bin/env python3
"""Opt-in runtime acceptance; destructive scenarios are operator evidence only."""

import argparse
import json
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCENARIOS = ("runtime_100_samples_10_minutes", "nordic_update", "stm_slot0_update",
             "stm_slot1_update", "rom_repair", "rollback", "interrupted_update",
             "unplug_reconnect")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run-hardware", action="store_true")
    parser.add_argument("--electrical-verified", action="store_true")
    parser.add_argument("--port")
    parser.add_argument("--report", type=pathlib.Path)
    parser.add_argument("--runtime-report", type=pathlib.Path,
                        default=ROOT / "build/phase12-runtime.json")
    args = parser.parse_args(argv)
    report = {"format": "multi-mcu-hil-acceptance", "mode": "not_run",
              "scenarios": {name: {"status": "NOT_RUN_HARDWARE", "evidence": None}
                            for name in SCENARIOS}}
    code = 0
    if args.report and args.report.resolve() == args.runtime_report.resolve():
        parser.error("--report must differ from --runtime-report")
    if args.run_hardware:
        if not args.electrical_verified or not args.port:
            report["error"] = "hardware requires --electrical-verified and explicit --port"
            code = 1
        else:
            args.runtime_report.parent.mkdir(parents=True, exist_ok=True)
            # Never reuse an old PASS report if the new process fails before opening serial.
            if args.runtime_report.exists():
                report["error"] = "runtime report already exists; choose a fresh --runtime-report"
                code = 1
            else:
                report["mode"] = "hardware"
                command = [str(ROOT / "scripts/mmctl"), "--port", args.port, "demo",
                           "--samples", "100", "--watch-seconds", "600",
                           "--require-acceptance", "--report", str(args.runtime_report)]
                try:
                    completed = subprocess.run(command, check=False, timeout=3600)
                    evidence = json.loads(args.runtime_report.read_text()) if args.runtime_report.exists() else {}
                    passed = (completed.returncode == 0 and isinstance(evidence, dict) and
                              evidence.get("mode") == "hardware" and
                              evidence.get("status") == "PASS" and evidence.get("acceptance_complete") is True)
                    report["scenarios"][SCENARIOS[0]] = {
                        "status": "PASS" if passed else "FAIL",
                        "evidence": str(args.runtime_report)}
                    code = 0 if passed else 1
                except (OSError, ValueError, subprocess.TimeoutExpired) as error:
                    report["scenarios"][SCENARIOS[0]] = {
                        "status": "FAIL", "evidence": str(args.runtime_report)
                        if args.runtime_report.exists() else None}
                    report["error"] = str(error)
                    code = 1
    report["note"] = ("Updates, ROM repair, rollback and reconnect require explicit operator "
                      "procedures/evidence; this runner never flashes, resets or changes options.")
    text = json.dumps(report, sort_keys=True, indent=2) + "\n"
    if args.report:
        args.report.write_text(text, encoding="utf-8")
    print(text, end="")
    return code


if __name__ == "__main__":
    sys.exit(main())
