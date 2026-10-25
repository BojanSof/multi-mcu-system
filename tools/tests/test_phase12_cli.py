"""Deterministic fake-peer acceptance; never opens a hardware serial device."""

import argparse
import importlib.util
import json
import pathlib
import struct
import sys

import pytest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from cli import demo, mmctl
from cli.smp import SmpError


class Clock:
    value = 0.0

    def now(self):
        return self.value

    def sleep(self, value):
        self.value += value


class Peer:
    host_boot_id = 9

    def __init__(self, clock, **flags):
        self.clock, self.flags = clock, flags
        self.history, self.writes = [], []
        self.next_id = self.next_operation = self.sample_index = 0
        self.injected = self.async_emitted = False
        self.lifecycle = 2
        self.session = 77

    def record(self, operation, service, code, data, kind="response", status=0):
        self.next_id += 2 if self.flags.get("gap") else 1
        record = {"id": self.next_id, "session": self.session, "operation": operation,
                  "service": service, "code": code, "data": data, "kind": kind, "status": status}
        self.history.append(record)
        return record

    def request(self, group, command, data=None, **kwargs):
        assert group == 64
        self.clock.value += .005
        if command == 0:
            return {"api": 1, "host_boot_id": 10 if self.flags.get("host_changed") else 9,
                    "session": self.session, "state": "online", "metrics": True,
                    "busy": self.flags.get("busy", False)}
        if command == 1:
            services = [3, 0x1001]
            if not self.flags.get("no_actuator"):
                services.append(0x1002)
            if self.flags.get("diagnostics"):
                services.append(2)
            return {"services": [{"id": service} for service in services]}
        if command == 3:
            cursor = (data or {}).get("after", 0)
            page = [event for event in self.history if event["id"] > cursor][:4]
            if (not page and self.flags.get("async_event") and not self.async_emitted and
                    self.next_operation >= self.flags.get("async_after", 103)):
                page = [self.record(0, 1, 1, b"\x01", kind="event")]
                self.async_emitted = True
            return {"host_boot_id": 9, "latest": self.next_id, "events": page, "dropped": 500}
        if command == 7:
            counters = {name: 0 for name in ("requests", "retries", "responses", "timeouts",
                "protocol_errors", "uart_errors", "rx_bytes", "tx_bytes", "rx_overflows",
                "tx_overflows", "events", "dropped", "manager_restarts", "online_count",
                "session_changes", "ready_losses", "node_reboots")}
            counters.update(host_boot_id=9, requests=self.next_operation,
                            responses=self.next_operation, dropped=self.next_id)
            if self.flags.get("uart_errors") and self.next_operation > 2:
                counters["uart_errors"] = 1
            return counters
        assert command == 2 and kwargs.get("write")
        self.writes.append(data)
        self.next_operation += 1
        operation = 1 if self.flags.get("duplicate_operation") else self.next_operation
        service, code = data["service"], data["command"]
        if service == 3:
            if code in (2, 3):
                self.lifecycle = 3 if code == 2 else 2
            payload = (struct.pack("<HHHBBHHI", 0, 6, 0, 1, 0, 1, 0, 1) if code == 4 else
                       struct.pack("<BBHII", self.lifecycle, 0, 0, 1, 1))
        elif service == 0x1001:
            self.sample_index = (self.sample_index + (2 if self.flags.get("skipped_sample") else 1)) & 0xffffffff
            index = 0 if self.flags.get("duplicate_sample") else self.sample_index
            payload = struct.pack("<iI", -100, index)
            if self.flags.get("malformed_sensor"):
                payload = b"x"
        elif service == 0x1002:
            payload = struct.pack("<I", 42)
        else:
            if code == 2 and self.flags.get("unsupported_injection"):
                self.record(operation, service, code, b"", kind="error", status=6)
                return {"operation": operation}
            self.injected = code == 2
            payload = struct.pack("<IIIB", int(self.injected), 0, 0, 0)
        self.record(operation, service, code, payload)
        if service == 0x1002:
            if self.flags.get("interleaved_events"):
                self.record(operation, 2, 1, bytes(17), kind="event")
                self.record(operation, 1, 1, b"\x01", kind="event")
            for percentage in (25, 50, 75):
                self.record(operation, service, 1, bytes([percentage]) + payload, kind="event")
            if not self.flags.get("no_completion"):
                self.record(operation, service, 2, payload, kind="event")
        if self.flags.get("session_changed"):
            self.session += 1
        return {"operation": operation}


