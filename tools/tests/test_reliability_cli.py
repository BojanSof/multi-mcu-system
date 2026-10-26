"""Reliability CLI contracts with fake peers, no physical board operations."""

import pathlib
import struct
import sys

import pytest

sys.path.insert(0, str(pathlib.Path(__file__).parents[1]))
from cli import mmctl
from cli.smp import SmpError, SmpTimeout


def history(generation=1, found=True, healthy=True):
    header = struct.pack("<BBHII", found, healthy, 0, 2, 3)
    record = struct.pack("<7IHBB16s", generation, 4, 5, 6, 7, 8, 9, 10, 11, 12, bytes(16))
    return header + record


@pytest.mark.parametrize("arguments,yes", [
    (["node", "recovery", "status"], False),
    (["node", "recovery", "clear"], True),
    (["node", "faults", "list"], False),
    (["node", "faults", "clear"], True),
    (["node", "reset"], True),
])
def test_authorization(arguments, yes):
    assert mmctl.requires_yes(mmctl.parse_args(arguments)) == yes


@pytest.mark.parametrize("noun", ["recovery", "faults"])
def test_clear_authorization_before_opening_serial(monkeypatch, noun):
    monkeypatch.setattr(mmctl, "open_serial", lambda *args: pytest.fail("opened serial"))
    assert mmctl.main(["node", noun, "clear"]) == 1


class RecoveryPeer:
    host_boot_id = 17

    def __init__(self, mode="normal"):
        self.mode = mode
        self.calls = []

    def request(self, group, command, data=None, write=False, **kwargs):
        assert (group, command) == (64, 8)
        self.calls.append((write, data, kwargs))
        if not write:
            return {"host_boot_id": 18 if self.mode == "reboot" else 17,
                    "health": "failed", "failed": True}
        assert data["expected_boot_id"] == 17
        if self.mode in ("timeout", "reboot") and len(self.calls) == 1:
            raise SmpTimeout("lost admission")
        return {"request_id": data["request_id"] + (self.mode == "wrong_id"),
                "accepted": self.mode != "not_accepted"}


def test_recovery_read_is_nonmutating():
    peer = RecoveryPeer()
    assert mmctl.recovery_status(peer)["failed"] is True
    assert peer.calls == [(False, None, {"retry": False})]


def test_recovery_clear_exact_same_boot_retry():
    peer = RecoveryPeer("timeout")
    assert mmctl.recovery_clear(peer)["accepted"] is True
    assert [write for write, _, _ in peer.calls] == [True, False, True]
    assert peer.calls[0][1] == peer.calls[2][1]
    assert all(item[2] == {"retry": False} for item in peer.calls)


@pytest.mark.parametrize("mode", ["reboot", "wrong_id", "not_accepted"])
def test_recovery_rejects_restart_or_bad_admission(mode):
    peer = RecoveryPeer(mode)
    with pytest.raises(SmpError):
        mmctl.recovery_clear(peer)
    assert sum(write for write, _, _ in peer.calls) == 1


def test_fault_record_decoding():
    decoded = mmctl.decode_fault_history(history())
    assert decoded == {"present": True, "healthy": True, "overwritten": 2,
                       "storage_errors": 3, "record": {
                           "generation": 1, "boot_count": 4, "session": 5, "build": 6,
                           "time_ms": 7, "correlation": 8, "detail": 9,
                           "code": 10, "reset_reason": 11, "severity": 12}}
    event = mmctl.decode_event({"service": 3, "code": 10, "data": history()})
    assert event["decoded"] == decoded
    assert mmctl.decode_fault_history(history(found=False))["present"] is False
    assert mmctl.decode_fault_history(history(found=False)[:12], clear=True)["present"] is False


def test_reliable_event_status_decoding():
    result = mmctl.decode_event({"service": 3, "code": 12,
                                "data": struct.pack("<6I", 1, 2, 3, 4, 5, 6)})
    assert result["decoded"] == {"pending": 1, "rejected_admissions": 2, "retries": 3,
                                 "exhausted": 4, "button_dropped": 5, "next_id": 6}
    with pytest.raises(SmpError, match="malformed"):
        mmctl.decode_event({"service": 3, "code": 12, "data": b""})


@pytest.mark.parametrize("data,clear", [
    (b"", False), (bytes(59), False), (bytes(61), False),
    (b"\x02" + bytes(59), False), (b"\x00\x02" + bytes(58), False),
    (b"\x00\x00\x01" + bytes(57), False), (history()[:12], True),
])
def test_fault_malformed_response(data, clear):
    with pytest.raises(SmpError, match="malformed"):
        mmctl.decode_fault_history(data, clear=clear)


def test_fault_history_cursor_and_non_destructive_read(monkeypatch):
    seen = []
    def operation(client, args, service, command, data=b""):
        seen.append((service, command, data))
        cursor = struct.unpack("<I", data)[0]
        return {"service": 3, "code": 10, "session": 5,
                "data": history(cursor + 1, found=cursor < 2)}
    monkeypatch.setattr(mmctl, "service_operation", operation)
    mmctl.fault_history(object(), mmctl.parse_args(["node", "faults", "list"]))
    assert seen == [(3, 10, struct.pack("<I", cursor)) for cursor in (0, 1, 2)]


@pytest.mark.parametrize("mode", ["stuck", "session", "service"])
def test_fault_history_rejects_stale_or_mismatched_response(monkeypatch, mode):
    calls = []
    def operation(*args):
        calls.append(1)
        return {"service": 2 if mode == "service" else 3, "code": 10,
                "session": len(calls) if mode == "session" else 5,
                "data": history()}
    monkeypatch.setattr(mmctl, "service_operation", operation)
    with pytest.raises(SmpError):
        mmctl.fault_history(object(), mmctl.parse_args(["node", "faults", "list"]))
    assert len(calls) <= 2


def test_fault_clear_uses_explicit_service_command(monkeypatch):
    seen = []
    monkeypatch.setattr(mmctl, "service_operation", lambda *args: seen.append(args[2:]))
    mmctl.fault_history(object(), mmctl.parse_args(["node", "faults", "clear", "--yes"]))
    assert seen == [(3, 11)]
