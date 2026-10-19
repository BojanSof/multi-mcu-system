import json
import pathlib
import subprocess
import sys
import tempfile
import unittest


HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from hil_runner import evaluate, load_matrix  # noqa: E402


class HilRunnerTests(unittest.TestCase):
    def setUp(self):
        self.matrix = load_matrix(HERE / "matrix.json")

    def test_simulated_markers_pass_with_explicit_provenance(self):
        result = evaluate(
            "phase5_smoke",
            self.matrix["phase5_smoke"],
            {
                "host": "node link: online\nvirtual sensor sample: value 1",
                "node": "stm32 heartbeat",
            },
            "simulated fixture",
        )
        self.assertEqual(result.status, "PASS")
        self.assertEqual(result.execution, "simulated fixture")

    def test_missing_marker_fails_deterministically(self):
        result = evaluate(
            "phase5_smoke", self.matrix["phase5_smoke"], {"host": "", "node": ""}, "simulated fixture"
        )
        self.assertEqual(result.status, "FAIL")
        self.assertEqual(result.missing["node"], ["heartbeat"])

    def run_cli(self, *arguments):
        result = subprocess.run(
            [sys.executable, str(HERE / "run_hil.py"), "--scenario", "phase5_smoke", *arguments],
            check=False, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
        return result, json.loads(result.stdout)

    def test_default_is_not_run_hardware(self):
        process, result = self.run_cli()
        self.assertEqual(process.returncode, 0)
        self.assertEqual(result["status"], "NOT_RUN_HARDWARE")

    def test_hardware_requires_electrical_verification(self):
        process, result = self.run_cli(
            "--run-hardware", "--host-port", "/dev/a", "--node-port", "/dev/b"
        )
        self.assertEqual(process.returncode, 1)
        self.assertEqual(result["status"], "BLOCKED_ELECTRICAL")

    def test_hardware_requires_two_distinct_ports(self):
        process, result = self.run_cli(
            "--run-hardware", "--electrical-verified", "--host-port", "/dev/a",
            "--node-port", "/dev/a",
        )
        self.assertEqual(process.returncode, 1)
        self.assertEqual(result["status"], "BLOCKED_TOOL")

    def test_fixture_cli_is_labeled_simulated(self):
        process, result = self.run_cli(
            "--fixture-host", str(HERE / "fixtures" / "phase5_host.log"),
            "--fixture-node", str(HERE / "fixtures" / "phase5_node.log"),
        )
        self.assertEqual(process.returncode, 0)
        self.assertEqual(result["status"], "PASS")
        self.assertEqual(result["execution"], "simulated fixture")

    def test_malformed_matrix_is_blocked_tool(self):
        with tempfile.TemporaryDirectory() as directory:
            matrix = pathlib.Path(directory) / "bad.json"
            matrix.write_text("[]", encoding="utf-8")
            process, result = self.run_cli("--matrix", str(matrix))
        self.assertEqual(process.returncode, 1)
        self.assertEqual(result["status"], "BLOCKED_TOOL")


if __name__ == "__main__":
    unittest.main()
