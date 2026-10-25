"""Deterministic polling tests; no serial hardware or wall-clock sleeps."""

import pathlib
import sys

import pytest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from cli import demo, mmctl, polling, rom, stm
from cli.smp import SmpError


class Clock:
    def __init__(self):
        self.value = 0.0
        self.sleeps = []

    def now(self):
        return self.value

    def sleep(self, duration):
        self.sleeps.append(duration)
        self.value += duration


@pytest.fixture
def clock(monkeypatch):
    value = Clock()
    monkeypatch.setattr(polling.time, "monotonic", value.now)
    monkeypatch.setattr(polling.time, "sleep", value.sleep)
    return value


def test_backoff_caps_and_fresh_wait_resets(clock):
    backoff = polling.CompletionBackoff()
    for _ in range(6):
        backoff.wait(10)
    assert clock.sleeps == pytest.approx([.005, .010, .020, .050, .050, .050])
    polling.CompletionBackoff().wait(10)
    assert clock.sleeps[-1] == .005


def test_backoff_clips_and_never_sleeps_after_deadline(clock):
    backoff = polling.CompletionBackoff()
    backoff.wait(.003)
    backoff.wait(.003)
    assert clock.sleeps == [.003]


class StatusPeer:
    host_boot_id = 7

    def __init__(self, offsets, **change):
        self.offsets = iter(offsets)
        self.change = change
        self.calls = 0

    def request(self, group, command, **kwargs):
        assert group == 64 and command in (5, 6)
        assert kwargs.get("retry") is False and not kwargs.get("write")
        self.calls += 1
        return {"host_boot_id": 7, "job": 9, "offset": next(self.offsets),
                "total": 512, "state": "ready", "error": 0, **self.change}


def run_poll(module, peer, clock, minimum=0, maximum=256, seconds=1):
    predicate = lambda status: status["offset"] == maximum
    if module is stm:
        return stm.poll(peer, 9, clock.now() + seconds, 512, minimum, maximum, predicate)
    return rom.poll(peer, 9, 512, minimum, maximum, predicate, seconds=seconds)


@pytest.mark.parametrize("module", [stm, rom])
def test_offset_requires_ack_and_backoff_resets_for_next_chunk(clock, module):
    first = run_poll(module, StatusPeer([0, 0, 0, 256]), clock)
    assert first["offset"] == 256
    assert clock.sleeps == pytest.approx([.005, .010, .020])
    run_poll(module, StatusPeer([256, 512]), clock, minimum=256, maximum=512)
    assert clock.sleeps[-1] == .005


@pytest.mark.parametrize("module", [stm, rom])
def test_pending_wait_stops_at_existing_deadline(clock, module):
    peer = StatusPeer([0] * 10)
    with pytest.raises(SmpError, match="timed out|timeout"):
        run_poll(module, peer, clock, seconds=.012)
    assert clock.now() == pytest.approx(.012)
    assert clock.sleeps == pytest.approx([.005, .007])
    assert peer.calls == 2


@pytest.mark.parametrize("module", [stm, rom])
@pytest.mark.parametrize("change", [
    {"host_boot_id": 8}, {"job": 10}, {"offset": 1}, {"total": 513},
    {"state": "failed", "error": -116},
])
def test_guard_failure_is_immediate_and_never_replayed(clock, module, change):
    peer = StatusPeer([0], **change)
    with pytest.raises(SmpError):
        run_poll(module, peer, clock)
    assert peer.calls == 1 and clock.sleeps == []


def test_service_result_empty_pages_use_backoff(clock):
    class Peer:
        host_boot_id = 7
        calls = 0

        def request(self, group, command, data):
            self.calls += 1
            assert (group, command) == (64, 3)
            records = [] if self.calls < 4 else [{"id": 1, "operation": 9,
                "kind": "response", "status": 0, "data": b""}]
            return {"host_boot_id": 7, "events": records}

    assert mmctl.wait_result(Peer(), 9, 0, 1)["operation"] == 9
    assert clock.sleeps == pytest.approx([.005, .010, .020])


def test_demo_operation_uses_backoff_without_changing_event_guards(clock):
    from test_phase12_cli import Peer

    class DelayedPeer(Peer):
        polls = 0

        def request(self, group, command, data=None, **kwargs):
            if command == 3:
                self.polls += 1
                if self.polls < 4:
                    return {"host_boot_id": 9, "events": []}
            return super().request(group, command, data, **kwargs)

    peer = DelayedPeer(clock)
    result = demo.Observer(peer, mmctl, peer.session, 0).call(0x1001, 1)
    assert result["operation"] == 1
    assert clock.sleeps == pytest.approx([.005, .010, .020])
