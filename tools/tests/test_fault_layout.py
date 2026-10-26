"""The application fault journal must never overlap code or boot selection metadata."""
import pathlib
import subprocess
import sys

import pytest
import yaml

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCHEMA = ROOT / "firmware/stm32u083/memory/flash_layout.yaml"


def generate(tmp_path, layout):
    schema = tmp_path / "layout.yaml"
    schema.write_text(yaml.safe_dump(layout))
    output = tmp_path / "generated"
    result = subprocess.run(
        [sys.executable, str(ROOT / "scripts/generate_flash_layout.py"), str(schema), str(output)],
        capture_output=True, text=True, check=False,
    )
    return result, output


def test_fault_journal_generated_bounds_preserve_existing_layout(tmp_path):
    layout = yaml.safe_load(SCHEMA.read_text())
    result, output = generate(tmp_path, layout)
    assert result.returncode == 0, result.stderr
    header = (output / "flash_layout.h").read_text()
    for macro, address in (
        ("MULTI_MCU_SLOT0_ORIGIN", "08010000"),
        ("MULTI_MCU_SLOT1_ORIGIN", "08026000"),
        ("MULTI_MCU_METADATA_JOURNAL0_ORIGIN", "0803C000"),
        ("MULTI_MCU_METADATA_JOURNAL1_ORIGIN", "0803C800"),
        ("MULTI_MCU_FAULT_JOURNAL0_ORIGIN", "0803D000"),
        ("MULTI_MCU_FAULT_JOURNAL1_ORIGIN", "0803D800"),
    ):
        assert f"#define {macro} 0x{address}UL" in header
    assert "ORIGIN = 0x08010200" in (output / "slot0.ld").read_text()
    assert "ORIGIN = 0x08026200" in (output / "slot1.ld").read_text()


@pytest.mark.parametrize("name,field,value", [
    ("fault0", "origin", 0x0803C800),
    ("fault0", "size", 0x1000),
    ("fault1", "origin", 0x0803D400),
    ("reserved", "origin", 0x0803D800),
    ("reserved", "size", 0x3000),
])
def test_fault_journal_rejects_overlaps_or_out_of_range(tmp_path, name, field, value):
    layout = yaml.safe_load(SCHEMA.read_text())
    layout["profiles"]["custom_ab_provisional"]["metadata"][name][field] = value
    result, output = generate(tmp_path, layout)
    assert result.returncode != 0
    assert not (output / "flash_layout.h").exists()
