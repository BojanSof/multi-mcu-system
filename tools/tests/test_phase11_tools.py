"""Phase 11 release/CLI regression uses synthetic files and fake SMP peers only."""

import argparse
import copy
import hashlib
import json
import pathlib
import struct
import sys

import pytest
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec

sys.path.insert(0, str(pathlib.Path(__file__).parents[1]))
from cli import mmctl, rom, stm
from cli.smp import SmpError, SmpTimeout
from firmware import release_bundle
from firmware.mmfw import PackageError, build_signed_image


def nordic_image(signed=True):
    header = struct.pack("<IIHHII", 0x96F3B83D, 0x11000, 32, 0, 5, 0x100) + bytes(12)
    body = header + b"image"
    tlvs = struct.pack("<HH", 0x10, 32) + hashlib.sha256(body).digest()
    if signed:
        tlvs += struct.pack("<HH", 0x22, 64) + b"s" * 64
    return body + struct.pack("<HH", 0x6907, 4 + len(tlvs)) + tlvs


@pytest.fixture
def bundle(tmp_path):
    key = ec.generate_private_key(ec.SECP256R1())
    public = tmp_path / "public.pem"
    public.write_bytes(key.public_key().public_bytes(
        serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo))
    nordic = tmp_path / "nordic.bin"
    nordic.write_bytes(nordic_image())
    loader = tmp_path / "loader.bin"
    loader.write_bytes(struct.pack("<II", 0x2000A000, 0x08000009) + b"L" * 261)
    slots = []
    for slot in range(2):
        path = tmp_path / f"slot{slot}.mmfw"
        path.write_bytes(build_signed_image(
            bytes([slot]) * 1024, key, slot_id=slot, target_id=2, hardware_id=0x00830001,
            version=(0, 11, 0), build_id=0xb00,
            load_address=0x08010200 if slot == 0 else 0x08026200,
            required_wire=(1, 0), required_management=(1, 0)))
        slots.append(path)
    manifest = tmp_path / "release.json"
    release_bundle.create(manifest, nordic, loader, *slots, "0.11.0", public)
    return manifest, public


def write_document(path, document):
    path.write_text(json.dumps(document), encoding="utf-8")


def test_bundle_create_verify_deterministic(bundle, capsys):
    manifest, public = bundle
    parsed = release_bundle.load(manifest, public)
    assert len(parsed.bootloader) == 269
    assert parsed.slot_manifests[0].slot_id == 0
    assert parsed.slot_manifests[1].slot_id == 1
    assert release_bundle.main(["verify", str(manifest), "--public-key", str(public)]) == 0
    assert '"verified": true' in capsys.readouterr().out
    before = manifest.read_bytes()
    release_bundle.create(manifest, manifest.parent / "nordic.bin", manifest.parent / "loader.bin",
                          manifest.parent / "slot0.mmfw", manifest.parent / "slot1.mmfw",
                          "0.11.0", public)
    assert manifest.read_bytes() == before


@pytest.mark.parametrize("damage", ["unknown", "missing", "board", "format", "version",
                                    "slot_order", "key_id", "address", "size", "sha",
                                    "absolute", "traversal", "backslash", "duplicate_path",
                                    "bool_api", "missing_artifact"])
def test_bundle_rejects_invalid_schema_and_paths(bundle, damage):
    manifest, public = bundle
    document = json.loads(manifest.read_text())
    if damage == "unknown":
        document["extra"] = 1
    elif damage == "missing":
        del document["wire"]
    elif damage == "board":
        document["board"] = "wrong"
    elif damage == "format":
        document["format_version"] = 1
    elif damage == "version":
        document["system_version"] = "0.12.0"
    elif damage == "slot_order":
        document["slots"].reverse()
    elif damage == "key_id":
        document["bootloader"]["key_id"] = 2
    elif damage == "address":
        document["bootloader"]["address"] += 0x10000
    elif damage == "size":
        document["bootloader"]["size"] = 65537
    elif damage == "sha":
        document["bootloader"]["sha256"] = "0" * 64
    elif damage == "absolute":
        document["bootloader"]["file"] = "/tmp/loader.bin"
    elif damage == "traversal":
        document["bootloader"]["file"] = "../loader.bin"
    elif damage == "backslash":
        document["bootloader"]["file"] = "foo\\loader.bin"
    elif damage == "duplicate_path":
        document["slots"][1]["file"] = document["slots"][0]["file"]
    elif damage == "bool_api":
        document["api"] = True
    else:
        document["bootloader"]["file"] = "absent.bin"
    write_document(manifest, document)
    with pytest.raises((PackageError, OSError)):
        release_bundle.load(manifest, public)


