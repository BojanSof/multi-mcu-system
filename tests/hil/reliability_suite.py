#!/usr/bin/env python3
"""Explicit real-board checks; omitted manual evidence is never converted to PASS."""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
SAFE_SUITES = ("startup", "compatibility", "watchdog", "recovery", "persistence", "events")
DESTRUCTIVE_SUITES = ("interruption", "trial-rollback")


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--suite", choices=("all", *SAFE_SUITES, *DESTRUCTIVE_SUITES), default="all")
    parser.add_argument("--run-hardware", action="store_true")
    parser.add_argument("--electrical-verified", action="store_true")
    parser.add_argument("--port")
    parser.add_argument("--report", type=pathlib.Path)
    parser.add_argument("--destructive", action="store_true")
    parser.add_argument("--yes", action="store_true")
    parser.add_argument("--manifest", type=pathlib.Path)
    parser.add_argument("--public-key", type=pathlib.Path)
    parser.add_argument("--key-id", type=int, default=1)
    parser.add_argument("--rescue-verified", action="store_true",
                        help="confirm matching-key ROM/SWD rescue was physically verified")
    parser.add_argument("--swd-serial", help="ST-LINK serial of the verified rescue probe")
    parser.add_argument("--slot", type=int, choices=(0, 1),
                        help="explicit inactive slot to erase in interruption test")
    return parser.parse_args(argv)


def guard(args):
    if not args.run_hardware:
        return
    if not args.electrical_verified or not args.port:
        raise ValueError("hardware requires --electrical-verified and explicit --port")
    if args.suite in DESTRUCTIVE_SUITES:
        if not (args.destructive and args.yes and args.rescue_verified and
                args.manifest and args.public_key and args.swd_serial):
            raise ValueError("destructive suite requires --destructive --yes --rescue-verified "
                             "--manifest --public-key and --swd-serial")
        if args.suite == "interruption" and args.slot is None:
            raise ValueError("interruption requires explicit --slot 0|1")


def command(args, *words):
    invocation = [str(ROOT / "scripts/mmctl"), "--port", args.port, "--json", *words]
    completed = subprocess.run(invocation, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, timeout=120, check=False)
    records = []
    for line in completed.stdout.splitlines():
        if line.strip():
            value = json.loads(line)
            if not isinstance(value, dict):
                raise ValueError("CLI returned a non-object record")
            records.append(value)
    if completed.returncode or not records:
        raise ValueError(f"mmctl {' '.join(words)} failed: "
                         f"{completed.stderr.strip() or records or completed.returncode}")
    return records


def passed(checks, name, evidence):
    checks[name] = {"status": "PASS", "evidence": evidence}


