# Phase 7 report — custom STM32 signed direct A/B

Date: 2026-10-18
Base Git revision: `64ed60a105b1ddecbbf09404541c6df3faccaa24`
Status: `HIL_IN_PROGRESS`; signed Slot 0/Slot 1 delivery and persistent confirmation pass on hardware.

## Dependency audit

`python3 scripts/doctor.py` found all required build/signing tools: CMake 4.3.1, Ninja 1.13.2,
Arm GNU 15.2.1, NCS 3.4.1/Zephyr 4.4.2, west 1.5.0, STM32CubeProgrammer 2.23.0,
J-Link 9.24a, and project-local cryptography 49.0.0. Optional cppcheck is missing. This execution
environment exposed no USB serial devices and libusb returned `-99`, so hardware operations were
performed by the user from the documented commands and are identified separately below.

## Frozen implementation

- Generated 64/88/88/16 KiB layout: bootloader, Slot 0, Slot 1, metadata/reserved.
- Slot vectors at `0x08010200` and `0x08026200`; two independent application link variants.
- Signed MMFW v2: 512-byte header, SHA-256 payload digest, low-S raw ECDSA-P256 signature,
  key/slot/link address included in canonical bytes, external private key only.
- Bare-metal bootloader with strict vector bounds, signature checks, inactive-slot writes,
  two-page transactional metadata, pending/trial/confirmed/rollback/recovery, and BKP application
  confirmation handshake.
- Boot update protocol with bounded frames/chunks, exact sequencing, response replay after lost
  acknowledgements, arbitrary logical final length, UART resynchronization, and a two-second
  maintenance-entry window.
- Opt-in Nordic client with 250 ms discovery retries, 5 s update timeout, exact-sequence replay, and
  optional embedded signed STM image. Existing Nordic and STM ROM-development profiles remain.

This demonstrates image authentication only. SWD and device protections remain open, no production
key provisioning or anti-rollback policy exists, and this is not production secure boot.

## Verification

| Check | Result | Evidence |
|---|---|---|
| Root portable C tests | PASS | `ctest --preset native-debug --output-on-failure`: 6/6 |
| Python tools/tests | PASS | `.venv/bin/python -m pytest tools/tests -q`: 44/44 |
| Boot journal/update engine | PASS | bootloader native CTest: 2/2 |
| Signed malformed/tamper vectors | PASS | high-S, hash, signature, slot/address and padding rejection |
| Metadata power-loss model | PASS | reset at normal append and page-rollover erase/write boundaries |
| Lost-ACK/replay model | PASS | duplicate WRITE/SET_PENDING do not repeat mutation; delayed replay rejected |
| STM application Slot 0 | PASS_BUILD | vector `0x08010200`; 23,456 text + 104 data, 9,440 bss |
| STM application Slot 1 | PASS_BUILD | vector `0x08026200`; same size |
| STM bootloader | PASS_BUILD | 49,500 text + 116 data = 49,616/65,536 flash; 2,220 data+bss RAM |
| Nordic default host | PASS_BUILD | 50,008 flash, 12,984 RAM |
| Nordic Phase 7 query client | PASS_BUILD | 43,136 flash, 12,216 RAM |
| Nordic embedded-image client | PASS_BUILD | 67,992 flash, 12,216 RAM |
| Formatting/diff whitespace | PASS | clang-format on changed C and `git diff --check` |
| Signed Slot 0 delivery/boot | PASS_HARDWARE_USER | Nordic delivery completed and the application booted |
| Signed Slot 1 delivery/boot | PASS_HARDWARE_USER | Nordic delivery completed; UART heartbeat at approximately two-second intervals confirms working SysTick/RTOS timing |
| Button interrupt from both linked slots | NOT_RUN_HARDWARE | not separately observed for both Phase 7 slot images |
| Signed pending image confirms | PASS_HARDWARE_USER | Slot 1 reported `fw 0.6.0`, `flags 0x000a`, `boot 5`; a subsequent manual reset returned to the same working image without another confirmation reset |
| Trial crash rolls back | NOT_RUN_HARDWARE | requires board reset observation |
| Interrupted erase/program recovery | NOT_RUN_HARDWARE | destructive test must be manually bounded |
| Factory ROM/SWD rescue retained | NOT_RUN_HARDWARE | Phase 6 route was user-verified; recheck after bootloader flash |

Representative commands are documented in `docs/BOOTLOADER.md`. Target builds used temporary
external development keys under `/tmp`; no private key or generated firmware image is tracked.

The initial hardware attempt failed with host protocol timeout `-116`. Hardware testing exposed a
missing bootloader `SysTick_Handler` and a recovery-entry race. Adding the handler, checking the
recovery window before signed-slot validation, and retrying QUERY every 250 ms allowed Slot 0 and
then Slot 1 delivery to complete.

## Outstanding validation and observations

- Measure bootloader crypto stack high-water usage on hardware.
- The generated STM linker emits an RWX load-segment warning; HAL/newlib also emit upstream
  conversion/unused-syscall warnings. Project sources build without compiler warnings.
- Phase 7 cannot be marked hardware-complete until rollback, corrupt-image rejection, bounded reset
  interruption, and ROM/SWD rescue are observed on the NUCLEO. Slot execution and persistent trial
  confirmation have been observed successfully.
