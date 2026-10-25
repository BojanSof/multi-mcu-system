"""Bounded operator acceptance; no firmware writes or implicit reconnects."""

import json
import math
import pathlib
import struct
import sys
import time

try:
    from .smp import SmpError
    from .polling import CompletionBackoff
    from . import stm
except ImportError:
    from smp import SmpError
    from polling import CompletionBackoff
    import stm


def percentile(values, fraction):
    ordered = sorted(values)
    return ordered[max(0, math.ceil(len(ordered) * fraction) - 1)] if ordered else None


class NodeFailure(SmpError):
    def __init__(self, record):
        super().__init__(f"node event error for operation {record.get('operation')}")
        self.record = record


class Observer:
    def __init__(self, client, api, session, cursor):
        self.client, self.api, self.session, self.cursor = client, api, session, cursor
        self.operations = set()
        self.responses = set()
        self.records = 0
        self.asynchronous = 0
        self.latencies = []
        self.sample_indices = set()
        self.last_sample_index = None

    def status(self, lifecycle_transition=False):
        value = self.client.request(64, 0, retry=False)
        if value.get("api") != 1 or value.get("host_boot_id") != self.client.host_boot_id:
            raise SmpError("host changed during acceptance; no command replayed")
        if lifecycle_transition:
            # READY pulses restart the whole discovery sequence. HELLO restores
            # the original session before identity/capability/service queries.
            state, session = value.get("state"), value.get("session")
            if ((state == "discovering" and session in (0, self.session)) or
                    (state in ("identity", "capabilities", "services") and
                     session == self.session)):
                return value
        if value.get("session") != self.session or value.get("state") != "online":
            raise SmpError(f"node session/link changed during acceptance: state={value.get('state')}, "
                           f"session={value.get('session')}, expected={self.session}; no command replayed")
        return value

    def batch(self):
        value = self.api.events(self.client, self.cursor)
        if value.get("host_boot_id") != self.client.host_boot_id:
            raise SmpError("host changed during event watch; no command replayed")
        result = value.get("events")
        if not isinstance(result, list) or len(result) > 4:
            raise SmpError("invalid event page")
        for event in result:
            if not isinstance(event, dict) or event.get("kind") not in ("event", "response", "error"):
                raise SmpError("malformed event record")
            identifier = event.get("id")
            expected = ((self.cursor + 1) & 0xffffffff) or 1
            if type(identifier) is not int or identifier != expected:
                raise SmpError("unread event gap or duplicate event ID")
            if event.get("session") != self.session:
                raise SmpError("event belongs to a different node session; no command replayed")
            self.cursor = identifier
            self.records += 1
            if event.get("kind") == "event":
                self.asynchronous += 1
            elif event.get("kind") == "response":
                operation = event.get("operation")
                if operation not in self.operations:
                    raise SmpError("response for an unadmitted operation")
                if operation in self.responses:
                    raise SmpError("duplicate operation response")
                self.responses.add(operation)
            if event.get("kind") == "error" or event.get("status", 0):
                raise NodeFailure(event)
        return result

    def call(self, service, command, payload=b"", complete=False, seconds=10,
             lifecycle_transition=False):
        self.status()
        started = time.monotonic()
        admitted = self.api.call(self.client, service, command, payload)
        operation = admitted.get("operation")
        if type(operation) is not int or not 0 < operation <= 0xffffffff or operation in self.operations:
            raise SmpError("invalid or duplicate admitted operation ID")
        self.operations.add(operation)
        deadline = started + seconds
        backoff = CompletionBackoff()
        response, completion, progress = None, None, []
        while time.monotonic() < deadline:
            link = self.status(lifecycle_transition=lifecycle_transition)
            page = self.batch()
            for record in page:
                if record.get("operation") != operation:
                    continue
                if record.get("kind") == "response":
                    if record.get("service") != service:
                        raise SmpError("operation response service mismatch")
                    if record.get("code") != command:
                        raise SmpError("operation response command mismatch")
                    response = record
                elif (complete and record.get("kind") == "event" and
                      record.get("service") == service):
                    # Correlated diagnostics and independent GPIO event counters
                    # can share this operation number; only this service owns
                    # the actuator progress/completion schema.
                    if record.get("code") == 1:
                        raw = record.get("data", b"")
                        if not isinstance(raw, bytes) or len(raw) != 5:
                            raise SmpError("malformed actuator progress")
                        progress.append((raw[0], struct.unpack("<I", raw[1:])[0]))
                    elif record.get("code") == 2:
                        completion = record
            if (response is not None and (not complete or completion is not None) and
                    link.get("state") == "online" and link.get("session") == self.session):
                latency = (time.monotonic() - started) * 1000
                if service == 0x1001:
                    data = response.get("data")
                    if not isinstance(data, bytes) or len(data) != 8:
                        raise SmpError("malformed sensor result")
                    _, index = struct.unpack("<iI", data)
                    if index in self.sample_indices:
                        raise SmpError("duplicate sensor sample index")
                    if (self.last_sample_index is not None and
                            index != (self.last_sample_index + 1) & 0xffffffff):
                        raise SmpError("skipped sensor sample index; unexpected extra execution")
                    self.sample_indices.add(index)
                    self.last_sample_index = index
                    self.latencies.append(latency)
                if complete:
                    raw = response.get("data", b"")
                    end = completion.get("data", b"")
                    if (not isinstance(raw, bytes) or len(raw) != 4 or end != raw or
                            [step for step, _ in progress] != [25, 50, 75] or
                            any(execution != struct.unpack("<I", raw)[0] for _, execution in progress)):
                        raise SmpError("actuator progress/completion did not match one execution")
                return response
            if not page:
                backoff.wait(deadline)
        raise SmpError(f"operation {operation} incomplete; no command replayed")