def manual(checks, name, instruction):
    checks[name] = {"status": "NOT_RUN_HARDWARE", "instruction": instruction}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def safe_suite(args, suite, checks):
    status = command(args, "status")[-1]
    boot_id = status.get("host_boot_id")
    require(type(boot_id) is int and boot_id != 0, "missing host boot identity")
    require(status.get("state") == "online", "node must be online before this suite")
    if suite == "startup":
        sample = command(args, "sensor", "sample")[-1]
        require("value_milli_units" in sample.get("decoded", {}), "sensor did not respond")
        passed(checks, "discovery_and_request", {"status": status, "sample": sample})
        manual(checks, "delayed_node_start", "Hold STM RESET while Nordic boots, release it, "
               "then rerun startup. Keep the documented Nordic-first electrical power order.")
    elif suite == "compatibility":
        services = command(args, "services", "list")[-1]
        versions = command(args, "node", "versions")[-1].get("decoded", {})
        require(versions.get("wire", [None])[0] == 1 and
                versions.get("management", [None])[0] == 1, "incompatible base API")
        require(any(item.get("id") == 0x1001 and item.get("major") == 1
                    for item in services.get("services", [])), "sensor v1 unavailable")
        passed(checks, "compatible_versions_services", {"services": services, "versions": versions})
        manual(checks, "legacy_and_incompatible_builds", "Run both personalities, a legacy node, "
               "and debug-incompatible-service separately; verify unavailable services are rejected.")
    elif suite in ("watchdog", "recovery"):
        recovery = command(args, "node", "recovery", "status")[-1]
        require(recovery.get("host_boot_id") == boot_id, "host restarted during health query")
        require(recovery.get("health") in ("healthy", "degraded", "recovering", "failed"),
                "missing recovery health")
        require(type(recovery.get("recovery_attempts")) is int, "missing attempt count")
        if suite == "watchdog":
            require(recovery.get("watchdog") is True, "Nordic watchdog not active")
            passed(checks, "host_watchdog_enabled", recovery)
            manual(checks, "both_watchdogs_reset", "Use development stall injection or debugger "
                   "halt without watchdog pause; collect reset cause and new boot/session evidence.")
        else:
            passed(checks, "recovery_status", recovery)
            manual(checks, "bounded_reset_and_latch", "Make node unresponsive in a development "
                   "build; observe at most three pulses/60s and failed latch; restore and explicitly clear.")
    elif suite == "persistence":
        records = command(args, "node", "faults", "list")
        responses = [item for item in records if item.get("service") == 3 and item.get("code") == 10]
        require(responses and all(item.get("decoded", {}).get("healthy") is True
                                  for item in responses), "fault journal is unavailable/unhealthy")
        passed(checks, "journal_read", {"records": responses})
        manual(checks, "power_cycle_retention", "Save journal output, reset STM and read again; "
               "then repeat after a controlled power cycle using the documented power order.")
    elif suite == "events":
        before = command(args, "--yes", "service", "call", "3", "12")[-1].get("decoded", {})
        result = command(args, "demo", "--samples", "5", "--watch-seconds", "10")[-1]
        require(result.get("status") == "PASS" and
                result.get("checks", {}).get("actuator_progress_completion") == "PASS",
                "runtime/event demonstration failed")
        after_events = command(args, "--yes", "service", "call", "3", "12")[-1].get("decoded", {})
        require(all(type(before.get(key)) is int and type(after_events.get(key)) is int and
                    before[key] == after_events[key] for key in ("exhausted", "button_dropped")),
                "reliable event exhaustion/button loss changed or counters unavailable")
        passed(checks, "runtime_actuator_events", {"demo": result, "before": before, "after": after_events})
        manual(checks, "lost_irq_ack_and_legacy", "Use development injection to lose an IRQ/ACK; "
               "verify deduplication and queue counters, then repeat with a legacy node.")
    after = command(args, "status")[-1]
    require(after.get("host_boot_id") == boot_id, "host restarted during suite")
    require(after.get("session") == status.get("session"), "node session changed during suite")


def destructive_preflight(args):
    # Validate all frozen artifact bytes and trust configuration before opening a device.
    sys.path.insert(0, str(ROOT / "tools"))
    from firmware.release_bundle import load
    bundle = load(args.manifest, args.public_key, args.key_id)
    probes = subprocess.run(["STM32_Programmer_CLI", "-l", "stlink"], text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=20,
                            check=False)
    serial_match = re.search(r"(?<![A-Za-z0-9])" + re.escape(args.swd_serial) +
                             r"(?![A-Za-z0-9])", probes.stdout)
    require(probes.returncode == 0 and serial_match is not None,
            "verified rescue ST-LINK is not enumerated; no device opened")
    return bundle


