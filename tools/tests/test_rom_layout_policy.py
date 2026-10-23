"""Reject drift between the narrowly allowed ROM range and canonical layout."""

import pathlib
import re

import yaml


def test_rom_policy_matches_canonical_bootloader_pages():
    root = pathlib.Path(__file__).resolve().parents[2]
    layout = yaml.safe_load((root / "firmware/stm32u083/memory/flash_layout.yaml").read_text())
    profile = layout["profiles"]["custom_ab_provisional"]
    header = (root / "libraries/stm32-rom-boot/include/stm32_rom_boot/Stm32RomBoot.h").read_text()
    for name, value in (("BASE", profile["bootloader"]["origin"]),
                        ("SIZE", profile["bootloader"]["size"]),
                        ("PAGE_SIZE", layout["device"]["erase_page_size"])):
        match = re.search(rf"#define STM32_ROM_BOOT_{name} UINT32_C\((0x[0-9a-fA-F]+)\)", header)
        assert match and int(match[1], 16) == value
    assert profile["bootloader"]["size"] // layout["device"]["erase_page_size"] == 32
    assert profile["bootloader"]["origin"] + profile["bootloader"]["size"] == profile["slot0"]["origin"]
