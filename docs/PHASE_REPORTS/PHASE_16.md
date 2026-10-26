# Milestone 16 — Reproducible failure evidence and guarded acceptance

Status: acceptance tooling/documentation implemented and unit/build verified;
hardware acceptance intentionally incomplete.

## Delivered tooling and contracts

Root `scripts/test/reliability.sh` names startup, compatibility, watchdog, recovery,
persistence and events suites. It executes real CLI checks only after explicit
hardware/electrical/port selection. Missing manual evidence stays NOT_RUN_HARDWARE,
not PASS. Existing evidence files are never overwritten.

Destructive interruption requires explicit --destructive/--yes, validated full release
bundle/public key, verified rescue declaration and complete ST-LINK serial match.
It only erases inactive slot under server guards, transfers one256-byte chunk then
aborts before finalize/activation. Unit tests prove it never sends finish. The
trial-rollback runner checkpoint remains guided; new Debug-only failing-trial slot
presets provide an actual reproducible non-confirming candidate. Release builds
reject that option. Native boot-confirm tests verify normal and deliberately failed
paths. Software reset/abort is never labeled electrical power-loss proof.

Testing guide includes root build/sign/flash commands, legacy/new compatibility
matrix, lost-acceptance and watchdog-stall walkthroughs, manual power/commit boundary
checks and optional real-analyzer capture manifest. CI native jobs exercise safe
no-op; manual embedded CI builds reliability and stores static resource evidence.

## Hardware acceptance still required

Run the documented100-request/600-second demo, both watchdog resets, bounded automatic
recovery/exclusion, journal retention through power cycle, IRQ/lost-ACK behavior,
interrupted updates and signed trial rollback. Actual analyzer capture is optional;
none exists for this extension. No hardware commands were executed in this session.

```sh
scripts/test/reliability.sh
scripts/test/reliability.sh --run-hardware --electrical-verified --port "$NRF_PORT" \
  --report build/reliability/acceptance-001.json
scripts/demo.sh --port "$NRF_PORT" --require-acceptance \
  --report build/reliability/runtime-001.json
```

Guard arguments reflect the user's verified development setup, not firmware voltage
measurement or proof of protection. No new wires, option-byte/protection changes,
physical power switching or automatic ROM repair were introduced.

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
[RELIABILITY_TESTING.md](../RELIABILITY_TESTING.md).


## User hardware confirmation

Source: user-supplied real-board status, five-sample/ten-second demo and reset journal.
Normal signed reliability update was reported working after protected-slot refusal
was resolved by choosing the inactive slot. Startup, normal service/event demo and
warm-reset journal retention PASS. Demo reports acceptance_complete:false, as
expected for a short run. Full100-sample/600-second acceptance, physical watchdog
expiry, automatic recovery failure escalation, power-cycle persistence, interrupted
update and signed failing-trial rollback remain NOT_RUN_HARDWARE.
Measured quick-demo PC latency p50=85.93ms, max=86.55ms; main stack free2268B and
work stack free3864B are observed snapshots, not guaranteed worst-case bounds.
