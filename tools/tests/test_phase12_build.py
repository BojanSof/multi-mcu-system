"""Check final sysbuild profile selection without building an SDK."""

import os
import pathlib
import shutil
import subprocess
import sys

import pytest


@pytest.mark.parametrize("profile", ["phase12", "phase12-verbose"])
@pytest.mark.parametrize("enabled,linked,correct_level", [
    (True, True, True), (False, True, True), (True, False, True), (True, True, False),
])
def test_final_profile_contract(tmp_path, profile, enabled, linked, correct_level):
    root = pathlib.Path(__file__).resolve().parents[2]
    script = tmp_path / "scripts/build/build_nordic.sh"
    script.parent.mkdir(parents=True)
    shutil.copy2(root / "scripts/build/build_nordic.sh", script)
    version = tmp_path / ".ncs-workspace/nrf/VERSION"
    version.parent.mkdir(parents=True)
    version.write_text("3.4.1\n")
    config = "phase12_verbose.conf" if profile.endswith("verbose") else "phase12_optimized.conf"
    level = (3 if profile.endswith("verbose") else 2) if correct_level else 1
    cache = ("MULTI_MCU_PHASE9_CONTROL:BOOL=ON\nMULTI_MCU_PHASE10_UPDATE:BOOL=ON\n"
             "MULTI_MCU_PHASE11_ROM:BOOL=ON\n"
             f"MULTI_MCU_PHASE12_METRICS:BOOL={'ON' if enabled else 'OFF'}\n")
    map_text = "HostControl.c.obj HostUpdateAdapter.c.obj HostStmUpdate.c.obj Stm32RomBoot.c.obj"
    if linked:
        map_text += " HostControl_MetricsRead"
    west = tmp_path / "fake-west"
    west.write_text(
        f"#!{sys.executable}\nimport pathlib, sys\n"
        "assert '-Dnrf54l15-host_MULTI_MCU_PHASE12_METRICS=ON' in sys.argv\n"
        "assert '-DMULTI_MCU_PHASE12_METRICS=ON' not in sys.argv\n"
        f"assert any(arg.endswith(';{config}') for arg in sys.argv)\n"
        "output = pathlib.Path(sys.argv[sys.argv.index('--build-dir') + 1]) / 'nrf54l15-host'\n"
        "(output / 'zephyr').mkdir(parents=True)\n"
        f"(output / 'CMakeCache.txt').write_text({cache!r})\n"
        f"(output / 'zephyr/zephyr.map').write_text({map_text!r})\n"
        f"(output / 'zephyr/.config').write_text('CONFIG_LOG_MAX_LEVEL={level}\\n')\n"
    )
    west.chmod(0o755)
    result = subprocess.run([str(script), "--profile", profile],
                            env={**os.environ, "WEST": str(west)},
                            capture_output=True, text=True)
    assert (result.returncode == 0) == (enabled and linked and correct_level), result.stderr
    if result.returncode:
        assert "error: Phase 12" in result.stderr