def interrupted_transfer(args, bundle, checks):
    from cli import mmctl, stm
    from cli.smp import SmpClient
    image = bundle.slot_images[args.slot]
    with mmctl.open_serial(args.port, 115200, 3) as connection:
        client = SmpClient(connection, 3, 0)
        initial = client.request(64, 0)
        require(initial.get("api") == 1 and initial.get("stm_update"), "STM update unavailable")
        client.host_boot_id = initial.get("host_boot_id")
        require(type(client.host_boot_id) is int and client.host_boot_id != 0,
                "missing host boot identity")
        current = stm.status(client)
        require(not current.get("active"), "another STM update job is active")
        if current.get("confirmed_slot") in (0, 1):
            require(current["confirmed_slot"] != args.slot, "selected slot is confirmed")
        job = None
        try:
            job = stm.mutate(client, 5, "begin", header=image[:512], total=len(image))
            ready = stm.poll(client, job, time.monotonic() + 40, len(image), 0, 0,
                             lambda value: value["state"] == "ready")
            confirmed = ready.get("confirmed_slot")
            require(confirmed in (0, 1) and confirmed != args.slot,
                    "bootloader did not identify a protected confirmed slot")
            stm.mutate(client, 5, "chunk", job, offset=0, data=image[:256])
            written = stm.poll(client, job, time.monotonic() + 20, len(image), 0, 256,
                               lambda value: value["offset"] == 256)
            stm.mutate(client, 5, "abort", job)
            deadline = time.monotonic() + 40
            while time.monotonic() < deadline:
                terminal = stm.status(client, job)
                if not terminal.get("active"):
                    break
                time.sleep(0.1)
            else:
                raise ValueError("abort did not release update ownership")
            require(terminal.get("state") == "aborted" and
                    terminal.get("confirmed_slot") == confirmed, "confirmed slot changed")
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline:
                online = client.request(64, 0)
                require(online.get("host_boot_id") == client.host_boot_id,
                        "host restarted; no command replayed")
                if online.get("state") == "online":
                    break
                time.sleep(0.2)
            else:
                raise ValueError("confirmed application did not rediscover")
            passed(checks, "partial_transfer_abort", {"written": written, "terminal": terminal,
                                                       "runtime": online})
        finally:
            # Only an explicit same-boot/job pre-commit abort, never a reconnect or commit.
            if job is not None:
                try:
                    current = stm.status(client, job)
                    if current.get("active") and current.get("state") in ("ready", "writing", "erasing"):
                        stm.mutate(client, 5, "abort", job)
                except (RuntimeError, OSError):
                    pass
    manual(checks, "physical_power_interruption", "Abort is not power-loss proof. Manually "
           "interrupt at selected offsets with verified rescue, then inspect the confirmed slot.")


def main(argv=None):
    args = parse_args(argv)
    report = {"format": "multi-mcu-reliability", "format_version": 1,
              "mode": "hardware" if args.run_hardware else "not_run", "checks": {}}
    code = 0
    try:
        if args.report and args.report.exists():
            raise ValueError("report already exists; choose a fresh evidence path")
        guard(args)
        suites = SAFE_SUITES if args.suite == "all" else (args.suite,)
        if not args.run_hardware:
            for suite in suites:
                manual(report["checks"], suite, "Pass --run-hardware --electrical-verified --port.")
        elif args.suite in DESTRUCTIVE_SUITES:
            bundle = destructive_preflight(args)
            if args.suite == "interruption":
                interrupted_transfer(args, bundle, report["checks"])
            else:
                manual(report["checks"], "trial_rollback", "Install a deliberately non-confirming "
                       "signed inactive-slot application using the documented manual procedure; "
                       "record trial attempts and return to the original confirmed slot. "
                       "This runner does not invent or install a failing image.")
        else:
            for suite in suites:
                safe_suite(args, suite, report["checks"])
        report["status"] = ("NOT_RUN_HARDWARE" if any(
            check["status"] == "NOT_RUN_HARDWARE" for check in report["checks"].values()) else "PASS")
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        report["status"] = "FAIL"
        report["error"] = str(error)
        code = 1
    text = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.report and not args.report.exists():
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(text, encoding="utf-8")
    print(text, end="")
    return code


if __name__ == "__main__":
    raise SystemExit(main())
