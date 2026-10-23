"""Explicit factory-ROM rescue; never reconnect or replay uncertain mutations."""

import argparse
import hashlib
import time

try:
    from . import stm
    from .smp import SmpError, SmpTimeout
except ImportError:
    import stm
    from smp import SmpError, SmpTimeout


def status(client, job=None):
    current = client.request(64, 6, retry=False)
    if current.get("host_boot_id") != client.host_boot_id or (
            job is not None and current.get("job") != job):
        raise stm.Indeterminate("host boot/job changed; use 'stm recover-status'; no replay")
    return current


def mutate(client, action, job=None, **fields):
    rid = stm.request_id()
    request = {"action": action, "request_id": rid,
               "expected_boot_id": client.host_boot_id, **fields}
    if job is not None:
        request["job"] = job
    try:
        result = client.request(64, 6, request, write=True, retry=False)
    except SmpTimeout:
        current = status(client)
        if current.get("job") != (job or rid):
            raise stm.Indeterminate("ROM admission uncertain; use 'stm recover-status'; no replay")
        # Same connection, boot and job proven: the host admission cache prevents
        # another physical erase/write. Never retry a lower-level ROM transaction.
        result = client.request(64, 6, request, write=True, retry=False)
    if result.get("request_id") != rid or result.get("job") != (job or rid):
        raise stm.Indeterminate("unexpected ROM admission; use 'stm recover-status'; no replay")
    return result["job"]


def poll(client, job, total, minimum, maximum, predicate, seconds=45):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        current = status(client, job)
        if current.get("state") in ("failed", "aborted") or current.get("error", 0):
            raise SmpError(f"ROM repair {current.get('state')}: error={current.get('error')}; "
                           "loader may be partial; use 'stm recover-status' or SWD recovery")
        if (current.get("total") != total or type(current.get("offset")) is not int or
                current["offset"] not in (minimum, maximum)):
            raise stm.Indeterminate("invalid ROM readback offset; use 'stm recover-status'; no replay")
        if predicate(current):
            return current
        time.sleep(0.05)
    raise stm.Indeterminate("ROM job timeout; use 'stm recover-status'; no replay")


def recover(client, args, emit):
    bundle = args.rom_preflight
    image = bundle.bootloader
    total = len(image)
    job = None
    finishing = False
    try:
        before = client.request(64, 0, retry=False)
        if before.get("host_boot_id") != client.host_boot_id or before.get("api") != 1:
            raise stm.Indeterminate("host changed before ROM repair; no mutation sent")
        previous_session = before.get("session", 0)
        if type(previous_session) is not int or not 0 <= previous_session <= 0xFFFFFFFF:
            raise stm.Indeterminate("invalid pre-repair session; no mutation sent")
        job = mutate(client, "begin", total=total, sha256=hashlib.sha256(image).digest(),
                     prerequisites=True)
        poll(client, job, total, 0, 0, lambda value: value["state"] == "ready")
        offset = 0
        while offset < total:
            chunk = image[offset:offset + 256]
            mutate(client, "chunk", job, offset=offset, data=chunk)
            end = offset + len(chunk)
            result = poll(client, job, total, offset, end,
                          lambda value: value["offset"] == end)
            emit(result, args.json)
            offset = end
        finishing = True
        mutate(client, "finish", job)
        result = poll(client, job, total, total, total,
                      lambda value: value["state"] == "complete")
        emit(result, args.json)
        # Only an explicit responsive offline state triggers chained recovery.
        # Transport loss or host restart must not be interpreted as missing apps.
        deadline = time.monotonic() + 30
        current = {}
        while time.monotonic() < deadline:
            current = client.request(64, 0, retry=False)
            if current.get("host_boot_id") != client.host_boot_id:
                raise stm.Indeterminate("host restarted; ROM outcome uncertain; no signed update replay")
            if current.get("api") != 1:
                raise SmpError("incompatible runtime control API after ROM repair")
            session = current.get("session")
            if (current.get("state") == "online" and type(session) is int and session != 0 and
                    session != previous_session):
                emit({"state": "runtime_online", "preserved_applications": True}, args.json)
                return
            time.sleep(0.2)
        if current.get("state") == "online":
            raise stm.Indeterminate("post-repair runtime session did not change; no signed update sent")
        emit({"state": "runtime_offline", "next": "signed Slot 0 recovery"}, args.json)
        chained = argparse.Namespace(**vars(args))
        chained.stm_preflight = (bundle.slot_images[0], bundle.slot_manifests[0])
        # Phase 10 still queries journal/protects confirmed slots. Never clear or
        # override metadata if Slot 0 is protected, even when runtime is invalid.
        try:
            stm.update(client, chained, emit)
        except stm.JobFailure as failure:
            rejected = failure.status
            if not (rejected.get("slot") == 0 and rejected.get("offset") == 0 and
                    rejected.get("total") == len(bundle.slot_images[0]) and
                    rejected.get("error") == -18 and rejected.get("protocol") == 6 and
                    type(rejected.get("job")) is int and rejected["job"] != 0):
                raise
            # Invalid applications can leave a confirmed Slot 0 journal entry.
            # Never clear metadata or retry that protected slot. A fresh Slot 1
            # job is safe only after the exact failed job has released ownership.
            deadline = time.monotonic() + 40
            while time.monotonic() < deadline:
                rejected = stm.status(client, rejected["job"])
                if not (rejected.get("slot") == 0 and rejected.get("offset") == 0 and
                        rejected.get("total") == len(bundle.slot_images[0]) and
                        rejected.get("error") == -18 and rejected.get("protocol") == 6):
                    raise stm.Indeterminate("protected-slot status changed; no recovery fallback")
                if not rejected.get("active", True):
                    break
                time.sleep(0.2)
            else:
                raise stm.Indeterminate("protected-slot job still active; no recovery fallback")
            runtime = client.request(64, 0, retry=False)
            if runtime.get("host_boot_id") != client.host_boot_id or runtime.get("api") != 1:
                raise stm.Indeterminate("host changed before inactive-slot recovery; no replay")
            session = runtime.get("session")
            if (runtime.get("state") == "online" and type(session) is int and session != 0 and
                    session != previous_session):
                emit({"state": "runtime_online", "preserved_applications": True}, args.json)
                return
            if runtime.get("state") == "online":
                raise stm.Indeterminate("runtime session is stale; no inactive-slot fallback")
            emit({"state": "slot0_protected", "next": "new signed Slot 1 job"}, args.json)
            chained.stm_preflight = (bundle.slot_images[1], bundle.slot_manifests[1])
            stm.update(client, chained, emit)
    except KeyboardInterrupt:
        if job is not None and not finishing:
            try:
                status(client, job)
                mutate(client, "abort", job)
            except (SmpError, OSError):
                pass
        raise
    except (SmpTimeout, OSError) as error:
        raise stm.Indeterminate("ROM transport lost; loader may be partial; "
                                "use 'stm recover-status'; no replay") from error