def test_bundle_rejects_duplicate_keys(bundle):
    manifest, public = bundle
    manifest.write_text(manifest.read_text().replace('"api": 1', '"api": 1, "api": 1'))
    with pytest.raises(PackageError, match="duplicate"):
        release_bundle.load(manifest, public)


def test_bundle_rejects_symlink_escape(bundle, tmp_path):
    manifest, public = bundle
    outside = tmp_path.parent / (tmp_path.name + "-outside.bin")
    outside.write_bytes((tmp_path / "loader.bin").read_bytes())
    (tmp_path / "loader.bin").unlink()
    (tmp_path / "loader.bin").symlink_to(outside)
    with pytest.raises(PackageError, match="symlink"):
        release_bundle.load(manifest, public)


@pytest.mark.parametrize("artifact,content", [
    ("bootloader", b"\x7fELF" + bytes(265)),
    ("bootloader", b":" + bytes(268)),
    ("bootloader", struct.pack("<II", 0x2000A008, 0x08000009) + bytes(261)),
    ("bootloader", struct.pack("<II", 0x2000A000, 0x08010001) + bytes(261)),
    ("nordic", nordic_image(False)),
    ("nordic", b"not MCUboot"),
])
def test_bundle_rejects_invalid_artifacts_even_with_matching_manifest_hash(bundle, artifact, content):
    manifest, public = bundle
    document = json.loads(manifest.read_text())
    record = document[artifact]
    (manifest.parent / record["file"]).write_bytes(content)
    record["sha256"] = hashlib.sha256(content).hexdigest()
    if artifact == "bootloader":
        record["size"] = len(content)
    write_document(manifest, document)
    with pytest.raises(PackageError):
        release_bundle.load(manifest, public)


def test_wrong_public_key_and_signed_payload(bundle):
    manifest, public = bundle
    other = ec.generate_private_key(ec.SECP256R1()).public_key()
    public.write_bytes(other.public_bytes(serialization.Encoding.PEM,
                                        serialization.PublicFormat.SubjectPublicKeyInfo))
    with pytest.raises(PackageError, match="signature"):
        release_bundle.load(manifest, public)


@pytest.mark.parametrize("damage", ["hash", "key", "flags", "yes"])
def test_recovery_preflight_before_device_discovery(bundle, monkeypatch, damage):
    manifest, public = bundle
    argv = ["stm", "recover", str(manifest), "--public-key", str(public),
            "--rom-prerequisites-verified", "--yes"]
    if damage == "hash":
        (manifest.parent / "loader.bin").write_bytes(b"bad")
    elif damage == "key":
        public.write_bytes(b"invalid")
    elif damage == "flags":
        argv.remove("--rom-prerequisites-verified")
    else:
        argv.remove("--yes")
    monkeypatch.setattr(mmctl, "devices", lambda: pytest.fail("discovery before offline preflight"))
    monkeypatch.setattr(mmctl, "open_serial", lambda *args: pytest.fail("opened serial"))
    assert mmctl.main(argv) == 1


