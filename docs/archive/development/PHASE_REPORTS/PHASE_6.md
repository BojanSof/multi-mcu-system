# Phase 6 completion report

**Status:** SOFTWARE_COMPLETE / HARDWARE_PARTIAL
**Date:** 2026-10-17
**Base Git revision:** `b01d744` (Phase 6 changes uncommitted)

## Delivered

- Portable C11 `FirmwareImage::FirmwareImage` package with a frozen 80-byte little-endian `MMFW`
  manifest, strict bounds/header parsing, compatibility policy, and constant-time digest compare.
- Deterministic standard-library Python pack, inspect, verify, and release-manifest tools. Known
  target/hardware/load-address combinations and target-region size ceilings are enforced. This is
  SHA-256 integrity only; Phase 6 does not claim MMFW authenticity.
- Opt-in nRF54L15 MCUboot/sysbuild profile with serial MCUmgr image and OS groups, equal internal
  RRAM slots, signed update artifacts, application self-confirmation after compatible node
  discovery, and a separately selectable deliberately-unconfirmed rollback demonstration.
- READY/IRQ reassigned to exposed P2.06/P2.08, restoring debugger UART RTS/CTS on P1.06/P1.07 for
  bidirectional J-Link VCOM/MCUmgr operation.
- STM32 application version/build identity advanced to `0.6.0` / `0x00060001`.
- Safe STM32 recovery guide covering ST-LINK connect-under-reset and manual factory-ROM USART2
  recovery with a verified reversible `NBOOT_SEL` development setting and no automated BOOT0/NRST
  control.
- Dependency lock and doctor coverage for `nrfutil-mcu-manager 0.12.0`.

## Dependency audit

- Required native, Arm GNU, STM32CubeCLT 1.22.0/CubeProgrammer 2.23.0, NCS 3.4.1, west 1.5.0,
  J-Link, and nrfutil MCU Manager 0.12.0 tools are available.
- `cppcheck` is optional and missing (`BLOCKED_TOOL_OPTIONAL`).
- Board actions were performed by the user; their reported results are recorded as
  `PASS_HARDWARE`. Tests without an explicit result remain `NOT_RUN_HARDWARE`.

## Verification

| Check | Result | Evidence |
|---|---|---|
| Native configure/build/CTest | PASS | 5/5 tests, including firmware-image malformed and compatibility cases |
| Python tools | PASS | 32/32 pytest cases; deterministic output, malformed/hash/profile/overflow rejection |
| Nordic normal profile | PASS | FLASH 50,000 B; RAM 12,984 B |
| Nordic Phase 6 normal OTA profile | PASS | app FLASH 77,600 B; RAM 20,784 B |
| Nordic MCUboot | PASS | FLASH 62,412 B / 68 KiB (89.63%); RAM 27,984 B |
| Nordic rollback-demo profile | PASS | app FLASH 77,600 B; RAM 20,784 B |
| MCUboot/app partition agreement | PASS | both DTS: `0x0/0x11000`, `0x11000/0xae000`, `0xbf000/0xae000`, `0x16d000/0x10000` |
| Signed-image validation | PASS | MCUboot imgtool accepted `0.6.0+0`; one-byte-corrupt copy rejected with invalid hash |
| STM32 personality B build | PASS | text 35,832 B; data 104 B; bss 9,440 B |
| MMFW package/release-manifest smoke test | PASS | 35,936-byte STM payload packed, verified, inspected, and emitted deterministically |
| clang-format / Python compile / diff whitespace | PASS | no project-source formatting or syntax errors |
| Nordic serial upload/test/confirm | PASS_HARDWARE | User verified distinct signed `0.6.0` to `0.6.1` MCUmgr upload, MCUboot trial boot, node-online self-test, and confirmation over the DK VCOM |
| Nordic automatic revert | NOT_RUN_HARDWARE | unconfirmed trial profile still requires a reset/revert observation |
| Interrupted/corrupt OTA recovery | PASS_HARDWARE | User verified an interrupted upload preserved the confirmed image and a payload-corrupted `0.6.2` candidate was rejected without replacing confirmed `0.6.1` |
| STM factory-ROM USART recovery | PASS_HARDWARE | User verified BOOT0-selected ROM USART identification and firmware programming after the documented reversible `NBOOT_SEL` change |
| STM SWD recovery | NOT_RUN_HARDWARE | build only in this phase execution; command documented |

The STM build retains known third-party FreeRTOS/HAL conversion/unused-variable warnings and
newlib syscall/RWX-linker warnings from earlier phases; no new project-source warning was observed.

## Security and safety limits

- The Nordic build uses the public NCS default development Ed25519 key. It demonstrates MCUboot
  mechanics only and is not production secure boot.
- MMFW packages are unsigned in Phase 6. Authentic signed STM slot images begin in Phase 7.
- The factory-ROM test changed only reversible `NBOOT_SEL` to `0`; this is retained as the
  development configuration so the physical BOOT0 recovery jumper remains usable. RDP,
  `BOOT_LOCK`, write protection, `nBOOT0`, and `nBOOT1` were not changed. Normal operation requires
  the BOOT0 jumper to be removed.
- Hardware testing established a Nordic-first power sequence. Powering STM first can back-power the
  unpowered Nordic GPIO domain through connected push-pull signals and prevent a clean Nordic boot;
  recovery is a complete USB power removal followed by Nordic-first startup.
- Phase 6 implementation is complete. Explicit automatic-revert and SWD-recovery observations
  remain open in the report; the normal OTA, interrupted/corrupt OTA, and factory-ROM paths passed
  on hardware.
