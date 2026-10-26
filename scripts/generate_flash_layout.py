#!/usr/bin/env python3
"""Validate the STM32 flash schema and generate C/linker constants."""

from __future__ import annotations

import argparse
import pathlib
from typing import Any

import yaml


def integer(value: Any, name: str) -> int:
    if not isinstance(value, int) or value < 0:
        raise ValueError(f"{name} must be a non-negative integer")
    return value


def region(profile: dict[str, Any], name: str) -> tuple[int, int]:
    item = profile.get(name)
    if not isinstance(item, dict):
        raise ValueError(f"missing region {name}")
    return integer(item.get("origin"), f"{name}.origin"), integer(item.get("size"), f"{name}.size")


def linker_script(flash_origin: int, flash_size: int, expected_vector: int) -> str:
    return f"""/* Generated from flash_layout.yaml; do not edit. */
ENTRY(Reset_Handler)

_estack = 0x20000000 + 40K;
_Min_Heap_Size = 0x200;
_Min_Stack_Size = 0x400;

MEMORY
{{
  FLASH (rx) : ORIGIN = 0x{flash_origin:08X}, LENGTH = 0x{flash_size:X}
  RAM (xrw)  : ORIGIN = 0x20000000, LENGTH = 40K
}}

SECTIONS
{{
  .isr_vector : {{ . = ALIGN(4); KEEP(*(.isr_vector)) . = ALIGN(4); }} >FLASH
  ASSERT(ADDR(.isr_vector) == 0x{expected_vector:08X}, "vector table address mismatch")

  .text :
  {{
    . = ALIGN(4); *(.text) *(.text*) *(.glue_7) *(.glue_7t) *(.eh_frame)
    KEEP(*(.init)) KEEP(*(.fini)) . = ALIGN(4); _etext = .;
  }} >FLASH
  .rodata : {{ . = ALIGN(4); *(.rodata) *(.rodata*) . = ALIGN(4); }} >FLASH
  .ARM.extab : {{ *(.ARM.extab* .gnu.linkonce.armextab.*) }} >FLASH
  .ARM : {{ __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; }} >FLASH
  .preinit_array :
  {{
    PROVIDE_HIDDEN(__preinit_array_start = .); KEEP(*(.preinit_array*))
    PROVIDE_HIDDEN(__preinit_array_end = .);
  }} >FLASH
  .init_array :
  {{
    PROVIDE_HIDDEN(__init_array_start = .); KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*)) PROVIDE_HIDDEN(__init_array_end = .);
  }} >FLASH
  .fini_array :
  {{
    PROVIDE_HIDDEN(__fini_array_start = .); KEEP(*(SORT(.fini_array.*)))
    KEEP(*(.fini_array*)) PROVIDE_HIDDEN(__fini_array_end = .);
  }} >FLASH

  _sidata = LOADADDR(.data);
  .data :
  {{
    . = ALIGN(4); _sdata = .; *(.data) *(.data*) . = ALIGN(4); _edata = .;
  }} >RAM AT>FLASH
  .bss :
  {{
    . = ALIGN(4); _sbss = .; __bss_start__ = _sbss; *(.bss) *(.bss*) *(COMMON)
    . = ALIGN(4); _ebss = .; __bss_end__ = _ebss;
  }} >RAM
  ._user_heap_stack :
  {{
    . = ALIGN(8); PROVIDE(end = .); PROVIDE(_end = .);
    . = . + _Min_Heap_Size; . = . + _Min_Stack_Size; . = ALIGN(8);
  }} >RAM
  ASSERT(_etext <= ORIGIN(FLASH) + LENGTH(FLASH), "FLASH region overflow")
  ASSERT(_ebss <= ORIGIN(RAM) + LENGTH(RAM), "RAM region overflow")
  /DISCARD/ : {{ libc.a(*) libm.a(*) libgcc.a(*) }}
  .ARM.attributes 0 : {{ *(.ARM.attributes) }}
}}
"""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("schema", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    args = parser.parse_args()

    data = yaml.safe_load(args.schema.read_text(encoding="utf-8"))
    if data.get("schema_version") != 2:
        raise ValueError("Phase 7 requires flash schema version 2")
    device = data["device"]
    flash_origin = integer(device["flash_origin"], "flash_origin")
    flash_size = integer(device["flash_size"], "flash_size")
    page_size = integer(device["erase_page_size"], "erase_page_size")
    profile = data["profiles"]["custom_ab_provisional"]
    if profile.get("enabled") is not True:
        raise ValueError("custom A/B profile is not enabled")
    header_size = integer(profile["image_header_size"], "image_header_size")
    if header_size != 0x200:
        raise ValueError("signed header must remain 0x200 bytes")

    boot_origin, boot_size = region(profile, "bootloader")
    slot0_origin, slot0_size = region(profile, "slot0")
    slot1_origin, slot1_size = region(profile, "slot1")
    metadata_origin, metadata_size = region(profile, "metadata")
    slot0_vector = integer(profile["slot0"]["vector_origin"], "slot0.vector_origin")
    slot1_vector = integer(profile["slot1"]["vector_origin"], "slot1.vector_origin")
    ordered = [
        (boot_origin, boot_size, "bootloader"),
        (slot0_origin, slot0_size, "slot0"),
        (slot1_origin, slot1_size, "slot1"),
        (metadata_origin, metadata_size, "metadata"),
    ]
    cursor = flash_origin
    for origin, size, name in ordered:
        if origin != cursor or origin % page_size or size % page_size:
            raise ValueError(f"{name} is not contiguous and page aligned")
        cursor = origin + size
    if cursor != flash_origin + flash_size:
        raise ValueError("A/B regions do not exactly cover device flash")
    if slot0_vector != slot0_origin + header_size or slot1_vector != slot1_origin + header_size:
        raise ValueError("slot vector origin must follow its signed header")
    for vector in (slot0_vector, slot1_vector):
        if vector % 0x200:
            raise ValueError("VTOR address must be 0x200 aligned")

    journal0_origin, journal0_size = region(profile["metadata"], "journal0")
    journal1_origin, journal1_size = region(profile["metadata"], "journal1")
    if (journal0_origin, journal0_size) != (metadata_origin, page_size):
        raise ValueError("journal0 must be the first metadata page")
    if (journal1_origin, journal1_size) != (metadata_origin + page_size, page_size):
        raise ValueError("journal1 must be the second metadata page")

    fault0_origin, fault0_size = region(profile["metadata"], "fault0")
    fault1_origin, fault1_size = region(profile["metadata"], "fault1")
    reserved_origin, reserved_size = region(profile["metadata"], "reserved")
    if (fault0_origin, fault0_size) != (metadata_origin + 2 * page_size, page_size):
        raise ValueError("fault0 must follow the two boot journal pages")
    if (fault1_origin, fault1_size) != (fault0_origin + page_size, page_size):
        raise ValueError("fault1 must follow fault0")
    if (reserved_origin != fault1_origin + page_size or
            reserved_origin + reserved_size != metadata_origin + metadata_size):
        raise ValueError("reserved flash must follow fault pages and end at metadata boundary")

    args.output.mkdir(parents=True, exist_ok=True)
    constants = f"""/* Generated from flash_layout.yaml; do not edit. */
#ifndef MULTI_MCU_FLASH_LAYOUT_H
#define MULTI_MCU_FLASH_LAYOUT_H
#define MULTI_MCU_FLASH_PAGE_SIZE 0x{page_size:X}UL
#define MULTI_MCU_BOOTLOADER_ORIGIN 0x{boot_origin:08X}UL
#define MULTI_MCU_BOOTLOADER_SIZE 0x{boot_size:X}UL
#define MULTI_MCU_SLOT0_ORIGIN 0x{slot0_origin:08X}UL
#define MULTI_MCU_SLOT0_VECTOR_ORIGIN 0x{slot0_vector:08X}UL
#define MULTI_MCU_SLOT1_ORIGIN 0x{slot1_origin:08X}UL
#define MULTI_MCU_SLOT1_VECTOR_ORIGIN 0x{slot1_vector:08X}UL
#define MULTI_MCU_SLOT_SIZE 0x{slot0_size:X}UL
#define MULTI_MCU_IMAGE_HEADER_SIZE 0x{header_size:X}UL
#define MULTI_MCU_METADATA_ORIGIN 0x{metadata_origin:08X}UL
#define MULTI_MCU_METADATA_SIZE 0x{metadata_size:X}UL
#define MULTI_MCU_METADATA_JOURNAL0_ORIGIN 0x{journal0_origin:08X}UL
#define MULTI_MCU_METADATA_JOURNAL1_ORIGIN 0x{journal1_origin:08X}UL
#define MULTI_MCU_FAULT_JOURNAL0_ORIGIN 0x{fault0_origin:08X}UL
#define MULTI_MCU_FAULT_JOURNAL1_ORIGIN 0x{fault1_origin:08X}UL
#endif
"""
    (args.output / "flash_layout.h").write_text(constants, encoding="utf-8")
    (args.output / "bootloader.ld").write_text(
        linker_script(boot_origin, boot_size, boot_origin), encoding="utf-8"
    )
    (args.output / "slot0.ld").write_text(
        linker_script(slot0_vector, slot0_size - header_size, slot0_vector), encoding="utf-8"
    )
    (args.output / "slot1.ld").write_text(
        linker_script(slot1_vector, slot1_size - header_size, slot1_vector), encoding="utf-8"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