class RomHost:
    host_boot_id = 7

    def __init__(self, mode="complete", online=True):
        self.mode, self.online = mode, online
        self.job, self.offset, self.total = 0, 0, 0
        self.state = "idle"
        self.writes = []
        self.dropped = False

    def request(self, group, command, data=None, write=False, **kwargs):
        assert group == 64
        if not write:
            if command == 0:
                if self.mode == "runtime_loss":
                    raise SmpTimeout("lost host after repair")
                return {"api": 1, "host_boot_id": 7,
                        "session": 7 if self.state == "idle" else 8,
                        "state": "online" if self.online else "discovering"}
            assert command == 6
            return {"host_boot_id": 8 if self.mode == "reboot" else 7,
                    "job": self.job + (1 if self.mode == "wrongjob" else 0),
                    "offset": self.offset + (1 if self.mode == "gap" else 0),
                    "total": self.total, "state": self.state, "error": 0,
                    "active": self.state not in ("complete", "failed", "aborted")}
        assert command == 6
        self.writes.append(copy.deepcopy(data))
        if data["action"] == "begin":
            assert data["prerequisites"] is True and len(data["sha256"]) == 32
            self.job, self.total = data["request_id"], data["total"]
            self.state = "ready"
        elif data["action"] == "chunk":
            assert data["offset"] % 8 == 0
            self.offset = data["offset"] + len(data["data"])
            if self.mode == "drop" and not self.dropped:
                self.dropped = True
                raise SmpTimeout("lost admission")
            if self.mode == "interrupt":
                raise KeyboardInterrupt
        elif data["action"] == "finish":
            self.state = "complete"
        elif data["action"] == "abort":
            self.state = "aborted"
        return {"request_id": data["request_id"], "job": self.job}


def recovery_args(bundle):
    return argparse.Namespace(rom_preflight=release_bundle.load(*bundle), json=True, wait_timeout=10)


def fast_clock(monkeypatch):
    counter = iter(range(1000000))
    monkeypatch.setattr(rom.time, "monotonic", lambda: next(counter))
    monkeypatch.setattr(rom.time, "sleep", lambda _: None)


def test_rom_stream_readback_and_exact_admission_retry(bundle):
    host = RomHost("drop")
    args = recovery_args(bundle)
    output = []
    rom.recover(host, args, lambda value, *a: output.append(value))
    chunks = [value for value in host.writes if value["action"] == "chunk"]
    assert chunks[0] == chunks[1]
    assert b"".join(value["data"] for value in chunks[1:]) == args.rom_preflight.bootloader
    assert [len(value["data"]) for value in chunks[1:]] == [256, 13]
    assert output[-1] == {"state": "runtime_online", "preserved_applications": True}


@pytest.mark.parametrize("mode", ["reboot", "wrongjob", "gap", "runtime_loss"])
def test_rom_identity_or_transport_error_never_chains_signed_update(bundle, monkeypatch, mode):
    monkeypatch.setattr(stm, "update", lambda *a: pytest.fail("unexpected signed mutation"))
    with pytest.raises(SmpError):
        rom.recover(RomHost(mode), recovery_args(bundle), lambda *a: None)


def test_ctrl_c_attempts_abort_before_finish(bundle):
    host = RomHost("interrupt")
    with pytest.raises(KeyboardInterrupt):
        rom.recover(host, recovery_args(bundle), lambda *a: None)
    assert host.writes[-1]["action"] == "abort"
    assert not any(value["action"] == "finish" for value in host.writes)


def test_offline_runtime_chains_only_signed_slot0(bundle, monkeypatch):
    fast_clock(monkeypatch)
    installed = []
    monkeypatch.setattr(stm, "update", lambda client, args, emit: installed.append(args.stm_preflight))
    args = recovery_args(bundle)
    rom.recover(RomHost(online=False), args, lambda *a: None)
    assert installed == [(args.rom_preflight.slot_images[0], args.rom_preflight.slot_manifests[0])]


