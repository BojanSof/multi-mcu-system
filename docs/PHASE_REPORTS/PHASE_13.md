# Milestone 13 — Watchdog and bounded automatic recovery

Status: software implemented and build/native verified; hardware acceptance pending.

## Delivered behavior and frozen interface

New `reliability` profile retains historical profiles. Nordic watchdog30s feeds
only after both main and workqueue progress. Node offline does not prevent feed.
Existing response500ms/3 retries and heartbeat2s remain. Recovery rediscovery5s,
NRST20ms, discovery10s, backoffs1s/5s, rolling3-pulse/60s bound and consecutive
failure latch are covered with a deterministic clock. Intentional lifecycle modes,
UART update/repair ownership and repeated-reboot latch suppress competing reset.
No automatic BOOT0 or ROM repair. Pending work is canceled, never replayed.

SMP APIv1 command8 adds status/clear, boot identity and eight-admission replay cache;
CLI `node recovery status|clear`, mutation requires `--yes`. Status health/reason
are separate from lifecycle. Reset cause is Zephyr hardware-info flags. Native tests
cover policy timing/window boundaries, lifecycle suppression and real CBOR stale
boot, busy and clear deduplication. Nordic watchdog DT enable is profile-local.

## Remaining hardware gate

Verify enabled watchdogs actually reset on failed progress; collect reset causes.
Measure recovery against real signed-loader startup time, observe at most3 pulses
per60s and no resets in maintenance/update/repair. Tune only with recorded evidence.
Debugger halt pauses Nordic watchdog; see testing guide for feed-gate procedure.

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


## Post-HIL startup correction — 2026-10-26

User reported status with host_boot_id0, state disabled, supervision/stm_update false,
watchdog false. Inspection confirms READY/IRQ use gpio2, which has no GPIOTE instance
in the built DTS; pinned gpio_nrfx returns ENOTSUP. Optional callback setup had
incorrectly aborted main before control initialization. Now required GPIO setup
remains fatal, while optional IRQ setup logs warnings and uses UART wake/10ms polling.
Status exposes gpio_interrupts:false on this configuration. No rewiring is required.
Added actual HostSignals adapter regression for ENOTSUP, callback-allocation failure,
partial support, initial IRQ assertion and genuine pin/device failures. Updated
software/build results are recorded in build/reliability-validation/startup-fix-*.log.
User subsequently confirmed successful startup; see hardware confirmation below.

Startup-fix verification: PASS26 root CTests,2 bootloader CTests,331 Python tests;
reliability, Phase12 and default Nordic rebuilds PASS. Reliability linker occupancy
95424 FLASH /39392 RAM; unchanged STM images were not reflashed or rebuilt for
this Nordic-only correction. User startup confirmation is recorded below; extended fault tests remain pending.


## User hardware confirmation

Source: user-supplied real-board status after reflashing the startup correction.
Host boot ID3685201341 is nonzero; node online/healthy, watchdog/supervision/update/
ROM support true, gpio_interrupts false as expected. This confirms successful
startup and watchdog enable, not a deliberate watchdog-expiry/reset test.
Automatic recovery escalation and fault-latch timing remain NOT_RUN_HARDWARE.
