# Phase 10 — Remote reset and streamed signed STM32 updates

Date: 2026-10-23. Base Git: `1f8436d1b7922b0c067474ff1c2a52a5135a41d5`
plus uncommitted Phase 10 implementation. Status: software implemented and verified;
hardware acceptance `NOT_RUN_HARDWARE`.

User hardware follow-up: remote reset was confirmed in STM logs. Initial query
timeout was traced to the standalone application occupying address 0x08000000
(vectors matched the standalone ELF), despite signed images remaining in both slots.
After restoring the custom bootloader and choosing the inactive slot, the user
confirmed the signed update works. Both-direction updates, interruption and rollback
are not separately confirmed and remain pending; no agent performed hardware flashing.

## Delivered

- Frozen plan/API before parallel work: integrator owns shared headers/docs/build;
  independent agents own update core, Nordic adapters and CLI/tests.
- Separate `phase10` MCUboot/MCUmgr profile. Phase 9/default/Phase 7 remain selectable.
  Wrapper checks child cache and linked control, adapter and update objects.
- Main-thread-owned bounded BootProtocol updater; no complete-image staging.
  ACKed offsets, exact sequence/slot checks, resynchronization, bounded retries,
  inactive-slot/journal protection and 30-second precommit inactivity abort.
- PC validates signed MMFW with trusted P-256 key before opening serial; STM
  independently verifies before commit. Header-only portable parser avoids pretending
  a 512-byte buffer contains a complete package.
- Commit uncertainty is observed, not replayed: lost/malformed/error pending reply
  still leads to trial boot and journal verification. Confirmed target or rollback
  is checked, followed by runtime rediscovery before terminal outcome.
- SMP reset/update mailbox, exact request deduplication and boot/job guards;
  synchronous admission snapshot, serialized runtime/update UART ownership.
- `mmctl node reset`, `stm status`, `stm image inspect`, `stm update`, `stm abort`.
  No automatic mutating reconnect. ROM recovery intentionally remains Phase 11.
- Source-verified P1.08 open-drain NRST and P1.14 BOOT0-low supervision overlay;
  conflicting button/LED consumers disabled. No option-byte or flash-layout changes.

## Environment and commands

Arch Linux x86_64; GCC native 16.2.1; Arm GNU 15.2.Rel1; CMake 4.3.1;
Ninja 1.13.2; Python 3.14.8/project `.venv`; west 1.5.0; NCS 3.4.1;
Zephyr 4.4.2; Zephyr SDK 1.0.1; CubeProgrammer 2.23.0; nrfutil 8.2.0;
J-Link 9.24a. Audit: required tools available, optional cppcheck missing.
No ttyACM/ttyUSB; USB enumeration fails libusb -99. No flashing was performed.

Root commands actually used:

```sh
source ./.env.local
python3 scripts/doctor.py
scripts/test/run_native.sh
scripts/build/build_stm32.sh --public-key-header keys/stm32-dev-public.h
scripts/build/build_nordic.sh --profile default
scripts/build/build_nordic.sh --profile phase9
scripts/build/build_nordic.sh --profile phase10
scripts/build/build_nordic.sh --profile phase7-query
cmake -S firmware/nrf54l15-host/tests/update -B build/phase10-update-sanitized -G Ninja \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer -g'
cmake --build build/phase10-update-sanitized
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/phase10-update-sanitized --output-on-failure
git diff --check
```

| Verification | Result |
|---|---|
| Native libraries, host state, real SDK CBOR, updater/STM engine model | PASS: 10 CTests |
| STM boot state-machine and real crypto tests | PASS: 2 CTests |
| Python tools, CLI, wrapper and HIL-runner unit tests | PASS: 108 tests |
| Python dependency consistency | PASS (`pip check`) |
| Update-core ASan/UBSan | PASS with leak detection disabled |
| STM normal, Slot 0, Slot 1, bootloader builds | PASS |
| Nordic default and Phase 9 regression builds | PASS |
| Nordic preserved Phase 7 query profile | PASS: 43184 flash / 12216 RAM bytes |
| Nordic Phase 10 final clean build and link checks | PASS |
| Real reset, both slot updates, interruption, rollback, physical levels | NOT_RUN_HARDWARE |