def arguments(tmp_path, **kwargs):
    return argparse.Namespace(samples=3, watch_seconds=.2, report=tmp_path / "result.json",
                              exercise_controls=False, require_acceptance=False,
                              wait_timeout=.3, json=True, **kwargs)


@pytest.fixture
def clock(monkeypatch):
    value = Clock()
    monkeypatch.setattr(demo.time, "monotonic", value.now)
    monkeypatch.setattr(demo.time, "sleep", value.sleep)
    return value


def test_demo_success_waits_for_actuator_completion_and_labels_quiet_watch(tmp_path, clock):
    client = Peer(clock)
    result = demo.run(client, arguments(tmp_path), mmctl, mode="simulated")
    assert result["status"] == "PASS"
    assert result["mode"] == "simulated" and result["sensor_responses"] == 3
    assert result["checks"]["actuator_progress_completion"] == "PASS"
    assert result["checks"]["event_watch"] == "NOT_OBSERVED"
    assert not result["acceptance_complete"]
    assert result["metric_deltas"]["dropped"] > 0  # history overwrite isn't unread loss
    assert all(request["service"] in (3, 0x1001, 0x1002) for request in client.writes)
    assert json.loads((tmp_path / "result.json").read_text())["status"] == "PASS"


def test_demo_progress_goes_to_stderr_not_json_stdout(tmp_path, clock, capsys):
    demo.run(Peer(clock), arguments(tmp_path), mmctl, mode="simulated")
    captured = capsys.readouterr()
    assert "sequential sensor samples" in captured.err and "STM user button" in captured.err
    assert json.loads(captured.out)["status"] == "PASS"


def test_actuator_allows_other_service_events_with_same_operation(tmp_path, clock):
    result = demo.run(Peer(clock, interleaved_events=True), arguments(tmp_path),
                      mmctl, mode="simulated")
    assert result["status"] == "PASS"
    assert result["checks"]["actuator_progress_completion"] == "PASS"


def test_response_service_mismatch_still_fails(clock):
    class WrongServicePeer(Peer):
        def record(self, operation, service, code, data, kind="response", status=0):
            return super().record(operation, service + 1 if kind == "response" else service,
                                  code, data, kind, status)

    peer = WrongServicePeer(clock)
    observer = demo.Observer(peer, mmctl, peer.session, 0)
    with pytest.raises(SmpError, match="response service mismatch"):
        observer.call(0x1001, 1)


def test_sensor_sample_index_wrap_is_contiguous(tmp_path, clock):
    client = Peer(clock)
    client.sample_index = 0xfffffffe
    assert demo.run(client, arguments(tmp_path), mmctl, mode="simulated")["sensor_responses"] == 3


@pytest.mark.parametrize("flag,error", [
    ("busy", "online idle"), ("host_changed", "online idle"),
    ("gap", "event gap"), ("duplicate_operation", "duplicate admitted"),
    ("session_changed", "session/link changed"), ("malformed_sensor", "malformed sensor"),
    ("duplicate_sample", "duplicate sensor"), ("uart_errors", "transport errors"),
    ("skipped_sample", "skipped sensor"),
    ("no_completion", "incomplete"),
])
def test_demo_failure_never_becomes_pass(tmp_path, clock, flag, error):
    with pytest.raises(SmpError, match=error):
        demo.run(Peer(clock, **{flag: True}), arguments(tmp_path), mmctl, mode="simulated")
    result = json.loads((tmp_path / "result.json").read_text())
    assert result["status"] == "FAIL" and not result["acceptance_complete"]


def test_full_acceptance_requires_watch_duration_samples_and_observed_events(tmp_path, clock):
    args = arguments(tmp_path)
    args.samples, args.watch_seconds, args.require_acceptance = 100, 600, True
    result = demo.run(Peer(clock, async_event=True), args, mmctl, mode="simulated")
    assert result["sensor_responses"] == 100 and result["acceptance_complete"]


def test_strict_smoke_incomplete_is_failure(tmp_path, clock):
    args = arguments(tmp_path)
    args.require_acceptance = True
    with pytest.raises(SmpError, match="full acceptance incomplete"):
        demo.run(Peer(clock), args, mmctl, mode="simulated")


def test_optional_capability_explicitly_not_supported(tmp_path, clock):
    result = demo.run(Peer(clock, no_actuator=True), arguments(tmp_path), mmctl, mode="simulated")
    assert result["checks"]["actuator_progress_completion"] == "NOT_SUPPORTED"


