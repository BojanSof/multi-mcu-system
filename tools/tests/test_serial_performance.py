"""Deterministic short-read timing models; no physical serial devices."""

import base64
import pathlib
import struct
import sys

import pytest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from cli import smp
from test_phase9_cli import FakeSerial, reply


class Clock:
    value = 0.0

    def now(self):
        return self.value


class BufferedSerial(FakeSerial):
    def __init__(self, responder, clock):
        super().__init__(responder)
        self.clock = clock
        self.sizes = []

    @property
    def in_waiting(self):
        return len(self.pending)

    def read(self, length):
        self.sizes.append(length)
        if len(self.pending) < length:
            self.clock.value += 0.1
        result = bytes(self.pending[:length])
        del self.pending[:length]
        return result


def test_short_reply_does_not_wait_for_unfilled_read_buffer(monkeypatch):
    clock = Clock()
    peer = BufferedSerial(lambda packet, _: reply(packet, {"ok": True}), clock)
    monkeypatch.setattr(smp.time, "monotonic", clock.now)
    assert smp.SmpClient(peer).request(64, 0) == {"ok": True}
    assert clock.value == 0  # Original read(256) consumed one 100 ms timeout.
    assert len(peer.sizes) == 1 and peer.sizes[0] < 256


def test_old_fixed_read_model_has_tail_timeout():
    clock = Clock()
    peer = BufferedSerial(lambda *_: None, clock)
    peer.pending.extend(b"short reply")
    assert peer.read(256) == b"short reply"
    assert clock.value == 0.1


def test_idle_read_blocks_for_one_byte_and_then_drains_available():
    class Arrival:
        pending = bytearray()
        sizes = []

        @property
        def in_waiting(self):
            return len(self.pending)

        def read(self, length):
            self.sizes.append(length)
            if not self.pending:
                self.pending.extend(b"arriving bytes")
            result = bytes(self.pending[:length])
            del self.pending[:length]
            return result

    peer = Arrival()
    assert smp._read_available(peer) == b"a"
    assert smp._read_available(peer) == b"rriving bytes"
    assert peer.sizes == [1, 13]


@pytest.mark.parametrize("payload_size", [0, 1, 90, 256, 1000, 3000])
def test_available_multiline_reply_is_bounded_and_complete(payload_size):
    peer = BufferedSerial(lambda packet, _: reply(packet, {"data": bytes(payload_size)}), Clock())
    assert smp.SmpClient(peer).request(64, 0) == {"data": bytes(payload_size)}
    assert all(1 <= count <= 256 for count in peer.sizes)


def test_legacy_fragmented_transport_without_availability():
    class Legacy(FakeSerial):
        sizes = []

        def read(self, length):
            self.sizes.append(length)
            return super().read(length)

    peer = Legacy(lambda packet, _: reply(packet, {"data": bytes(300)}))
    assert smp.SmpClient(peer).request(64, 0) == {"data": bytes(300)}
    assert set(peer.sizes) == {1}


def test_delayed_fragments_wait_only_for_first_byte(monkeypatch):
    class DelayedFragments(BufferedSerial):
        fragments = None

        def write(self, data):
            self.writes.append(data)
            for packet in self.decoder.feed(data):
                response = reply(packet, {"data": bytes(240)})
                self.fragments = [response[:3], response[3:17], response[17:]]
            return len(data)

        def read(self, length):
            if not self.pending and self.fragments:
                assert length == 1
                self.clock.value += .02  # New bytes arrive during blocking read.
                self.pending.extend(self.fragments.pop(0))
            return super().read(length)

    clock = Clock()
    peer = DelayedFragments(None, clock)
    monkeypatch.setattr(smp.time, "monotonic", clock.now)
    assert smp.SmpClient(peer, timeout=.2).request(64, 0) == {"data": bytes(240)}
    assert clock.value == pytest.approx(.06)
    assert all(1 <= count <= 256 for count in peer.sizes)


def test_stale_available_count_and_empty_read_recover():
    class Racing(BufferedSerial):
        first = True

        def read(self, length):
            if self.first:
                self.first = False
                self.sizes.append(length)
                return b""
            return super().read(length)

    peer = Racing(lambda packet, _: reply(packet, {"ok": True}), Clock())
    assert smp.SmpClient(peer).request(64, 0) == {"ok": True}
    assert len(peer.sizes) == 2


def test_availability_error_is_not_masked():
    class Broken:
        @property
        def in_waiting(self):
            raise OSError("disconnected")

        def read(self, length):
            pytest.fail("must not read disconnected port")

    with pytest.raises(OSError, match="disconnected"):
        smp._read_available(Broken())


@pytest.mark.parametrize("count", [-1, 1.5, None, True])
def test_invalid_availability_cannot_request_unbounded_read(count):
    class Broken:
        in_waiting = count

        def read(self, length):
            pytest.fail("invalid availability")

    with pytest.raises(smp.SmpError, match="availability"):
        smp._read_available(Broken())


def test_decoder_checks_crc_and_ignores_unrelated_packets():
    def respond(packet, _):
        corrupt = b"\x06\x09" + base64.b64encode(
            struct.pack(">H", len(packet) + 2) + packet + b"\x00\x00") + b"\n"
        return corrupt + reply(packet, {}, group=1) + reply(packet, {"ok": True})

    peer = BufferedSerial(respond, Clock())
    client = smp.SmpClient(peer)
    assert client.request(64, 0) == {"ok": True}
    assert client.decoder.errors == 1


def test_empty_reads_preserve_timeout_and_identical_retry(monkeypatch):
    clock = Clock()
    peer = BufferedSerial(lambda *_: None, clock)
    monkeypatch.setattr(smp.time, "monotonic", clock.now)
    with pytest.raises(smp.SmpTimeout):
        smp.SmpClient(peer, timeout=.2, retries=1).request(64, 2, {"request_id": 19}, write=True)
    assert peer.writes[0] == peer.writes[1]
    assert clock.value == .4
    assert peer.sizes == [1, 1, 1, 1]


def test_retry_false_never_adds_mutation_attempt(monkeypatch):
    clock = Clock()
    peer = BufferedSerial(lambda *_: None, clock)
    monkeypatch.setattr(smp.time, "monotonic", clock.now)
    with pytest.raises(smp.SmpTimeout):
        smp.SmpClient(peer, timeout=.2, retries=5).request(64, 5, write=True, retry=False)
    assert len(peer.writes) == 1