The sanitizer's default LeakSanitizer execution failed because the sandbox uses
ptrace; rerun explicitly disabled leak checking while retaining address/undefined
checks. No heap is used in the updater. An initial Nordic compile exposed CBOR
header ordering and was fixed; a subsequent overlapping build had an archive race,
so acceptance uses a fresh, non-overlapping wrapper build, not that failed run.

Native coverage includes fragmented/corrupt/malformed replies, dropped/duplicate
ACKs, exact chunk replay, header mismatch, wrong/confirmed/pending slot protection,
signature failure, inactivity abort, reset-only, confirmed target/rollback outcomes,
commit ACK loss/malformed ACK and actual BootEngine duplicate-write defense. Adapter
tests use pinned SDK zcbor; CLI tests verify offline preflight and no cross-boot replay.

## Memory and stack

STM GNU `size` (bytes):

| Target | text | data | bss |
|---|---:|---:|---:|
| Normal release | 23280 | 104 | 9440 |
| Slot 0 | 23456 | 104 | 9440 |
| Slot 1 | 23456 | 104 | 9440 |
| Bootloader | 49544 | 116 | 2104 |

Both linked applications fit the existing 88 KiB slots; loader fits 64 KiB;
STM remains within 40 KiB RAM. Existing GNU newlib syscall-stub and bootloader
RWX LOAD-segment warnings remain; these are not new Phase 10 build failures.

Nordic default: 50264 flash / 12992 RAM bytes. Phase 9 app: 78656 / 30272.
MCUboot: 62412 / 27984 (68 KiB loader region). Phase 10 app: 84692 / 38240;
increment over Phase 9 is 6036 flash / 7968 RAM bytes, including larger main stack
and bounded update/replay buffers.

Phase 10 enables compiler `-fstack-usage` for application C sources. Main/workqueue
stacks are each 4096 bytes; ISR stack 2048. Static individual frames include
HostUpdate_Write 936, HostUpdateAdapter_Process 568, HostStmUpdate_Send 304,
HostStmUpdate_Drain 312 bytes. These are individual compiler frames, **not** whole
call-chain bounds or measured watermarks. Runtime stack high-water testing remains
NOT_RUN_HARDWARE. Fixed core/mailbox/replay buffers reside in static RAM.

## Hardware acceptance and limitations

Exact wiring/default-level and root-invoked build/flash/update instructions:
[PHASE_10_TESTING.md](../PHASE_10_TESTING.md), [PINOUT.md](../../hardware/PINOUT.md),
[WIRING.md](../../hardware/WIRING.md). Nordic PCA10156 1.0.0 schematic sheets 5/7
were inspected; ST UM3256 Rev 2 table 11 confirms NRST/BOOT0 and default BOOT0 low.
The large ST schematic PDF could not be fetched in this environment; no new claim
about its resistor values is relied upon. Wiring still requires human inspection
and physical level verification before issuing mutations.

Reuse installed matching signing keys and established signed A/B STM loader.
Do not overwrite it with the standalone application. Software tests do not prove
physical slot switching, UART recovery timing, reset electrical behavior or rollback.
No production secure-boot/anti-rollback claim: SWD/protections remain open.

## Final verification

Final clean `scripts/build/build_nordic.sh --profile phase10`: PASS. The wrapper
verified both image-scoped options in the child cache and HostControl,
HostUpdateAdapter and HostStmUpdate objects in the final map. Final canonical
suite: 10 root CTests, 2 bootloader CTests, 108 Python tests, dependency consistency
PASS. Formatting and `git diff --check` PASS. No hardware was accessed or flashed.
