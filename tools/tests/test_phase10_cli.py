"""Phase 10 uses signed synthetic images and fake hosts, never physical boards."""
import argparse
import pathlib
import sys

import pytest
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec

sys.path.insert(0, str(pathlib.Path(__file__).parents[1]))
from cli import mmctl, stm
from cli.smp import SmpError, SmpTimeout
from firmware.mmfw import build_signed_image


@pytest.fixture
def signed(tmp_path):
    key = ec.generate_private_key(ec.SECP256R1())
    public = tmp_path / "public.pem"
    public.write_bytes(key.public_key().public_bytes(
        serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo))
    image = tmp_path / "slot.mmfw"
    image.write_bytes(build_signed_image(
        b"firmware" * 100, key, slot_id=1, target_id=2, hardware_id=0x00830001,
        version=(0, 10, 0), build_id=3, load_address=0x08026200,
        required_wire=(1, 0), required_management=(1, 0)))
    return image, public


@pytest.mark.parametrize("damage", ["payload", "signature", "key", "missing"])
def test_preflight_before_serial(monkeypatch, signed, tmp_path, damage):
    image, public = signed
    if damage in ("payload", "signature"):
        content = bytearray(image.read_bytes())
        content[-1 if damage == "payload" else 90] ^= 1
        image.write_bytes(content)
    elif damage == "key":
        public.write_bytes(b"not a key")
    else:
        public = tmp_path / "absent.pem"
    monkeypatch.setattr(mmctl, "devices", lambda: pytest.fail("device discovery before preflight"))
    monkeypatch.setattr(mmctl, "open_serial", lambda *a: pytest.fail("opened serial"))
    assert mmctl.main(["stm", "update", str(image), "--public-key", str(public), "--yes"]) == 1


def test_inspect_offline_and_key_id(monkeypatch, signed):
    image, public = signed
    monkeypatch.setattr(mmctl, "devices", lambda: pytest.fail("device discovery"))
    assert mmctl.main(["stm", "image", "inspect", str(image), "--public-key", str(public)]) == 0
    assert mmctl.main(["stm", "image", "inspect", str(image), "--public-key", str(public),
                       "--key-id", "2"]) == 1


class Host:
    host_boot_id = 7

    def __init__(self, mode="complete"):
        self.mode = mode
        self.job = 0
        self.offset = 0
        self.total = 0
        self.state = "idle"
        self.writes = []
        self.dropped = False

    def request(self, group, command, data=None, write=False, **kwargs):
        if not write:
            return {"host_boot_id": 8 if self.mode == "reboot" else 7,
                    "job": self.job + (1 if self.mode == "wrongjob" else 0),
                    "offset": self.offset + (1 if self.mode == "gap" else 0),
                    "total": self.total, "state": self.state, "error": 0}
        self.writes.append(data.copy())
        action = data["action"]
        if action == "begin":
            self.job, self.total = data["request_id"], data["total"]
            self.state = "ready"
        elif action == "chunk":
            assert data["offset"] <= self.offset
            if self.mode != "stall":
                self.offset = data["offset"] + len(data["data"])
            if self.mode == "drop" and not self.dropped:
                self.dropped = True
                raise SmpTimeout("lost admission reply")
        elif action == "finish":
            self.state = "rolled_back" if self.mode == "rollback" else "complete"
        elif action == "abort":
            self.state = "aborted"
        return {"request_id": data["request_id"], "job": self.job}


def update_args(signed):
    return argparse.Namespace(stm_preflight=stm.preflight(*signed), wait_timeout=0.01, json=True)


def test_whole_image_ack_and_duplicate_retry(signed):
    client = Host("drop")
    stm.update(client, update_args(signed), lambda *a: None)
    chunks = [data for data in client.writes if data["action"] == "chunk"]
    assert chunks[0] == chunks[1]
    assert b"".join(data["data"] for data in chunks[1:]) == signed[0].read_bytes()
    assert all(0 < len(data["data"]) <= 256 for data in chunks)


@pytest.mark.parametrize("mode", ["gap", "reboot", "wrongjob", "rollback"])
def test_invalid_progress_does_not_complete(signed, mode):
    client = Host(mode)
    with pytest.raises(SmpError):
        stm.update(client, update_args(signed), lambda *a: None)
    if mode != "rollback":
        assert not any(data["action"] == "finish" for data in client.writes)


def test_ack_stall_bounded(monkeypatch, signed):
    ticks = iter(range(0, 1000, 5))
    monkeypatch.setattr(stm.time, "monotonic", lambda: next(ticks))
    monkeypatch.setattr(stm.time, "sleep", lambda _: None)
    client = Host("stall")
    with pytest.raises(stm.Indeterminate, match="timed out"):
        stm.update(client, update_args(signed), lambda *a: None)
    assert not any(data["action"] == "finish" for data in client.writes)


def test_ctrl_c_before_commit_attempts_abort(signed):
    client = Host()
    def interrupt(*args):
        raise KeyboardInterrupt
    with pytest.raises(KeyboardInterrupt):
        stm.update(client, update_args(signed), interrupt)
    assert client.writes[-1]["action"] == "abort"


def test_no_replay_on_boot_change_after_timeout():
    client = Host("reboot")
    def failed(*args, **kwargs):
        if kwargs.get("write"):
            raise SmpTimeout("lost")
        return {"host_boot_id": 8, "job": 1}
    client.request = failed
    with pytest.raises(stm.Indeterminate, match="boot/job changed"):
        stm.mutate(client, 5, "begin", header=bytes(512), total=1024)


def test_wrong_trusted_key_rejected(signed, tmp_path):
    key = ec.generate_private_key(ec.SECP256R1())
    other = tmp_path / "other.pem"
    other.write_bytes(key.public_key().public_bytes(
        serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo))
    with pytest.raises(ValueError, match="signature mismatch"):
        stm.preflight(signed[0], other)


def test_numeric_error_is_status_not_smp_domain_error():
    from test_phase9_cli import FakeSerial, reply
    from cli.smp import SmpClient
    connection = FakeSerial(lambda packet, _: reply(packet, {"error": 0, "state": "ready"}))
    assert SmpClient(connection).request(64, 5)["error"] == 0


def test_transport_timeout_reports_uncertain(signed):
    client = Host()
    original = client.request
    def timeout(group, command, data=None, **kwargs):
        if data and data.get("action") == "chunk":
            raise SmpTimeout("no reply")
        if client.writes:
            raise SmpTimeout("no status")
        return original(group, command, data, **kwargs)
    client.request = timeout
    with pytest.raises(stm.Indeterminate, match="outcome uncertain"):
        stm.update(client, update_args(signed), lambda *a: None)
