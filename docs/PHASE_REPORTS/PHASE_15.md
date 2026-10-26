# Milestone 15 — Negotiated acknowledged events and wakeups

Status: software implemented and native/build verified; actual IRQ delivery pending.

## Delivered behavior and frozen interface

Wire-major1 stays unchanged. Capabilitybit5, ENABLE0x0006, ACK0x0007 and event0x8001
are additive. STM16-entry queue uses stop-and-wait (cumulative window1),250ms retry,
maximum5 sends before counted exhaustion; pending data and IRQ remain until ACK.
Admission failures return backpressure; actuator completion stays at its producer
until admitted, and button loss has a separate counter. Lifecycle0x000c exposes
pending/rejections/retries/exhaustion/button-loss/next-ID counters.

Nordic validates session, numbering and bounds, admits to bounded history then ACKs;
duplicates are not republished. History overwrite does not imply PC consumption.
GPIO/UART callbacks wake main only, with10ms timer/poll fallback and initial IRQ-level
check. No measured power-saving claim is made.

Independent review found and fixed three reconnect issues: HELLO now renegotiates
mode for legacy reconnect; explicit new negotiation rearms exhausted events without
changing IDs; host preserves its cursor across same-session manager rediscovery.
Native tests exercise actual STM transport and host manager, lost ACK, no callback,
TX-full ACK retry, stale session, malformed/gapped events, bounded exhaustion and
legacy downgrade preserving queued data. New lifecycle replies do not falsely
increment protocol-error counters. Node reset still discards volatile events;
this is not durable exactly-once delivery across arbitrary power loss.

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

Source: user-supplied status and quick-demo logs. Reliability capabilities63,
node online/healthy, five sensor requests and ten-second event watch PASS, including
actuator progress/completion and four asynchronous watch events. During run:
protocol errors, retries, timeouts, UART errors, RX/TX overflows all0. Lifetime
reliable_gaps, reliable_duplicates and event_ack_retries all0. Host history dropped6
means overwritten retained records, not demonstrated wire loss. UART/poll fallback
works with gpio_interrupts:false. Deliberate lost ACK/exhaustion/legacy-reconnect
hardware exercises remain NOT_RUN_HARDWARE.
