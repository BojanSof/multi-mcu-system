"""Exercise sysbuild flag forwarding and fail-fast artifact checks without an SDK."""

import os
import pathlib
import shutil
import subprocess
import sys

import pytest


@pytest.mark.parametrize("enabled,linked,success", [(True, True, True),
                                                   (False, True, False),
                                                   (True, False, False)])
def test_phase9_build_requires_control_in_application(tmp_path, enabled, linked, success):
    root = pathlib.Path(__file__).resolve().parents[2]
    script = tmp_path / "scripts/build/build_nordic.sh"
    script.parent.mkdir(parents=True)
    shutil.copy2(root / "scripts/build/build_nordic.sh", script)
    version = tmp_path / ".ncs-workspace/nrf/VERSION"
    version.parent.mkdir(parents=True)
    version.write_text("3.4.1\n")
    west = tmp_path / "fake-west"
    west.write_text(
        f"#!{sys.executable}\n"
        "import pathlib, sys\n"
        "assert '-Dnrf54l15-host_MULTI_MCU_PHASE9_CONTROL=ON' in sys.argv\n"
        "assert '-DMULTI_MCU_PHASE9_CONTROL=ON' not in sys.argv\n"
        "output = pathlib.Path(sys.argv[sys.argv.index('--build-dir') + 1]) / 'nrf54l15-host'\n"
        "(output / 'zephyr').mkdir(parents=True)\n"
        f"(output / 'CMakeCache.txt').write_text('MULTI_MCU_PHASE9_CONTROL:BOOL={'ON' if enabled else 'OFF'}\\n')\n"
        f"(output / 'zephyr/zephyr.map').write_text({'HostControl.c.obj' if linked else 'other.c.obj'!r})\n"
    )
    west.chmod(0o755)
    result = subprocess.run([str(script), "--profile", "phase9"],
                            env={**os.environ, "WEST": str(west)},
                            capture_output=True, text=True)
    assert (result.returncode == 0) == success, result.stderr
    if not success:
        assert "error: Phase 9 control" in result.stderr
