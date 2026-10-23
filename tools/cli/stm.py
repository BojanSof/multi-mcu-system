"""Signed STM preflight and bounded Phase 10 job execution."""

from dataclasses import asdict
import pathlib
import secrets
import sys
import time

try:
    from .smp import SmpError, SmpTimeout
except ImportError:
    from smp import SmpError, SmpTimeout

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from firmware.mmfw import parse_signed_image


class Indeterminate(SmpError):
    """Execution may have occurred; never reconnect and replay."""


def preflight(path, public_key, key_id=1):
    from cryptography.hazmat.primitives.serialization import load_pem_public_key
    image = pathlib.Path(path).read_bytes()
    key = load_pem_public_key(pathlib.Path(public_key).read_bytes())
    manifest, _ = parse_signed_image(image, public_key=key)
    if manifest.key_id != key_id:
        raise SmpError("image key ID does not match trusted --key-id")
    if (manifest.required_wire_major, manifest.required_wire_minor,
            manifest.required_management_major, manifest.required_management_minor) != (1, 0, 1, 0):
        raise SmpError("image requires incompatible wire/management versions")
    return image, asdict(manifest)


def request_id():
    return secrets.randbelow((1 << 64) - 1) + 1


def status(client, job=None):
    result = client.request(64, 5, retry=False)
    if result.get("host_boot_id") != client.host_boot_id or (
            job is not None and result.get("job") != job):
        raise Indeterminate("host boot/job changed; outcome uncertain; use 'stm status'; no replay")
    return result


def mutate(client, command, action, job=None, **fields):
    rid = request_id()
    body = {"action": action, "request_id": rid,
            "expected_boot_id": client.host_boot_id, **fields}
    if job is not None:
        body["job"] = job
    try:
        result = client.request(64, command, body, write=True, retry=False)
    except SmpTimeout:
        # Retry the identical request only after a read proves the same boot/job.
        current = status(client)
        expected = job if job is not None else rid
        if current.get("job") != expected:
            raise Indeterminate("admission outcome uncertain; use 'stm status'; no replay")
        result = client.request(64, command, body, write=True, retry=False)
    if result.get("request_id") != rid or result.get("job") != (job or rid):
        raise Indeterminate("unexpected admission identity; use 'stm status'; no replay")
    return result["job"]


def check_failure(current):
    if current.get("state") in ("failed", "error", "aborted", "rolled_back") or current.get("error", 0):
        raise SmpError(f"STM job {current.get('state')}: error={current.get('error')} "
                       f"protocol={current.get('protocol')}; use 'stm status'")


def poll(client, job, deadline, total, minimum, maximum, predicate):
    while time.monotonic() < deadline:
        current = status(client, job)
        check_failure(current)
        offset = current.get("offset")
        if (current.get("total") != total or type(offset) is not int or
                offset not in (minimum, maximum)):
            raise Indeterminate("invalid STM ACK offset/total; use 'stm status'; no replay")
        if predicate(current):
            return current
        time.sleep(0.05)
    raise Indeterminate("STM job timed out; use 'stm status'; no replay")


def update(client, args, emit):
    image, _ = args.stm_preflight
    total = len(image)
    job = None
    committing = False
    try:
        job = mutate(client, 5, "begin", header=image[:512], total=total)
        # Entry/query/erase may take 15 seconds plus a 5-second erase response.
        poll(client, job, time.monotonic() + 30, total, 0, 0,
             lambda value: value["state"] == "ready")
        offset = 0
        while offset < total:
            data = image[offset:offset + 256]
            mutate(client, 5, "chunk", job, offset=offset, data=data)
            end = offset + len(data)
            current = poll(client, job, time.monotonic() + max(args.wait_timeout, 20),
                           total, offset, end, lambda value: value["offset"] == end)
            offset = end
            emit(current, args.json)
        # Once finish is sent, it may commit even if its response is lost.
        committing = True
        mutate(client, 5, "finish", job)
        current = poll(client, job, time.monotonic() + 120, total, total, total,
                       lambda value: value["state"] == "complete")
        emit(current, args.json)
    except KeyboardInterrupt:
        if job is not None and not committing:
            try:
                status(client, job)
                mutate(client, 5, "abort", job)
            except (SmpError, OSError):
                pass
        raise
    except (SmpTimeout, OSError) as exc:
        raise Indeterminate("transport lost; outcome uncertain; use 'stm status'; no replay") from exc
