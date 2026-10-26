"""Guard and evidence tests. Peers are simulated; these are not HIL results."""

import contextlib
import io
import json
import pathlib
import sys
import tempfile
import types
import unittest
from unittest import mock

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parents[1] / "tools"))
import reliability_suite as suite


class ReliabilitySuiteTests(unittest.TestCase):
    def execute(self, *args):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            result = suite.main(list(args))
        return result, json.loads(output.getvalue())

    def test_default_does_not_open_device_or_call_subprocess(self):
        with mock.patch.object(suite.subprocess, "run", side_effect=AssertionError("external call")):
            code, report = self.execute()
        self.assertEqual(code, 0)
        self.assertEqual(report["status"], "NOT_RUN_HARDWARE")
        self.assertEqual(set(report["checks"]), set(suite.SAFE_SUITES))

    def test_hardware_requires_explicit_port_and_electrical_check(self):
        with mock.patch.object(suite, "command", side_effect=AssertionError("opened device")):
            for args in (("--run-hardware",), ("--run-hardware", "--port", "/dev/fake")):
                with self.subTest(args=args):
                    code, report = self.execute(*args)
                    self.assertEqual(code, 1)
                    self.assertIn("electrical", report["error"])

    def test_every_destructive_prerequisite_is_required_before_open(self):
        base = ["--run-hardware", "--electrical-verified", "--port", "/dev/fake",
                "--suite", "interruption", "--destructive", "--yes", "--rescue-verified",
                "--manifest", "release.json", "--public-key", "public.pem",
                "--swd-serial", "123", "--slot", "1"]
        with mock.patch.object(suite, "destructive_preflight", side_effect=AssertionError("preflight")):
            for option in ("--destructive", "--yes", "--rescue-verified", "--manifest",
                           "--public-key", "--swd-serial", "--slot"):
                with self.subTest(option=option):
                    args = base[:]
                    index = args.index(option)
                    del args[index:index + (2 if option in (
                        "--manifest", "--public-key", "--swd-serial", "--slot") else 1)]
                    code, report = self.execute(*args)
                    self.assertEqual(code, 1)
                    self.assertIn("requires", report["error"])

    def test_existing_evidence_is_not_overwritten(self):
        with tempfile.TemporaryDirectory() as directory:
            report = pathlib.Path(directory) / "report.json"
            report.write_text("original")
            code, result = self.execute("--report", str(report))
            self.assertEqual(code, 1)
            self.assertIn("already exists", result["error"])
            self.assertEqual(report.read_text(), "original")

    def test_new_report_has_explicit_non_hardware_provenance(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "new/report.json"
            code, report = self.execute("--report", str(path))
            self.assertEqual(code, 0)
            self.assertEqual(json.loads(path.read_text()), report)
            self.assertEqual(report["mode"], "not_run")

    def test_cli_failure_is_not_pass(self):
        with mock.patch.object(suite, "command", side_effect=ValueError("peer timeout")):
            code, report = self.execute("--suite", "startup", "--run-hardware",
                                        "--electrical-verified", "--port", "/dev/fake")
        self.assertEqual(code, 1)
        self.assertEqual(report["status"], "FAIL")

    def test_safe_read_checks_do_not_promote_manual_evidence(self):
        def fake_command(args, *words):
            if words == ("status",):
                return [{"host_boot_id": 7, "state": "online"}]
            if words == ("sensor", "sample"):
                return [{"decoded": {"value_milli_units": 8}}]
            if words == ("services", "list"):
                return [{"services": [{"id": 0x1001, "major": 1}]}]
            if words == ("node", "versions"):
                return [{"decoded": {"wire": [1, 0], "management": [1, 0]}}]
            if words == ("node", "recovery", "status"):
                return [{"host_boot_id": 7, "health": "healthy", "watchdog": True,
                         "recovery_attempts": 0}]
            if words == ("node", "faults", "list"):
                return [{"service": 3, "code": 10, "decoded": {"healthy": True}}]
            if words == ("--yes", "service", "call", "3", "12"):
                return [{"decoded": {"exhausted": 0, "button_dropped": 0}}]
            return [{"status": "PASS", "checks": {"actuator_progress_completion": "PASS"}}]
        with mock.patch.object(suite, "command", side_effect=fake_command):
            code, report = self.execute("--run-hardware", "--electrical-verified", "--port", "/dev/fake")
        self.assertEqual(code, 0)
        self.assertEqual(report["status"], "NOT_RUN_HARDWARE")
        self.assertEqual(sum(check["status"] == "PASS" for check in report["checks"].values()), 6)

    def test_bundle_validation_precedes_probe_enumeration(self):
        from firmware import release_bundle
        args = suite.parse_args(["--manifest", "missing", "--public-key", "missing"])
        with mock.patch.object(release_bundle, "load", side_effect=ValueError("invalid bundle")), \
                mock.patch.object(suite.subprocess, "run", side_effect=AssertionError("probe opened")):
            with self.assertRaisesRegex(ValueError, "invalid bundle"):
                suite.destructive_preflight(args)

    def test_rescue_probe_requires_full_serial_not_substring(self):
        from firmware import release_bundle
        args = suite.parse_args(["--manifest", "manifest", "--public-key", "public",
                                 "--swd-serial", "12345678"])
        result = types.SimpleNamespace(returncode=0, stdout="ST-LINK SN : 001234567899", stderr="")
        with mock.patch.object(release_bundle, "load", return_value=object()), \
                mock.patch.object(suite.subprocess, "run", return_value=result):
            with self.assertRaisesRegex(ValueError, "not enumerated"):
                suite.destructive_preflight(args)
            result.stdout = "ST-LINK SN : 12345678\n"
            suite.destructive_preflight(args)

    def test_abort_partial_transfer_never_commits(self):
        from cli import mmctl, stm, smp
        args = suite.parse_args(["--slot", "1", "--port", "/dev/fake"])
        bundle = types.SimpleNamespace(slot_images=(bytes(800), bytes(800)))
        writes = []
        state = {"host_boot_id": 7, "confirmed_slot": 0, "active": False,
                 "state": "idle", "job": 9}
        class Client:
            host_boot_id = 7
            def request(self, *args, **kwargs):
                return {"api": 1, "stm_update": True, "host_boot_id": 7, "state": "online"}
        def mutate(client, command, action, *args, **kwargs):
            writes.append(action)
            if action == "abort":
                state.update(active=False, state="aborted")
            else:
                state.update(active=True, state="ready")
            return 9
        checks = {}
        with mock.patch.object(mmctl, "open_serial", return_value=contextlib.nullcontext(object())), \
                mock.patch.object(smp, "SmpClient", return_value=Client()), \
                mock.patch.object(stm, "status", side_effect=lambda *a: dict(state)), \
                mock.patch.object(stm, "mutate", side_effect=mutate), \
                mock.patch.object(stm, "poll", return_value={"confirmed_slot": 0, "offset": 256}):
            suite.interrupted_transfer(args, bundle, checks)
        self.assertEqual(writes, ["begin", "chunk", "abort"])
        self.assertEqual(checks["partial_transfer_abort"]["status"], "PASS")
        self.assertEqual(checks["physical_power_interruption"]["status"], "NOT_RUN_HARDWARE")


if __name__ == "__main__":
    unittest.main()
