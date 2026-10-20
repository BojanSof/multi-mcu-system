"""Control-plane tests use protocol peers, never physical serial devices."""

import argparse
import binascii
import pathlib
import struct
import sys

import cbor2
import hashlib
import pytest

sys.path.insert(0, str(pathlib.Path(__file__).parents[1]))
from cli import mmctl
from cli.smp import (HEADER, SerialDecoder, SmpClient, SmpError, SmpTimeout,
                     crc16, encode_serial, make_packet)


class FakeSerial:
    def __init__(self, responder):
        self.responder = responder
        self.writes = []
        self.pending = bytearray()
        self.decoder = SerialDecoder()

    def write(self, data):
        self.writes.append(data)
        for packet in self.decoder.feed(data):
            response = self.responder(packet, len(self.writes))
            if response:
                self.pending.extend(response)
        return len(data)

    def read(self, length):
        # Exercise partial serial reads and lines across calls.
        amount = min(length, 7)
        result = bytes(self.pending[:amount])
        del self.pending[:amount]
        return result


def reply(packet, data, **overrides):
    op, flags, length, group, sequence, command = HEADER.unpack(packet[:8])
    body = cbor2.dumps(data)
    response = HEADER.pack(overrides.get("op", op + 1), 0, len(body),
                           overrides.get("group", group), sequence, command) + body
    return encode_serial(response)


def test_golden_crc_header_and_frame():
    assert crc16(b"123456789") == 0x31C3
    packet = make_packet(0, 64, 0, 42, {})
    assert packet == bytes.fromhex("0800000100402a00a0")
    frame = encode_serial(packet)
    assert frame.startswith(b"\x06\x09") and frame.endswith(b"\n")
    assert SerialDecoder().feed(frame) == [packet]


@pytest.mark.parametrize("length", [0, 1, 80, 90, 200, 2000])
def test_serial_roundtrip_fragmentation_and_logs(length):
    packet = make_packet(2, 64, 2, 19, {"data": bytes(length)})
    framed = b"boot log\r\n" + encode_serial(packet) + b"other log\n"
    logs = []
    decoder = SerialDecoder(logs.append)
    received = []
    for offset in range(0, len(framed), 5):
        received.extend(decoder.feed(framed[offset:offset + 5]))
    assert received == [packet]
    assert logs == [b"boot log", b"other log"]
    assert all(len(line) + 1 <= 124 for line in encode_serial(packet).splitlines())


def test_serial_rejects_bad_crc_and_recovers():
    packet = make_packet(0, 64, 0, 1, {})
    import base64
    corrupt = b"\x06\x09" + base64.b64encode(
        struct.pack(">H", len(packet) + 2) + packet + b"\x00\x00") + b"\n"
    decoder = SerialDecoder()
    assert decoder.feed(b"\x04\x14AAAA\n" + corrupt + b"\x06\x09???\n") == []
    assert decoder.errors == 3
    assert decoder.feed(encode_serial(packet)) == [packet]


def test_serial_memory_bounds():
    decoder = SerialDecoder()
    decoder.feed(b"x" * 20000)
    assert len(decoder.line) == 0
    assert decoder.errors == 1
    with pytest.raises(SmpError):
        encode_serial(bytes(9000))


def test_client_ignores_unrelated_response_and_demultiplexes_logs():
    logs = []
    serial = FakeSerial(lambda request, attempt: b"logging\n" +
                        reply(request, {}, group=12) + reply(request, {"api": 1}))
    client = SmpClient(serial, timeout=0.1, log=logs.append)
    assert client.request(64, 0) == {"api": 1}
    assert logs == [b"logging"]


def test_retry_preserves_exact_request_and_request_id():
    seen = []
    def responder(packet, attempt):
        seen.append(packet)
        return None if attempt == 1 else reply(packet, {"operation": 7})
    serial = FakeSerial(responder)
    client = SmpClient(serial, timeout=0.001, retries=1)
    client.host_boot_id = 55
    assert mmctl.call(client, 0x1002, 1, struct.pack("<I", 2000), request_id=999) == {"operation": 7}
    assert seen[0] == seen[1]
    payload = cbor2.loads(seen[0][8:])
    assert payload["request_id"] == 999
    assert payload["expected_boot_id"] == 55


