# Milestone 14 — Persistent node fault journal

Status: software implemented and native/build verified; hardware persistence pending.

## Delivered behavior and frozen interface

Two2KiB pages at0x0803D000/0x0803D800 come from the previously reserved region.
Application slots and two boot-journal pages retain their original bounds. YAML
schema generates and validates journal addresses. Standalone whole-flash reliability
configuration is rejected before writing anything.

Portable DiagnosticsJournal uses64-byte records, CRC, monotonic generation and
commit-last doubleword. Rotation preserves newest committed data; clear commits a
logical tombstone, not secure physical erasure. Tests cover every doubleword boundary,
257 partial-erase boundaries, corrupt bytes, rollover and clear aging through rotations.

Task-owned STM adapter writes boot summaries and coalesced important faults. At most
8 fault writes per boot, one-second warning spacing, fatal pre-stall breadcrumb;
no ISR/fatal-handler writes and no automatic storage-failure retry loop. Trial
confirmation is not blocked on journal storage. Nordic reset cause is exposed without
adding a second persistent journal.

Lifecycle1.1 adds0x000a read cursor,0x000b clear, preserving old commands and legacy1.0
profiles. CLI `node faults list|clear` handles48-byte records/60-byte responses with
session/cursor checks. `overwritten` is missing physical generations, not unread-PC
loss; storage_errors is perboot observed I/O count. Read does not consume records.
Native actual lifecycle-dispatch test checks lengths, read cursor, clear deduplication
and legacy status behavior. Real flash retention/ECC behavior still requires boards.

## Verification environment and provenance

Date: 2026-10-26. Base Git revision: `4bd719d`; results refer to the uncommitted
Milestones13–16 working tree, not a new release commit. Linux x86_64, Python3.14.8,
CMake/CTest4.3.1, Ninja1.13.2, native GCC16.2.1, STM Arm GNU15.2.1;
NCS3.4.1 / Zephyr4.4.2 with Zephyr SDK1.0.1 GCC14.3.0. Existing pinned dependencies
and project `.venv` reused. No packages installed globally, no board flash executed.

Doctor: required build/flash tools READY; optional cppcheck missing (manual install:
`pacman -S --needed cppcheck`). USB serial devices unavailable and libusb enumeration
inaccessible. To verify boards, use the documented voltage/wiring and Nordic-first
power order, connect USB, run `lsusb`, `ls -l /dev/serial/by-id/` and
`.venv/bin/python scripts/doctor.py`. Never join power rails.

## Common reproducible checks

Run from project root:

```sh
source ./.env.local
scripts/test/run_native.sh
scripts/build/build_nordic.sh --profile reliability
scripts/build/build_nordic.sh --profile phase12
scripts/build/build_nordic.sh --profile default
scripts/build/build_stm32.sh --profile reliability
scripts/build/build_stm32.sh --profile all --public-key-header keys/stm32-dev-public.h
```

The public-key header above is the existing ignored local development header;
use the public header matching your own installed bootloader. Never replace its key
just to reproduce a build. No private key is needed for these compile checks.
Additional checked presets: debug-reliability-slot0/1 and debug-trial-failure-slot0/1,
configured with `cmake --preset NAME -S firmware/stm32u083/application`, built with
`cmake --build firmware/stm32u083/application/build/NAME --parallel 4`.

| Check | Result |
|---|---|
| Root native CTests | PASS,26/26 |
| Separate bootloader CTests | PASS,2/2 |
| Python tooling/HIL-runner unit tests | PASS,331/331 (simulated peers, not HIL) |
| Python dependency consistency | PASS |
| Nordic reliability + MCUboot, Phase12, default builds | PASS |
| STM reliability release/debug both slots, legacy release both slots and standalone | PASS |
| STM deliberate failing-trial variants both slots, bootloader build | PASS |
| Unsafe release trial / whole-flash journal configurations rejected | PASS |
| Hardware watchdog, IRQ, persistence and update tests | NOT_RUN_HARDWARE |

Static resource evidence is `build/reliability-resources.json`; command:

```sh
.venv/bin/python tools/firmware/resource_report.py \
  --elf reliability=build/nrf54l15-host-reliability/nrf54l15-host/zephyr/zephyr.elf \
  --elf phase12=build/nrf54l15-host-phase12/nrf54l15-host/zephyr/zephyr.elf \
  --elf stm-reliability=firmware/stm32u083/application/build/release-reliability-slot0/multi_mcu_stm32_application.elf \
  --size-tool arm-none-eabi-size --output build/reliability-resources.json
```

| Image | ELF text | Data | BSS | Budget result |
|---|---:|---:|---:|---|
| Nordic reliability | 94016 | 1400 | 37994 | PASS,95416 bytes text+data;39394 static RAM |
| STM reliability Slot0/1 | 28444 | 104 | 10904 | PASS,28548 flash;11008 static RAM |
| STM debug reliability Slot0/1 | 43136 | 104 | 10904 | PASS |
| STM failing-trial Slot0/1 | 43120 | 104 | 10904 | PASS |
| STM legacy Slot0/1 | 23472 | 104 | 9440 | PASS |
| STM legacy standalone | 23312 | 104 | 9440 | PASS |
| STM bootloader | 49544 | 116 | 2104 | PASS,64KiB flash budget |

Nordic linker region occupancy includes alignment (95424 FLASH /39392 RAM).
MCUboot linker occupancy62412 FLASH /27984 RAM fits its68KiB region. STM slots
remain88KiB with512-byte signed header; device RAM40KiB. Static sizes do not prove
stack high-water bounds: runtime main/work/STM task measurements are NOT_RUN_HARDWARE.
Known vendor warnings remain: NCS deprecated NRF_PLATFORM_LUMOS; STM/newlib syscall
stubs and RWX load segment. No project compiler error was ignored.

Local ignored evidence: `build/reliability-validation/{tests,nordic,stm32}.log`.
No analyzer capture was made. Full hardware commands and criteria are in
[RELIABILITY_TESTING.md](../../../RELIABILITY_TESTING.md).


## User hardware confirmation

Source: user-supplied real-board journal output before and after explicit node reset.
Journal healthy:true, overwritten:0, storage_errors:0. Generations1 and2 survived;
generation3 records boot_count4/reset_reason2 with new session2027218649, replacing
previous session3668929806. Warm-reset retention PASS. Power-cycle retention,
flash interruption and fault-storm wear tests remain NOT_RUN_HARDWARE.