@pytest.mark.parametrize("unsupported", [False, True])
def test_optional_controls_cleanup_fault_and_verify_reset(tmp_path, clock, monkeypatch, unsupported):
    client = Peer(clock, diagnostics=True, unsupported_injection=unsupported)
    args = arguments(tmp_path)
    args.exercise_controls = True
    def reset(peer, command, action):
        assert peer is client and command == 4 and action == "reset"
        peer.session += 1
        return 999
    monkeypatch.setattr(demo.stm, "mutate", reset)
    monkeypatch.setattr(demo.stm, "status", lambda *_: {"state": "complete", "error": 0})
    report = demo.run(client, args, mmctl, mode="simulated")
    assert report["checks"]["reset_recovery"] == "PASS"
    assert report["checks"]["fault_exercise"] == ("NOT_SUPPORTED" if unsupported else "PASS")
    assert not client.injected and client.lifecycle == 2


@pytest.mark.parametrize("command", [2, 3])
def test_maintenance_waits_for_same_session_after_ready_pulse(clock, command):
    class TransitionPeer(Peer):
        transitions = None

        def request(self, group, code, data=None, **kwargs):
            result = super().request(group, code, data, **kwargs)
            if code == 2 and data["service"] == 3:
                self.transitions = [("discovering", 0), ("identity", self.session),
                                    ("capabilities", self.session), ("services", self.session)]
            elif code == 0 and self.transitions:
                state, session = self.transitions.pop(0)
                result.update(state=state, session=session)
            return result

    peer = TransitionPeer(clock)
    observer = demo.Observer(peer, mmctl, peer.session, 0)
    observer.call(3, command, lifecycle_transition=True)
    assert peer.transitions == []
    assert observer.status()["session"] == peer.session


def test_maintenance_transition_does_not_allow_new_session(clock):
    peer = Peer(clock, session_changed=True)
    observer = demo.Observer(peer, mmctl, peer.session, 0)
    with pytest.raises(SmpError, match="session/link changed"):
        observer.call(3, 2, lifecycle_transition=True)


@pytest.mark.parametrize("state", ["identity", "capabilities", "services", "online"])
def test_maintenance_discovery_rejects_different_nonzero_session(clock, state):
    class WrongSessionPeer(Peer):
        def request(self, group, code, data=None, **kwargs):
            value = super().request(group, code, data, **kwargs)
            if code == 0:
                value.update(state=state, session=self.session + 1)
            return value

    peer = WrongSessionPeer(clock)
    with pytest.raises(SmpError, match="session/link changed"):
        demo.Observer(peer, mmctl, peer.session, 0).status(lifecycle_transition=True)


def test_event_cursor_wrap_skips_zero():
    class Client:
        host_boot_id = 9
    class Api:
        @staticmethod
        def events(*_):
            return {"host_boot_id": 9, "events": [{"id": 1, "session": 77, "kind": "event"}]}
    observer = demo.Observer(Client(), Api, 77, 0xffffffff)
    assert observer.batch()[0]["id"] == 1


@pytest.mark.parametrize("record,error", [(None, "malformed event"),
    ({"id": 1, "session": 77, "kind": "unknown"}, "malformed event"),
    ({"id": 1, "session": 77, "kind": "response", "operation": 9}, "unadmitted operation")])
def test_observer_rejects_unexplained_or_malformed_records(record, error):
    class Client:
        host_boot_id = 9
    class Api:
        @staticmethod
        def events(*_):
            return {"host_boot_id": 9, "events": [record]}
    with pytest.raises(SmpError, match=error):
        demo.Observer(Client(), Api, 77, 0).batch()


def test_operator_interrupt_persists_failure_report(tmp_path, clock, monkeypatch):
    def interrupt(_):
        raise KeyboardInterrupt()
    monkeypatch.setattr(demo.time, "sleep", interrupt)
    with pytest.raises(KeyboardInterrupt):
        demo.run(Peer(clock), arguments(tmp_path), mmctl, mode="simulated")
    result = json.loads((tmp_path / "result.json").read_text())
    assert result["status"] == "FAIL" and "interrupted by operator" in result["error"]


def test_controls_require_yes_before_serial_discovery(monkeypatch):
    monkeypatch.setattr(mmctl, "devices", lambda: pytest.fail("must not discover without --yes"))
    assert mmctl.main(["demo", "--exercise-controls"]) == 1