def test_timeout_is_bounded_and_reset_not_retried():
    serial = FakeSerial(lambda packet, attempt: None)
    client = SmpClient(serial, timeout=0.001, retries=2)
    with pytest.raises(SmpTimeout):
        client.request(0, 5, write=True, retry=False)
    assert len(serial.writes) == 1
    with pytest.raises(SmpTimeout):
        client.request(64, 0)
    assert len(serial.writes) == 4


@pytest.mark.parametrize("response", [{"error": "host_restarted"}, {"rc": 3},
                                     {"err": {"group": 64, "rc": 2}}, [1, 2]])
def test_client_device_errors(response):
    client = SmpClient(FakeSerial(lambda packet, attempt: reply(packet, response)), timeout=0.1)
    with pytest.raises(SmpError):
        client.request(64, 0)


def test_client_rejects_invalid_response_header():
    client = SmpClient(FakeSerial(lambda packet, attempt: reply(packet, {}, op=3)), timeout=0.1)
    with pytest.raises(SmpError, match="header"):
        client.request(64, 0)


def test_missing_host_group_explains_firmware_profile():
    client = SmpClient(FakeSerial(lambda packet, attempt: reply(packet, {"rc": 8})), timeout=0.1)
    with pytest.raises(SmpError, match="build and flash the phase9 profile"):
        client.request(64, 0)


def test_dangerous_commands_require_yes_without_opening_port(monkeypatch, capsys):
    monkeypatch.setattr(mmctl, "open_serial", lambda *args: pytest.fail("opened serial"))
    assert mmctl.main(["node", "maintenance", "enter", "--json"]) == 1
    assert "requires --yes" in capsys.readouterr().out
    assert mmctl.main(["stm", "recover", "--json"]) == 1
    assert "unavailable" in capsys.readouterr().out


def test_options_at_any_depth_and_argument_schemas():
    args = mmctl.parse_args(["actuator", "start", "--duration-ms", "2500", "--port", "mock", "--json"])
    assert args.port == "mock" and args.duration_ms == 2500 and args.json
    args = mmctl.parse_args(["diagnostics", "inject", "8", "--yes"])
    assert args.injection == 8 and args.yes and mmctl.requires_yes(args)


def test_port_selection_is_unambiguous():
    jlink = {"port": "/dev/mock", "vid": 0x1366, "description": "J-Link"}
    assert mmctl.select_port(None, [jlink]) == "/dev/mock"
    assert mmctl.select_port("explicit", []) == "explicit"
    for ports in ([], [jlink, jlink]):
        with pytest.raises(SmpError):
            mmctl.select_port(None, ports)


def test_signed_sensor_and_actuator_decode():
    event = {"service": 0x1001, "data": struct.pack("<iI", -434, 1)}
    assert mmctl.decode_event(event)["decoded"] == {"value_milli_units": -434, "sample_index": 1}
    event = {"service": 0x1002, "data": struct.pack("<BI", 25, 7)}
    assert mmctl.decode_event(event)["decoded"] == {"execution": 7, "progress_percent": 25}
    assert mmctl.json_value({"data": b"\x00\xff"}) == {"data": "00ff"}


def test_call_requires_host_boot_id():
    client = SmpClient(FakeSerial(lambda packet, attempt: None), timeout=0.001)
    with pytest.raises(SmpError, match="host_boot_id"):
        mmctl.call(client, 1, 1)


def signed_nordic_image(body=b"image", flags=0, load_address=0, sha512=False):
    header = struct.pack("<IIHHII", 0x96F3B83D, load_address, 32, 0, len(body), flags) + bytes(12)
    image = header + body
    digest = (hashlib.sha512 if sha512 else hashlib.sha256)(image).digest()
    return image + struct.pack("<HHHH", 0x6907, 8 + len(digest),
                               0x12 if sha512 else 0x10, len(digest)) + digest


def test_upload_chunk_offsets_and_first_chunk_metadata(tmp_path):
    path = tmp_path / "signed.bin"
    image = signed_nordic_image(bytes(range(256)) * 3)
    path.write_bytes(image)
    requests = []
    def responder(packet, attempt):
        request = cbor2.loads(packet[8:])
        requests.append(request)
        return reply(packet, {"off": request["off"] + len(request["data"])})
    client = SmpClient(FakeSerial(responder), timeout=0.1)
    mmctl.upload(client, path)
    assert [item["off"] for item in requests] == [0, 256, 512, 768]
    assert requests[0]["len"] == len(image) and len(requests[0]["sha"]) == 32
    assert "sha" not in requests[1]