def metric_delta(before, after, boot):
    if before.get("host_boot_id") != boot or after.get("host_boot_id") != boot:
        raise SmpError("metrics host boot changed")
    names = ("requests", "retries", "responses", "timeouts", "protocol_errors", "uart_errors",
             "rx_bytes", "tx_bytes", "rx_overflows", "tx_overflows", "events", "dropped",
             "manager_restarts", "online_count", "session_changes", "ready_losses", "node_reboots")
    result = {}
    for name in names:
        a, b = before.get(name), after.get(name)
        if type(a) is not int or type(b) is not int or a < 0 or b < a:
            raise SmpError(f"invalid/reset metrics counter: {name}")
        result[name] = b - a
    return result


def run(client, args, api, mode="hardware"):
    if type(args.samples) is not int or not 1 <= args.samples <= 10000:
        raise SmpError("--samples must be 1..10000")
    if not math.isfinite(args.watch_seconds) or not 0 <= args.watch_seconds <= 3600:
        raise SmpError("--watch-seconds must be 0..3600")
    report = {"format": "multi-mcu-acceptance", "format_version": 1, "mode": mode,
              "status": "FAIL", "acceptance_complete": False, "checks": {}}
    observer = None
    try:
        status = client.request(64, 0, retry=False)
        boot, session = status.get("host_boot_id"), status.get("session")
        if (status.get("api") != 1 or boot != client.host_boot_id or type(boot) is not int or
                boot == 0 or type(session) is not int or session == 0 or
                status.get("state") != "online" or status.get("busy")):
            raise SmpError("demo requires compatible online idle host/node")
        report.update(host_boot_id=boot, session=session)
        services = client.request(64, 1, retry=False).get("services")
        if (not isinstance(services, list) or len(services) > 16 or
                any(not isinstance(item, dict) or type(item.get("id")) is not int or
                    not 0 <= item["id"] <= 65535 for item in services)):
            raise SmpError("invalid service registry")
        available = {item.get("id") for item in services if isinstance(item, dict)}
        report["services"] = sorted(available)
        initial = client.request(64, 3, {"after": 0}, retry=False)
        if initial.get("host_boot_id") != boot or type(initial.get("latest")) is not int:
            raise SmpError("invalid initial event cursor")
        observer = Observer(client, api, session, initial["latest"])
        if 3 in available:
            lifecycle = observer.call(3, 1, seconds=args.wait_timeout).get("decoded", {})
            if lifecycle.get("state") != 2:
                raise SmpError("demo refuses maintenance/shutdown/non-online lifecycle")
            versions = observer.call(3, 4, seconds=args.wait_timeout)
            report["versions"] = versions.get("decoded", {})
            report["checks"]["lifecycle_versions"] = "PASS"
        else:
            raise SmpError("lifecycle service required to prove safe runtime state")
        if not status.get("metrics"):
            raise SmpError("demo requires Phase 12 metrics firmware")
        before = client.request(64, 7, retry=False)
        if 0x1001 not in available:
            report["checks"]["sensor_requests"] = "NOT_SUPPORTED"
        else:
            print(f"demo: requesting {args.samples} sequential sensor samples", file=sys.stderr,
                  flush=True)
            for _ in range(args.samples):
                observer.call(0x1001, 1, seconds=args.wait_timeout)
            report["checks"]["sensor_requests"] = "PASS"
        if 0x1002 in available:
            observer.call(0x1002, 1, struct.pack("<I", 2000), complete=True,
                          seconds=max(args.wait_timeout, 5))
            report["checks"]["actuator_progress_completion"] = "PASS"
        else:
            report["checks"]["actuator_progress_completion"] = "NOT_SUPPORTED"
        asynchronous_before = observer.asynchronous
        print(f"demo: watching events for {args.watch_seconds:g} seconds; press the STM user "
              "button a few times during the watch. Ctrl-C stops.", file=sys.stderr, flush=True)
        watch_started = time.monotonic()
        deadline = watch_started + args.watch_seconds
        next_progress = watch_started + 60
        while time.monotonic() < deadline:
            observer.status()
            if not observer.batch():
                time.sleep(min(0.2, max(0, deadline - time.monotonic())))
            now = time.monotonic()
            if now >= next_progress and now < deadline:
                print(f"demo: event watch {max(0, deadline - now):.0f} seconds remaining", 
                      file=sys.stderr, flush=True)
                next_progress = now + 60
        report["watch_seconds"] = args.watch_seconds
        report["watch_elapsed_seconds"] = max(0, time.monotonic() - watch_started)
        report["watch_async_events"] = observer.asynchronous - asynchronous_before
        report["checks"]["event_watch"] = (
            "PASS" if report["watch_async_events"] else "NOT_OBSERVED")
        after = client.request(64, 7, retry=False)
        report["metric_deltas"] = metric_delta(before, after, boot)
        report["metrics"] = after
        if any(report["metric_deltas"][name] for name in
               ("timeouts", "protocol_errors", "uart_errors", "rx_overflows", "tx_overflows")):
            raise SmpError("unexplained transport errors during reliability checks")
        report["checks"]["runtime_metrics"] = "PASS"
        if args.exercise_controls:
            controls(observer, available, args, report)
        report["status"] = "PASS"
        report["acceptance_complete"] = (args.samples >= 100 and args.watch_seconds >= 600 and
            report["watch_elapsed_seconds"] >= 600 and
            report["checks"].get("sensor_requests") == "PASS" and report["watch_async_events"] > 0)
        if args.require_acceptance and not report["acceptance_complete"]:
            raise SmpError("full acceptance incomplete: require100samples/600seconds/observed async events")
    except KeyboardInterrupt:
        report["error"] = "interrupted by operator; no command replayed"
        raise
    except (SmpError, OSError, ValueError) as error:
        report["status"] = "FAIL"
        report["error"] = str(error)
    finally:
        if observer is not None:
            report.update(sensor_responses=len(observer.sample_indices),
                          unique_operations=len(observer.operations), event_records=observer.records,
                          last_event_id=observer.cursor,
                          latency_ms={"p50": percentile(observer.latencies, .50),
                                      "p95": percentile(observer.latencies, .95),
                                      "max": max(observer.latencies, default=None)})
        if args.report:
            pathlib.Path(args.report).write_text(json.dumps(api.json_value(report), sort_keys=True,
                                                          indent=2) + "\n", encoding="utf-8")
        api.emit(report, args.json)
    if report["status"] != "PASS":
        raise SmpError(report.get("error", "acceptance failed"))
    return report