@pytest.mark.parametrize("protected", [True, False])
def test_protected_slot0_allows_only_verified_inactive_slot1_fallback(bundle, monkeypatch, protected):
    fast_clock(monkeypatch)
    host = RomHost(online=False)
    args = recovery_args(bundle)
    installs = []
    failed = {"host_boot_id": 7, "job": 88, "slot": 0, "offset": 0,
              "total": len(args.rom_preflight.slot_images[0]), "error": -18 if protected else -116,
              "protocol": 6, "state": "failed", "active": False}
    def install(client, candidate, emit):
        installs.append(candidate.stm_preflight)
        if len(installs) == 1:
            raise stm.JobFailure(failed)
    monkeypatch.setattr(stm, "update", install)
    monkeypatch.setattr(stm, "status", lambda client, job=None: dict(failed))
    if protected:
        rom.recover(host, args, lambda *a: None)
        assert installs[1][0] == args.rom_preflight.slot_images[1]
    else:
        with pytest.raises(stm.JobFailure):
            rom.recover(host, args, lambda *a: None)
        assert len(installs) == 1


def test_missing_prerequisites_and_mutation_authority():
    args = mmctl.parse_args(["stm", "recover-abort"])
    assert mmctl.requires_yes(args)
    assert not mmctl.requires_yes(mmctl.parse_args(["stm", "recover-status"]))


def test_protected_fallback_stops_if_same_job_cannot_be_proven(bundle, monkeypatch):
    fast_clock(monkeypatch)
    host = RomHost(online=False)
    args = recovery_args(bundle)
    failed = {"host_boot_id": 7, "job": 88, "slot": 0, "offset": 0,
              "total": len(args.rom_preflight.slot_images[0]), "error": -18,
              "protocol": 6, "state": "failed", "active": False}
    installs = []
    def install(client, candidate, emit):
        installs.append(candidate.stm_preflight)
        raise stm.JobFailure(failed)
    monkeypatch.setattr(stm, "update", install)
    original = host.request
    def request(group, command, *args, **kwargs):
        if command == 5:
            return {**failed, "host_boot_id": 8}
        return original(group, command, *args, **kwargs)
    host.request = request
    with pytest.raises(stm.Indeterminate, match="boot/job changed"):
        rom.recover(host, args, lambda *a: None)
    assert len(installs) == 1


def test_main_recovery_capability_and_parser_handoff(bundle, monkeypatch):
    manifest, public = bundle
    host = RomHost()
    original = host.request
    def request(group, command, *args, **kwargs):
        result = original(group, command, *args, **kwargs)
        if command == 0:
            result.update({"stm_update": True, "supervision": True, "rom_recovery": True})
        return result
    host.request = request
    class Connection:
        def __enter__(self):
            return self
        def __exit__(self, *args):
            return False
    monkeypatch.setattr(mmctl, "devices", lambda: [])
    monkeypatch.setattr(mmctl, "open_serial", lambda *args: Connection())
    monkeypatch.setattr(mmctl, "SmpClient", lambda *args: host)
    assert mmctl.main(["stm", "recover", str(manifest), "--public-key", str(public),
                       "--rom-prerequisites-verified", "--yes", "--port", "mock"]) == 0
    assert host.writes[0]["action"] == "begin"
    assert host.writes[-1]["action"] == "finish"


def test_stale_online_snapshot_requires_new_runtime_session(bundle, monkeypatch):
    host = RomHost()
    original = host.request
    online_reads = []
    def request(group, command, *args, **kwargs):
        value = original(group, command, *args, **kwargs)
        if command == 0:
            online_reads.append(host.state)
            if len(online_reads) <= 2:
                value["session"] = 7
        return value
    host.request = request
    monkeypatch.setattr(rom.time, "sleep", lambda _: None)
    output = []
    rom.recover(host, recovery_args(bundle), lambda value, *a: output.append(value))
    assert online_reads == ["idle", "complete", "complete"]
    assert output[-1]["state"] == "runtime_online"


def test_permanently_stale_online_never_triggers_signed_recovery(bundle, monkeypatch):
    fast_clock(monkeypatch)
    host = RomHost()
    original = host.request
    def request(group, command, *args, **kwargs):
        value = original(group, command, *args, **kwargs)
        if command == 0:
            value["session"] = 7
        return value
    host.request = request
    monkeypatch.setattr(stm, "update", lambda *a: pytest.fail("signed mutation on stale snapshot"))
    with pytest.raises(stm.Indeterminate, match="session did not change"):
        rom.recover(host, recovery_args(bundle), lambda *a: None)