def test_upload_rejects_nonprogressing_offset(tmp_path):
    path = tmp_path / "signed.bin"
    path.write_bytes(signed_nordic_image())
    client = SmpClient(FakeSerial(lambda packet, attempt: reply(packet, {"off": 0})), timeout=0.1)
    with pytest.raises(SmpError, match="offset"):
        mmctl.upload(client, path)


def test_no_dtr_pulse_and_flow_control(monkeypatch):
    class Port:
        def __init__(self, **kwargs):
            self.options = kwargs
        def open(self):
            assert self.dtr is False and self.rts is False
    monkeypatch.setattr(mmctl.serial, "Serial", Port)
    result = mmctl.open_serial("mock", 115200, 3)
    assert result.port == "mock" and result.options["rtscts"] is True
    assert result.options["port"] is None


def test_wait_result_rejects_host_restart_with_reused_operation():
    class Client:
        host_boot_id = 1
        def request(self, *args, **kwargs):
            return {"host_boot_id": 2, "events": [{"id": 1, "operation": 7,
                                                    "kind": "response"}]}
    with pytest.raises(SmpError, match="outcome unknown"):
        mmctl.wait_result(Client(), 7, 0, 0.1)


def test_wait_result_paginates_unrelated_events(capsys):
    class Client:
        host_boot_id = 1
        def __init__(self):
            self.cursors = []
        def request(self, group, command, data):
            self.cursors.append(data["after"])
            if data["after"] == 0:
                return {"host_boot_id": 1, "events": [
                    {"id": i, "operation": 8, "kind": "response"} for i in range(1, 5)]}
            return {"host_boot_id": 1, "events": [{"id": 5, "operation": 7,
                                                    "kind": "response", "status": 0}]}
    client = Client()
    assert mmctl.wait_result(client, 7, 0, 0.1)["id"] == 5
    assert client.cursors == [0, 4]


def test_mcuboot_preflight_rejects_wrong_and_corrupt_images():
    image = signed_nordic_image()
    assert len(mmctl.inspect_nordic_image(image)) == 32
    for bad in (b"PK\x03\x04" + bytes(100), image[:-1], image[:-1] + bytes([image[-1] ^ 1])):
        with pytest.raises(SmpError):
            mmctl.inspect_nordic_image(bad)


def test_mcuboot_nordic_fixed_rom_layout():
    image = signed_nordic_image(flags=0x100, load_address=0x11000)
    assert mmctl.inspect_nordic_image(image) == hashlib.sha256(image[:37]).digest()
    with pytest.raises(SmpError, match="different Nordic flash layout"):
        mmctl.inspect_nordic_image(signed_nordic_image(flags=0x100, load_address=0x21000))
    with pytest.raises(SmpError, match="unsupported"):
        mmctl.inspect_nordic_image(signed_nordic_image(flags=0x104, load_address=0x11000))


def test_mcuboot_ncs_fixed_rom_sha512():
    image = signed_nordic_image(flags=0x100, load_address=0x11000, sha512=True)
    digest = hashlib.sha512(image[:37]).digest()
    assert mmctl.inspect_nordic_image(image) == digest
    assert mmctl.image_hash(digest.hex()) == digest
    with pytest.raises(SmpError, match="hash mismatch"):
        mmctl.inspect_nordic_image(image[:-1] + bytes([image[-1] ^ 1]))


def test_update_refuses_same_primary_without_upload(tmp_path):
    path = tmp_path / "signed.bin"
    image = signed_nordic_image()
    path.write_bytes(image)
    class Client:
        def request(self, group, command, *args, **kwargs):
            assert not kwargs.get("write")
            return {"images": [{"slot": 0, "hash": mmctl.inspect_nordic_image(image)}]}
    args = argparse.Namespace(image_action="update", firmware=path, json=True)
    with pytest.raises(SmpError, match="already primary"):
        mmctl.host_command(Client(), args)


def test_update_refuses_wrong_secondary_hash(tmp_path):
    path = tmp_path / "signed.bin"
    image = signed_nordic_image()
    path.write_bytes(image)
    class Client:
        def request(self, group, command, data=None, **kwargs):
            if command == 1:
                return {"off": len(image)}
            assert not kwargs.get("write"), "must not mark pending or reset"
            return {"images": [{"slot": 0, "hash": bytes(32)},
                                {"slot": 1, "hash": bytes(32)}]}
    args = argparse.Namespace(image_action="update", firmware=path, json=True)
    with pytest.raises(SmpError, match="does not match"):
        mmctl.host_command(Client(), args)