def controls(observer, services, args, report):
    try:
        observer.call(3, 2, seconds=args.wait_timeout, lifecycle_transition=True)
        report["checks"]["maintenance_enter"] = "PASS"
    finally:
        observer.call(3, 3, seconds=args.wait_timeout, lifecycle_transition=True)
    report["checks"]["maintenance_exit"] = "PASS"
    if 2 in services:
        diagnostics = observer.call(2, 1, seconds=args.wait_timeout).get("decoded", {})
        if diagnostics.get("injections"):
            raise SmpError("fault exercise refuses pre-existing injections")
        injected = False
        cleanup = True
        try:
            observer.call(2, 2, b"\x01", seconds=args.wait_timeout)
            injected = True
            observer.call(0x1001, 1, seconds=args.wait_timeout)
            report["checks"]["fault_exercise"] = "PASS"
        except NodeFailure as error:
            if (not injected and error.record.get("service") == 2 and
                    error.record.get("code") == 2 and error.record.get("status") == 6):
                report["checks"]["fault_exercise"] = "NOT_SUPPORTED"
                cleanup = False
            else:
                raise
        finally:
            if cleanup:
                observer.call(2, 3, seconds=args.wait_timeout)
    else:
        report["checks"]["fault_exercise"] = "NOT_SUPPORTED"
    job = stm.mutate(observer.client, 4, "reset")
    deadline = time.monotonic() + 45
    while time.monotonic() < deadline:
        value = stm.status(observer.client, job)
        stm.check_failure(value)
        if value.get("state") == "complete":
            current = observer.client.request(64, 0, retry=False)
            if (current.get("host_boot_id") != observer.client.host_boot_id or
                    current.get("state") != "online" or not current.get("session") or
                    current["session"] == observer.session):
                raise SmpError("reset did not produce fresh online session")
            report["checks"]["reset_recovery"] = "PASS"
            report["post_reset_session"] = current["session"]
            return
        time.sleep(0.1)
    raise SmpError("reset recovery timed out; no reset replayed")
