# Phase 4 completion report

Date: 2026-10-14
Implementation base commit: `de42d029564d29dff112dadb31e1728aefee0f6b`
Scope: diagnostics, development fault injection, correlated log collection and analyzer lessons

## Implemented

- Portable 16-record structured diagnostic FIFO with typed records, saturating counters,
  drop-oldest overflow policy and a reboot-cleared injection mask.
- Diagnostics service `0x0002` with status, enable, disable and durable record retrieval.
- Nordic discovery/client support, exact-request retry, duplicate record suppression and UART polling
  independent of the IRQ hint. A long button press cycles the development fault selection.
- STM development-only fault adapters for short IRQ, delayed/corrupt/truncated responses, dropped
  acceptance, incorrect virtual-output ordering and a bounded task stall. Release builds reject
  runtime fault enabling. Early READY uses a separate, explicit debug preset.
- STM independent logging uses a bounded interrupt-driven UART queue. Startup and heartbeat records
  identify firmware version, profile, personality, session and local uptime.
- PB3 `NODE_DEBUG` marker support without changing the frozen Phase 2 wiring.
- Bounded dual-console JSONL/CSV collector and deterministic CSV/VCD trace generator. Generated
  traces are explicitly marked `SIMULATED` and are not presented as hardware captures.
- Analyzer lessons and expected buggy/fixed behavior are documented in
  `hardware/LOGIC_ANALYZER.md` and `docs/FAULT_INJECTION.md`.

An independent review found and integration fixed two issues before this report: diagnostic
housekeeping could consume an armed UART fault, and event-created durable diagnostic records could
remain undrained. Transport faults now apply only to non-diagnostics service responses, and the
host drains the node record FIFO until an explicit empty response.

Board logs subsequently exposed repeated virtual-sensor execution when corrupt, truncated or
delayed responses caused retries. The generic node API now caches the most recent successful
service response using the full service/command/operation/data identity. Exact retries return that
response without invoking the service again; a native regression verifies this behavior.

## Dependency audit

Command: `.venv/bin/python scripts/doctor.py`

- Required compilers, CMake/Ninja, west/NCS 3.4.1, J-Link and STM32CubeProgrammer: `READY` for
  compilation.
- `cppcheck`: optional, `MISSING`; install locally/system-wide as appropriate and verify with
  `cppcheck --version`.
- `nrfutil`: executable found, but its home log directory is read-only in this sandbox. Verify in a
  normal user shell with `nrfutil --version` before flashing.
- USB/serial and libusb board access: unavailable in this execution environment. Hardware tests are
  therefore reported as `NOT_RUN_HARDWARE`, not passed.
- `sigrok-cli`: installed, but libusb analyzer access is unavailable here.

## Verification

| Check | Result |
|---|---|
| Native configure/build and CTest | `PASS`, 4/4 |
| Python tooling tests | `PASS`, 17/17 |
| clang-format dry run on changed C headers/sources | `PASS` |
| `git diff --check` | `PASS` |
| Nordic pristine NCS build | `PASS`; flash 47,572 B, RAM 12,896 B |
| STM Debug personality A | `PASS`; text 33,200 B, data 104 B, BSS 8,536 B |
| STM Debug personality B | `PASS`; text 33,200 B, data 104 B, BSS 8,536 B |
| STM early-READY profile | `PASS`; text 33,216 B, data 104 B, BSS 8,536 B |
| STM Release personality B | `PASS`; text 21,916 B, data 104 B, BSS 8,536 B |
| User-observed board logs: IDs 2, 3, 5, 6, 7 and 8 | `PASS` |
| Dropped-acceptance ID 4 | `PASS` in initial user run; one execution/completion after retry |
| Early-READY profile and analyzer captures | `NOT_RUN_HARDWARE` |

The STM link continues to report the known newlib syscall and RWX load-segment warnings. Third-party
FreeRTOS/HAL compilation also reports existing upstream warnings. No application compile error was
hidden. Nordic reports the existing deprecated `NRF_PLATFORM_LUMOS` Kconfig warning.

Automated tests prove the portable FIFO and virtual-ordering model and validate deterministic tool
output. They do not substitute for executing the STM adapters, retry behavior or IRQ fallback on
the boards; those remain the hardware acceptance gate below.

User-supplied Nordic logs verified short-IRQ event delivery, bounded delayed/corrupt/truncated
response recovery, dropped-acceptance deduplication, injected output-order reporting and bounded
task-stall recovery. After the generic replay fix, sensor indices advanced exactly once across
retries (`3 -> 4`, `19 -> 20`, `22 -> 23`). No raw log artifact was committed. Early-READY timing
and physical analyzer captures remain unverified.

## Hardware acceptance gate

Keep the already verified 3.3 V wiring and do not connect board power rails. Flash the normal
Nordic Phase 4 image and STM `debug-personality-b` image. Confirm discovery, periodic service
traffic, STM metadata/heartbeat logs, and that reset clears the injection mask. Then follow the
three mandatory tutorials in `hardware/LOGIC_ANALYZER.md`:

1. Short IRQ: UART polling still delivers/drains the event.
2. Dropped acceptance: the host retries the exact sequence/operation and the actuator executes once.
3. Early virtual output: injected output-first ordering is reported; normal mode restores
   prerequisite-first ordering.

Also exercise delayed, corrupt and truncated responses and the bounded task stall. For startup
READY timing, flash only the `debug-early-ready` STM preset, capture reset/READY/UART timing, then
restore the normal debug image. Record actual captures with hardware provenance; never relabel the
provided simulated examples.

## Status

Software implementation and available build/test validation are complete. Phase 4 overall remains
`NOT_RUN_HARDWARE` until the board checklist above is completed successfully.
