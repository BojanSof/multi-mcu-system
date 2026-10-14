import base64
import csv
import importlib.util
import json
import sys
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[2]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


collector = load("log_collector", ROOT / "tools/log-collector/log_collector.py")
runner = load("fault_runner", ROOT / "tools/fault-runner/fault_runner.py")


@pytest.mark.parametrize("value", ["0", "-1", "3600.1"])
def test_collector_rejects_unbounded_duration(value):
    with pytest.raises(Exception):
        collector.validate_duration(value)


def test_collector_preserves_raw_and_marks_unsynchronized(tmp_path):
    host = tmp_path / "host.log"
    node = tmp_path / "node.log"
    host.write_bytes(b"request operation_id=42\n")
    node.write_bytes(b"response operation_id=42 \xff\n")
    output = tmp_path / "capture.jsonl"
    assert collector.main(["--host", f"file:{host}", "--node", f"file:{node}", "--duration", "1", "--output", str(output)]) == 0
    rows = [json.loads(line) for line in output.read_text().splitlines()]
    assert rows[0]["record_type"] == "metadata"
    assert rows[0]["clock_synchronized"] is False
    assert {row["source"] for row in rows[1:]} == {"host", "node"}
    assert {row["correlation_id"] for row in rows[1:]} == {42}
    assert base64.b64decode(rows[2]["raw_base64"]).endswith(b"\xff")


def test_csv_schema_has_clock_and_raw_fields(tmp_path):
    source = tmp_path / "empty.log"
    source.write_text("line\n")
    output = tmp_path / "capture.csv"
    collector.main(["--host", str(source), "--node", str(source), "--duration", "1", "--format", "csv", "--output", str(output)])
    rows = list(csv.DictReader(output.open()))
    assert rows[0]["schema"] == "multi-mcu-log-v1"
    assert rows[0]["clock_synchronized"] == "False"
    assert rows[1]["raw_base64"]


@pytest.mark.parametrize("scenario", tuple(runner.FAULTS))
def test_scenarios_are_deterministic_and_have_buggy_fixed_outcomes(scenario):
    first = runner.simulate(scenario, "buggy", 77, 1000)
    assert first == runner.simulate(scenario, "buggy", 77, 1000)
    fixed = runner.simulate(scenario, "fixed", 77, 1000)
    assert first["provenance"] == "SIMULATED"
    assert all(event["provenance"] == "SIMULATED" for event in first["events"])
    assert first["outcome"] == runner.FAULTS[scenario].expected_buggy
    assert fixed["outcome"] == runner.FAULTS[scenario].expected_fixed
    assert first["outcome"] != fixed["outcome"]


def test_duration_limits_events_and_rejects_excess():
    result = runner.simulate("delay_response", "buggy", 1, 100)
    assert all(event["time_ms"] <= 100 for event in result["events"])
    with pytest.raises(Exception):
        runner.duration_ms(str(runner.MAX_DURATION_MS + 1))


def test_csv_and_vcd_are_explicitly_simulated(tmp_path):
    result = runner.simulate("lost_irq", "buggy", 5, 1000)
    csv_path = tmp_path / "trace.csv"
    vcd_path = tmp_path / "trace.vcd"
    runner.write_csv(csv_path, result)
    runner.write_vcd(vcd_path, result)
    assert all(row["provenance"] == "SIMULATED" for row in csv.DictReader(csv_path.open()))
    assert "SIMULATED trace; not a hardware capture" in vcd_path.read_text()


def test_fault_enable_only_prints_operator_guidance(capsys):
    assert runner.main(["fault", "enable", "duplicate_operation"]) == 0
    output = capsys.readouterr().out
    assert "OPERATOR_ACTION_REQUIRED" in output
    assert "does not directly enable" in output
    assert "exactly 4 time(s)" in output


def test_all_firmware_injection_ids_are_listed_and_numeric_enable_works(capsys):
    assert set(runner.INJECTIONS) == set(range(1, 9))
    assert runner.main(["fault", "enable", "8"]) == 0
    output = capsys.readouterr().out
    assert "TASK_STALL" in output
    assert "enabled_mask=0x00000080" in output


def test_early_ready_guidance_uses_boot_profile_not_runtime_selection(capsys):
    assert runner.main(["fault", "enable", "EARLY_READY"]) == 0
    output = capsys.readouterr().out
    assert "debug-early-ready" in output
    assert "runtime selection cannot change boot timing" in output
    assert "Long-press" not in output


def test_fixed_duplicate_trace_has_one_execution_marker():
    result = runner.simulate("duplicate_operation", "fixed", 7, 1000)
    markers = [event for event in result["events"] if event["channel"] == "NODE_DEBUG"]
    assert [event["detail"] for event in markers] == ["execution_1"]
    assert any(event["detail"] == "cached_acceptance" for event in result["events"])