@pytest.mark.parametrize("samples,watch", [(0, 1), (10001, 1), (1, -1), (1, float("nan"))])
def test_demo_bounds(tmp_path, clock, samples, watch):
    args = arguments(tmp_path)
    args.samples, args.watch_seconds = samples, watch
    with pytest.raises(SmpError):
        demo.run(Peer(clock), args, mmctl, mode="simulated")


def test_identity_reconnect_changes_path_only_for_same_usb_identity():
    original = {"port": "/dev/ttyACM2", "serial": "dk-123", "vid": 0x1366,
                "pid": 0x1069, "interface": "VCOM"}
    identity = mmctl.port_identity("/dev/ttyACM2", [original])
    assert mmctl.reconnect_port(identity, [{**original, "port": "/dev/ttyACM5"}]) == "/dev/ttyACM5"
    with pytest.raises(SmpError, match="original USB identity"):
        mmctl.reconnect_port(identity, [{**original, "serial": "another-kit"}])
    with pytest.raises(SmpError):
        mmctl.reconnect_port(identity, [original, {**original, "port": "/dev/ttyACM6"}])
    assert mmctl.reconnect_port(mmctl.port_identity("/dev/unknown", []), []) == "/dev/unknown"


def load_matrix_module():
    path = pathlib.Path(__file__).resolve().parents[2] / "tests/hil/acceptance_matrix.py"
    spec = importlib.util.spec_from_file_location("acceptance_matrix", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_hil_default_is_noop_and_all_pending(monkeypatch, capsys):
    module = load_matrix_module()
    monkeypatch.setattr(module.subprocess, "run", lambda *a, **k: pytest.fail("must not run hardware"))
    assert module.main([]) == 0
    result = json.loads(capsys.readouterr().out)
    assert result["mode"] == "not_run"
    assert all(item["status"] == "NOT_RUN_HARDWARE" for item in result["scenarios"].values())


def test_hil_requires_electrical_gate(monkeypatch):
    module = load_matrix_module()
    monkeypatch.setattr(module.subprocess, "run", lambda *a, **k: pytest.fail("must not open serial"))
    assert module.main(["--run-hardware", "--port", "/dev/fake"]) == 1


def test_hil_report_paths_cannot_alias(tmp_path):
    module = load_matrix_module()
    same = str(tmp_path / "same.json")
    with pytest.raises(SystemExit):
        module.main(["--report", same, "--runtime-report", same])
    assert not (tmp_path / "same.json").exists()


@pytest.mark.parametrize("payload,returncode", [([], 0), ({"status": "PASS"}, 0),
    ({"mode": "hardware", "status": "PASS", "acceptance_complete": True}, 1)])
def test_hil_does_not_accept_invalid_or_failed_process_evidence(tmp_path, monkeypatch, capsys,
                                                              payload, returncode):
    module = load_matrix_module()
    output = tmp_path / "runtime.json"
    def fake_process(*args, **kwargs):
        assert kwargs["timeout"] == 3600
        output.write_text(json.dumps(payload))
        return argparse.Namespace(returncode=returncode)
    monkeypatch.setattr(module.subprocess, "run", fake_process)
    assert module.main(["--run-hardware", "--electrical-verified", "--port", "/dev/fake",
                        "--runtime-report", str(output)]) == 1
    report = json.loads(capsys.readouterr().out)
    assert report["scenarios"]["runtime_100_samples_10_minutes"]["status"] == "FAIL"


def test_hil_process_timeout_is_failure(tmp_path, monkeypatch, capsys):
    module = load_matrix_module()
    def fail_process(command, **kwargs):
        raise module.subprocess.TimeoutExpired(command, kwargs["timeout"])
    monkeypatch.setattr(module.subprocess, "run", fail_process)
    assert module.main(["--run-hardware", "--electrical-verified", "--port", "/dev/fake",
                        "--runtime-report", str(tmp_path / "runtime.json")]) == 1
    report = json.loads(capsys.readouterr().out)
    assert report["scenarios"]["runtime_100_samples_10_minutes"]["status"] == "FAIL"


def test_metrics_read_only_and_boot_guard():
    class Client:
        host_boot_id = 3
        def request(self, group, command, **kwargs):
            assert (group, command) == (64, 7) and not kwargs.get("write")
            return {"host_boot_id": 4}
    with pytest.raises(SmpError, match="metrics host boot changed"):
        mmctl.execute(Client(), argparse.Namespace(command="metrics", json=True))
