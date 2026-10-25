#!/usr/bin/env python3
"""Operator CLI for the Phase 9 Nordic host control API."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import secrets
import struct
import sys
import time

import serial
from serial.tools import list_ports

try:
    from .smp import SmpClient, SmpError, SmpTimeout
    from . import stm, rom, demo
except ImportError:
    from smp import SmpClient, SmpError, SmpTimeout
    import stm
    import rom
    import demo


def number(value: str) -> int:
    return int(value, 0)


def positive(value: str) -> float:
    result = float(value)
    if not 0 < result <= 3600:
        raise argparse.ArgumentTypeError("must be between 0 and 3600 seconds")
    return result


def json_value(value):
    if isinstance(value, bytes):
        return value.hex()
    if isinstance(value, dict):
        return {str(key): json_value(item) for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [json_value(item) for item in value]
    return value


def emit(value, as_json: bool = False):
    # One JSON object per line in machine mode, including watch output.
    print(json.dumps(json_value(value), sort_keys=True, indent=None if as_json else 2), flush=True)


def devices() -> list[dict]:
    return [{"port": port.device, "description": port.description,
             "serial": port.serial_number, "vid": port.vid, "pid": port.pid,
             "interface": port.interface}
            for port in list_ports.comports()]


def select_port(explicit: str | None, available: list[dict]) -> str:
    if explicit:
        return explicit
    candidates = [port["port"] for port in available
                  if port["vid"] == 0x1366 or "j-link" in port["description"].lower()]
    if len(candidates) != 1:
        raise SmpError("specify --port (prefer /dev/serial/by-id/...); use 'device list'")
    return candidates[0]


def port_identity(port, available):
    """Pin USB identity before reset; never substitute an unrelated singleton."""
    resolved = str(pathlib.Path(port).resolve())
    matches = [item for item in available
               if str(pathlib.Path(item["port"]).resolve()) == resolved]
    if len(matches) == 1 and matches[0].get("serial"):
        return {key: matches[0].get(key) for key in ("serial", "vid", "pid", "interface")}
    return {"fixed_port": port}


def reconnect_port(identity, available):
    if "fixed_port" in identity:
        return identity["fixed_port"]
    matches = [item["port"] for item in available
               if all(item.get(key) == value for key, value in identity.items())]
    if len(matches) != 1:
        raise SmpError("original USB identity absent or ambiguous; refusing another device")
    return matches[0]


def open_serial(port: str, baud: int, timeout: float):
    # Set states before opening to avoid pyserial's default DTR assertion.
    connection = serial.Serial(port=None, baudrate=baud, timeout=min(timeout, 0.1),
                               write_timeout=timeout, rtscts=True, dsrdtr=False,
                               exclusive=True)
    connection.dtr = False
    connection.rts = False
    connection.port = port
    connection.open()
    return connection


def call(client, service: int, command: int, data: bytes = b"", request_id=None):
    if not 0 <= service <= 65535 or not 0 <= command <= 65535 or len(data) > 64:
        raise SmpError("service/command must fit u16; payload maximum is 64 bytes")
    request_id = request_id or secrets.randbelow((1 << 64) - 1) + 1
    boot_id = getattr(client, "host_boot_id", None)
    if not isinstance(boot_id, int) or boot_id == 0:
        raise SmpError("host_boot_id missing; query status before issuing a service call")
    return client.request(64, 2, {"request_id": request_id, "service": service,
                                  "command": command, "data": data,
                                  "expected_boot_id": boot_id}, write=True)


def decode_event(event: dict) -> dict:
    result = dict(event)
    data = event.get("data", b"")
    service = event.get("service")
    code = event.get("code")
    if event.get("status", 0) == 0 and isinstance(data, bytes):
        if service == 0x1001 and len(data) == 8:
            value, index = struct.unpack("<iI", data)
            result["decoded"] = {"value_milli_units": value, "sample_index": index}
        elif service == 0x1002 and len(data) == 4:
            result["decoded"] = {"execution": struct.unpack("<I", data)[0]}
        elif service == 0x1002 and len(data) == 5:
            result["decoded"] = {"execution": struct.unpack("<I", data[1:])[0],
                                   "progress_percent": data[0]}
        elif service == 2 and code in (1, 2, 3) and len(data) == 13:
            injections, records, overflows, queued = struct.unpack("<IIIB", data)
            result["decoded"] = {"injections": injections, "records": records,
                                   "overflows": overflows, "queued": queued}
        elif service == 2 and code == 4 and len(data) in (1, 18):
            result["decoded"] = {"present": bool(data[0])}
            if len(data) == 18:
                timestamp, correlation, detail, fault, source, severity, domain = struct.unpack(
                    "<IIIHBBB", data[1:])
                result["decoded"]["record"] = {
                    "time_ms": timestamp, "correlation": correlation, "detail": detail,
                    "code": fault, "source": source, "severity": severity, "domain": domain}
        elif service == 3 and code == 4 and len(data) == 16:
            major, minor, patch = struct.unpack("<HHH", data[:6])
            result["decoded"] = {"firmware": f"{major}.{minor}.{patch}",
                                   "wire": list(data[6:8]),
                                   "management": list(struct.unpack("<HH", data[8:12])),
                                   "build": struct.unpack("<I", data[12:])[0]}
        elif service == 3 and code in (1, 2, 3, 5, 6) and len(data) == 12:
            state, reason, raw_flags, boot_count, progress_count = struct.unpack("<BBHII", data)
            result["decoded"] = {"state": state, "reset_reason": reason,
                                   "raw_reset_flags": raw_flags, "boot_count": boot_count,
                                   "progress_count": progress_count}
    return result


def events(client, after: int = 0) -> dict:
    result = client.request(64, 3, {"after": after})
    result["events"] = [decode_event(event) for event in result.get("events", [])]
    return result


def wait_result(client, operation: int, cursor: int, seconds: float, as_json=False):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        batch = events(client, cursor)
        if batch.get("host_boot_id") != client.host_boot_id:
            raise SmpError("host restarted; pending operation outcome unknown; no command replayed")
        for event in batch.get("events", []):
            cursor = event["id"]
            if event.get("operation") == operation:
                emit(event, as_json)
                if event.get("kind") == "error" or event.get("status", 0):
                    raise SmpError(f"node operation {operation} failed")
                if event.get("kind") == "response":
                    return event
        if not batch.get("events"):
            time.sleep(0.05)
    raise SmpError(f"operation {operation} result timed out; use 'events watch'")


def service_operation(client, args, service, command, data=b""):
    cursor = client.request(64, 3, {"after": 0}).get("latest", 0)
    admitted = call(client, service, command, data)
    emit(admitted, args.json)
    return wait_result(client, admitted["operation"], cursor, args.wait_timeout, args.json)


def inspect_nordic_image(image: bytes) -> bytes:
    """Validate MCUboot bounds/hash and return its signed-region image hash."""
    if not 32 <= len(image) <= 2 * 1024 * 1024:
        raise SmpError("MCUboot signed .bin must be 32 bytes to 2 MiB (not a dfu zip)")
    magic, load_address, header_size, protected_size, image_size, flags = struct.unpack("<IIHHII", image[:20])
    if magic != 0x96F3B83D or header_size < 32 or image_size == 0:
        raise SmpError("not a valid MCUboot signed .bin")
    # NCS nRF54L15 RRAM images set IMAGE_F_ROM_FIXED, with the primary slot's
    # address in ih_load_addr. This is also the normal OTA artifact format.
    if flags & ~0x100:
        raise SmpError("encrypted/compressed/special MCUboot images are unsupported by this CLI")
    if flags & 0x100 and load_address != 0x11000:
        raise SmpError("fixed-ROM image targets a different Nordic flash layout")
    offset = header_size + image_size
    if offset > len(image):
        raise SmpError("truncated MCUboot image body")
    if protected_size:
        if protected_size < 4 or offset + protected_size > len(image):
            raise SmpError("truncated protected MCUboot TLV")
        if struct.unpack("<HH", image[offset:offset + 4]) != (0x6908, protected_size):
            raise SmpError("invalid protected MCUboot TLV")
        offset += protected_size
    signed_region = image[:offset]
    if offset + 4 > len(image):
        raise SmpError("missing MCUboot hash TLV")
    magic, total = struct.unpack("<HH", image[offset:offset + 4])
    end = offset + total
    if magic != 0x6907 or total < 4 or end > len(image):
        raise SmpError("invalid MCUboot TLV area")
    offset += 4
    found = None
    while offset < end:
        if offset + 4 > end:
            raise SmpError("truncated MCUboot TLV entry")
        kind, length = struct.unpack("<HH", image[offset:offset + 4])
        offset += 4
        if offset + length > end:
            raise SmpError("MCUboot TLV entry out of bounds")
        if kind in (0x10, 0x12):
            algorithm = hashlib.sha256 if kind == 0x10 else hashlib.sha512
            if found is not None or length != algorithm().digest_size:
                raise SmpError("invalid/duplicate MCUboot hash TLV")
            found = image[offset:offset + length]
            if found != algorithm(signed_region).digest():
                raise SmpError("MCUboot image hash mismatch")
        offset += length
    if found is None:
        raise SmpError("MCUboot SHA256/SHA512 TLV missing")
    return found


def upload(client, path: pathlib.Path, as_json=False):
    image = path.read_bytes()
    image_digest = inspect_nordic_image(image)
    digest = hashlib.sha256(image).digest()
    offset = 0
    while offset < len(image):
        request = {"off": offset, "data": image[offset : offset + 256]}
        if offset == 0:
            request.update({"len": len(image), "sha": digest, "image": 0})
        response = client.request(1, 1, request, write=True)
        next_offset = response.get("off")
        if not isinstance(next_offset, int) or not offset < next_offset <= len(image):
            raise SmpError(f"invalid upload offset: {next_offset}")
        offset = next_offset
        if as_json:
            emit({"uploaded": offset, "total": len(image)}, True)
    emit({"uploaded": offset, "sha256": digest.hex()}, as_json)
    return image_digest


def reconnect_images(client, args, expected_hash=None):
    """Read-only reconnect after reset; no mutating command can be replayed."""
    port = client.serial.port
    identity = getattr(client, "port_identity", {"fixed_port": port})
    client.serial.close()
    deadline = time.monotonic() + 30
    last_error = None
    while time.monotonic() < deadline:
        time.sleep(1)
        try:
            port = reconnect_port(identity, devices())
            with open_serial(port, args.baud, args.timeout) as connection:
                probe = SmpClient(connection, timeout=min(args.timeout, 2), retries=0)
                listing = probe.request(1, 0)
                if expected_hash is not None:
                    active = [image for image in listing.get("images", [])
                              if image.get("active") and image.get("image", 0) == 0]
                    if not active or active[0].get("hash") != expected_hash:
                        last_error = SmpError("expected uploaded image is not active yet")
                        continue
                emit({"state": "reconnected", **listing}, args.json)
                return listing
        except (SmpError, serial.SerialException, OSError) as exc:
            last_error = exc
    raise SmpError(f"read-only reconnect did not verify boot within 30 seconds: {last_error}")


def reset_and_reconnect(client, args, expected_hash=None):
    try:
        emit(client.request(0, 5, write=True, retry=False), args.json)
    except SmpTimeout:
        # A reset may take effect before its acknowledgement reaches the UART.
        emit({"state": "reset_ack_timeout", "next": "read-only reconnect"}, args.json)
    return reconnect_images(client, args, expected_hash)


def image_hash(value: str) -> bytes:
    try:
        result = bytes.fromhex(value)
    except ValueError as exc:
        raise SmpError("image hash must be 64 or 128 hexadecimal characters") from exc
    if len(result) not in (32, 64):
        raise SmpError("image hash must be 64 or 128 hexadecimal characters")
    return result


def host_command(client, args):
    action = args.image_action
    if action == "list":
        emit(client.request(1, 0), args.json)
    elif action == "upload":
        upload(client, args.firmware, args.json)
    elif action in ("test", "confirm"):
        fields = {"confirm": action == "confirm"}
        if args.hash:
            fields["hash"] = image_hash(args.hash)
        emit(client.request(1, 0, fields, write=True), args.json)
    elif action == "reset":
        reset_and_reconnect(client, args)
    elif action == "update":
        expected_hash = inspect_nordic_image(args.firmware.read_bytes())
        before = client.request(1, 0)
        if any(image.get("slot") == 0 and image.get("hash") == expected_hash
               for image in before.get("images", [])):
            raise SmpError("requested image is already primary; build a distinct update image")
        upload(client, args.firmware, args.json)
        listing = client.request(1, 0)
        candidates = [image for image in listing.get("images", [])
                      if image.get("slot") == 1 and image.get("image", 0) == 0]
        if len(candidates) != 1 or candidates[0].get("hash") != expected_hash:
            raise SmpError("secondary slot does not match uploaded image; aborted before reset")
        emit(client.request(1, 0, {"hash": candidates[0]["hash"], "confirm": False}, write=True),
             args.json)
        listing = reset_and_reconnect(client, args, expected_hash)
        emit({"state": "updated", "images": listing.get("images", []),
              "next": "Firmware auto-confirms after node-discovery self-test; inspect active/confirmed flags"},
             args.json)


def parser():
    root = argparse.ArgumentParser(description=__doc__)
    # Common options are accepted before or after subcommands.
    root.add_argument("--port", help="J-Link VCOM, preferably /dev/serial/by-id/...")
    root.add_argument("--baud", type=int, default=115200)
    root.add_argument("--timeout", type=positive, default=3.0)
    root.add_argument("--retries", type=int, choices=range(6), default=2)
    root.add_argument("--wait-timeout", type=positive, default=10.0)
    root.add_argument("--json", action="store_true")
    root.add_argument("--yes", action="store_true", help="authorize requested destructive/control action")
    root.add_argument("--logs", action="store_true", help="print host logs to stderr")
    commands = root.add_subparsers(dest="command", required=True)
    device = commands.add_parser("device").add_subparsers(dest="device_action", required=True)
    device.add_parser("list")
    status = commands.add_parser("status")
    status.add_argument("--watch", action="store_true")
    commands.add_parser("metrics")
    demonstration = commands.add_parser("demo")
    demonstration.add_argument("--samples", type=int, default=100)
    demonstration.add_argument("--watch-seconds", type=float, default=600)
    demonstration.add_argument("--report", type=pathlib.Path)
    demonstration.add_argument("--exercise-controls", action="store_true")
    demonstration.add_argument("--require-acceptance", action="store_true",
                               help="fail unless full100sample/600second/event acceptance passes")
    services = commands.add_parser("services").add_subparsers(dest="services_action", required=True)
    services.add_parser("list")
    sensor = commands.add_parser("sensor").add_subparsers(dest="sensor_action", required=True)
    sensor.add_parser("sample")
    actuator = commands.add_parser("actuator").add_subparsers(dest="actuator_action", required=True)
    start = actuator.add_parser("start")
    start.add_argument("--duration-ms", type=int, default=2000)
    event = commands.add_parser("events").add_subparsers(dest="events_action", required=True)
    watch = event.add_parser("watch")
    watch.add_argument("--after", type=number, default=0)
    diagnostics = commands.add_parser("diagnostics").add_subparsers(dest="diagnostic_action", required=True)
    for action in ("status", "clear", "records"):
        diagnostics.add_parser(action)
    inject = diagnostics.add_parser("inject")
    inject.add_argument("injection", type=int, choices=range(1, 9))
    node = commands.add_parser("node").add_subparsers(dest="node_action", required=True)
    for action in ("maintenance", "shutdown"):
        mode = node.add_parser(action)
        mode.add_argument("mode", choices=("enter", "exit"))
    node.add_parser("reset")
    node.add_parser("status")
    node.add_parser("versions")
    generic = commands.add_parser("service").add_subparsers(dest="service_action", required=True)
    generic_call = generic.add_parser("call")
    generic_call.add_argument("service", type=number)
    generic_call.add_argument("service_command", type=number)
    generic_call.add_argument("--data", default="", help="hexadecimal service payload")
    host = commands.add_parser("host").add_subparsers(dest="host_action", required=True)
    images = host.add_parser("image").add_subparsers(dest="image_action", required=True)
    images.add_parser("list")
    for action in ("upload", "update"):
        image = images.add_parser(action)
        image.add_argument("firmware", type=pathlib.Path, help="signed MCUboot .bin (not dfu zip)")
    test = images.add_parser("test")
    test.add_argument("hash")
    confirm = images.add_parser("confirm")
    confirm.add_argument("hash", nargs="?")
    images.add_parser("reset")
    stm = commands.add_parser("stm").add_subparsers(dest="stm_action", required=True)
    stm.add_parser("status")
    stm.add_parser("abort")
    stm.add_parser("recover-status")
    stm.add_parser("recover-abort")
    recover = stm.add_parser("recover")
    recover.add_argument("manifest", type=pathlib.Path, nargs="?")
    recover.add_argument("--public-key", type=pathlib.Path)
    recover.add_argument("--key-id", type=number, default=1)
    recover.add_argument("--rom-prerequisites-verified", action="store_true",
                         help="confirm manual boot options, 3.3 V wiring and SWD recovery")
    inspect = stm.add_parser("image").add_subparsers(dest="stm_image_action", required=True)
    for image in (stm.add_parser("update"), inspect.add_parser("inspect")):
        image.add_argument("firmware", type=pathlib.Path)
        image.add_argument("--public-key", type=pathlib.Path, required=True)
        image.add_argument("--key-id", type=number, default=1)
    return root


def parse_args(argv=None):
    # Move global options to the front so --port/--json/--yes work at every depth.
    argv = list(sys.argv[1:] if argv is None else argv)
    options_with_value = {"--port", "--baud", "--timeout", "--retries", "--wait-timeout"}
    flags = {"--json", "--yes", "--logs"}
    common, rest = [], []
    offset = 0
    while offset < len(argv):
        arg = argv[offset]
        if arg in flags or any(arg.startswith(name + "=") for name in options_with_value):
            common.append(arg)
        elif arg in options_with_value:
            common.extend(argv[offset : offset + 2])
            offset += 1
        else:
            rest.append(arg)
        offset += 1
    return parser().parse_args(common + rest)


def requires_yes(args):
    return (args.command == "service" or
            (args.command == "demo" and args.exercise_controls) or
            (args.command == "stm" and args.stm_action in ("update", "abort", "recover", "recover-abort")) or
            (args.command == "node" and args.node_action not in ("status", "versions")) or
            (args.command == "diagnostics" and args.diagnostic_action in ("inject", "clear")) or
            (args.command == "host" and args.image_action != "list"))


def execute(client, args):
    if args.command == "metrics":
        metrics = client.request(64, 7, retry=False)
        if metrics.get("host_boot_id") != client.host_boot_id:
            raise SmpError("metrics host boot changed")
        emit(metrics, args.json)
    elif args.command == "demo":
        demo.run(client, args, sys.modules[__name__])
    elif args.command == "status":
        while True:
            emit(client.request(64, 0), args.json)
            if not args.watch:
                return
            time.sleep(1)
    elif args.command == "services":
        emit(client.request(64, 1), args.json)
    elif args.command == "sensor":
        service_operation(client, args, 0x1001, 1)
    elif args.command == "actuator":
        if not 100 <= args.duration_ms <= 60000:
            raise SmpError("duration must be between 100 and 60000 ms")
        service_operation(client, args, 0x1002, 1, struct.pack("<I", args.duration_ms))
    elif args.command == "diagnostics":
        command = {"status": 1, "inject": 2, "clear": 3, "records": 4}[args.diagnostic_action]
        payload = bytes([args.injection]) if command == 2 else b""
        for _ in range(33 if command == 4 else 1):
            result = service_operation(client, args, 2, command, payload)
            if command != 4 or result.get("data") == b"\x00":
                break
        else:
            raise SmpError("diagnostic drain limit reached; run diagnostics records again")
    elif args.command == "node":
        if args.node_action == "reset":
            job = stm.mutate(client, 4, "reset")
            deadline = time.monotonic() + 40
            while time.monotonic() < deadline:
                result = stm.status(client, job)
                stm.check_failure(result)
                if result.get("state") == "complete":
                    emit(result, args.json)
                    return
                time.sleep(0.1)
            raise stm.Indeterminate("reset timed out; use 'stm status'")
        if args.node_action in ("status", "versions"):
            command = 1 if args.node_action == "status" else 4
        else:
            command = {("maintenance", "enter"): 2, ("maintenance", "exit"): 3,
                       ("shutdown", "enter"): 5, ("shutdown", "exit"): 6}[(args.node_action, args.mode)]
        service_operation(client, args, 3, command)
    elif args.command == "service":
        try:
            payload = bytes.fromhex(args.data)
        except ValueError as exc:
            raise SmpError("--data must contain hexadecimal bytes") from exc
        service_operation(client, args, args.service, args.service_command, payload)
    elif args.command == "events":
        cursor = args.after
        while True:
            status = client.request(64, 0)
            if status.get("host_boot_id") != client.host_boot_id:
                emit({"event": "host_restarted", "host_boot_id": status.get("host_boot_id")},
                     args.json)
                client.host_boot_id = status.get("host_boot_id")
                cursor = 0
            batch = events(client, cursor)
            emit(batch, args.json)
            for event in batch.get("events", []):
                cursor = event["id"]
            if not batch.get("events"):
                time.sleep(0.2)
    elif args.command == "host":
        host_command(client, args)
    elif args.command == "stm":
        if args.stm_action == "status":
            emit(stm.status(client), args.json)
        elif args.stm_action == "abort":
            current = stm.status(client)
            if not current.get("active"):
                raise SmpError("no active STM job")
            emit({"job": stm.mutate(client, 5, "abort", current["job"])}, args.json)
        elif args.stm_action == "update":
            stm.update(client, args, emit)
        elif args.stm_action == "recover-status":
            emit(rom.status(client), args.json)
        elif args.stm_action == "recover-abort":
            current = rom.status(client)
            if not current.get("active"):
                raise SmpError("no active ROM recovery job")
            emit({"job": rom.mutate(client, "abort", current["job"])}, args.json)
        elif args.stm_action == "recover":
            rom.recover(client, args, emit)


def main(argv=None):
    args = parse_args(argv)
    try:
        if args.command == "device":
            emit({"devices": devices()}, args.json)
            return 0
        if args.command == "stm" and args.stm_action == "recover":
            if args.manifest is None or args.public_key is None:
                raise SmpError("ROM recovery unavailable without MANIFEST and --public-key")
        if requires_yes(args) and not args.yes:
            raise SmpError("this command requires --yes")
        if args.command == "stm":
            if args.stm_action == "recover":
                if not args.rom_prerequisites_verified:
                    raise SmpError("ROM erase requires --rom-prerequisites-verified after manual boot option/wiring checks")
                from firmware.release_bundle import load
                args.rom_preflight = load(args.manifest, args.public_key, args.key_id)
            if args.stm_action in ("image", "update"):
                args.stm_preflight = stm.preflight(args.firmware, args.public_key, args.key_id)
                if args.stm_action == "image":
                    emit(args.stm_preflight[1], args.json)
                    return 0
        available = devices()
        port = select_port(args.port, available)
        with open_serial(port, args.baud, args.timeout) as connection:
            log = (lambda line: print(line.decode("utf-8", errors="replace"), file=sys.stderr)) if args.logs else None
            client = SmpClient(connection, args.timeout, args.retries, log)
            client.port_identity = port_identity(port, available)
            if args.command != "host":
                status = client.request(64, 0)
                if status.get("api") != 1:
                    raise SmpError(f"unsupported control API: {status.get('api')}")
                client.host_boot_id = status.get("host_boot_id")
                if args.command in ("metrics", "demo") and not status.get("metrics"):
                    raise SmpError("command requires Phase 12 metrics firmware")
                if (args.command == "stm" or
                        (args.command == "node" and args.node_action == "reset")):
                    if not status.get("stm_update") or not status.get("supervision"):
                        raise SmpError("command requires Phase 10 supervision/update firmware")
                    if args.command == "stm" and args.stm_action.startswith("recover") and not status.get("rom_recovery"):
                        raise SmpError("command requires Phase 11 ROM recovery firmware")
            execute(client, args)
        return 0
    except KeyboardInterrupt:
        return 130
    except (SmpError, serial.SerialException, OSError, ValueError) as exc:
        if args.json:
            emit({"error": str(exc)}, True)
        else:
            print(f"mmctl: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
