# Phase 7 plan and frozen interfaces

**Date:** 2026-10-18
**Base Git revision:** `64ed60a`

## Scope and gates

Phase 7 adds an independently buildable bare-metal STM32U083 bootloader, two separately linked
application images, signed-image tooling, transactional boot metadata, an inactive-slot UART update
protocol, host delivery, and application confirmation. ROM/SWD recovery remains available. Native
state-machine, malformed-image, flash-boundary, and reset-injection tests precede hardware writes.

The dependency audit found all required build and signing tools. Optional `cppcheck` is missing.
USB/libusb access is unavailable to this execution environment, so new HIL remains
`NOT_RUN_HARDWARE` until performed by the user.

## Frozen flash layout

`firmware/stm32u083/memory/flash_layout.yaml` is the sole authority. The Phase 7 profile is:

| Region | Base | Size | Notes |
|---|---:|---:|---|
| Bootloader | `0x08000000` | 64 KiB | bare-metal, fixed public key |
| Slot 0 | `0x08010000` | 88 KiB | vector table at `0x08010200` |
| Slot 1 | `0x08026000` | 88 KiB | vector table at `0x08026200` |
| Metadata | `0x0803C000` | 16 KiB | first two 2 KiB pages are redundant journals |

Each slot begins with a fixed 512-byte signed header. The raw application payload follows at the
slot-specific vector address. All erase regions are 2 KiB aligned; flash programming uses aligned
64-bit doublewords. Only the inactive slot may be erased or programmed.

## Signed image format v2

The existing unsigned MMFW v1 remains supported for Phase 6 tools and is not reinterpreted. Signed
v2 uses a 512-byte header: the first 80 bytes retain the explicit little-endian manifest fields,
with byte 42 `slot_id`, byte 43 `algorithm_id`, bytes 44..47 `key_id`, and bytes 48..79 the payload
SHA-256. Bytes 80..143 contain raw big-endian P-256 `r || s`; bytes 144..511 are zero. Flags must be
`AUTHENTICATED`, and payload length must exactly match the remainder of the image.

The canonical signed bytes are exactly header bytes 0..79. Because these bytes include payload
SHA-256, slot ID, vector/link address, target, hardware, version, build ID, key ID, and compatibility
requirements, any change invalidates the signature. Algorithm 1 is ECDSA-P256/SHA-256; key ID 1 is
the development key. Signatures use low-S canonical form. Private keys are external and ignored;
only the public key is compiled into the bootloader. This is demo authenticity, not production
secure boot while SWD/protection remain open.

## Boot metadata

Two distinct erase pages form an append-only journal. Fixed 64-byte records carry magic/schema,
generation, confirmed/pending/trial slots, attempts remaining, invalid-slot mask, update state and
offset, followed by CRC32. The final aligned doubleword contains the CRC commit marker and is
programmed last. Rollover writes and validates the alternate page before erasing the stale page.
An erased/torn/CRC-invalid record is ignored. Generation comparison is wrap-aware.

States are `CONFIRMED`, `PENDING`, `TRIAL`, `INVALID`, and `RECOVERY`. A pending image is fully
verified before selection. The bootloader journals `TRIAL` and decrements the bounded attempt count
before jumping. Application confirmation uses a narrow boot-control handshake consumed by the
bootloader; failure to confirm rolls back to the last valid confirmed slot. With no valid image, the
bootloader remains in UART recovery.

## Boot protocol

`BootProtocol.h` freezes version 1, a 20-byte little-endian header (16-byte prefix plus CRC32),
256-byte bounded payload,
sequence IDs, and commands `QUERY_SLOTS`, `ERASE_INACTIVE`, `WRITE_CHUNK`, `FINALIZE`,
`SET_PENDING`, `GET_BOOT_STATE`, `REBOOT`. Writes are contiguous stop-and-wait operations; exact
duplicate requests return the cached response, while conflicting duplicates, gaps, overlaps,
overflow, active-slot writes, metadata writes, and bootloader writes are rejected. Transport CRC
failure is distinct from signed-image verification failure.

## Implementation ownership

- Integrator: this plan, public schemas, flash layout/generator, root integration, final builds.
- Bootloader agent: `firmware/stm32u083/bootloader/` and its private state-machine tests.
- Protocol agent: `libraries/boot-protocol/` implementation/tests after this header freeze.
- Tooling/image agent: signed v2 parser/signing tools and golden vectors without changing frozen
  fields.
- Nordic agent: host delivery only after the protocol implementation stabilizes.
- Validation agent: independent native/reset-injection review and reproducible result report.

## Required acceptance

Native tests must cover canonical C/Python vectors; signature/key/algorithm/header/payload tamper;
vector and address bounds; exhaustive boot transitions; torn metadata writes at every commit point;
generation rollover; inactive-slot boundaries; duplicate/gap/overflow protocol behavior; and
private-key exclusion. Builds must enforce bootloader <64 KiB, each complete slot image <=88 KiB,
and RAM <40 KiB. HIL must separately boot both linked slots and exercise SysTick/FreeRTOS, UART and
button interrupts before direct A/B is claimed complete.
