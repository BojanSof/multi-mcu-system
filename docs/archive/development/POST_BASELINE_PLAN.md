# Post-baseline implementation plan — approved 2026-10-20

Phases 1–8 remain the delivered baseline; preserve their build profiles and reports.
Use TypeSafe AI's planning principle of closed typed operations and deterministic
execution. TypeSafe is not a runtime dependency. Scope is local USB control of the
existing two boards, with reliability and observability as the main improvement goals.

## Phase 9 — USB control and CLI

Freeze SMP v2 CBOR custom group 64, API v1 in `CONTROL_API.md`. Implement an opt-in
Nordic `phase9` MCUboot/MCUmgr profile with a bounded main-thread command mailbox,
request-ID deduplication and typed response/event history. Stop periodic demonstration
operations and automatic diagnostic draining in this profile. Python argparse CLI
`scripts/mmctl` exposes devices, status, services, sensor/actuator, diagnostics,
lifecycle, events, generic service calls and standard Nordic image management.
Use pyserial 3.5 and cbor2 6.1.5, stable JSON, bounded retries and serial ownership.
Reset, fault injection, shutdown and firmware changes require `--yes`.

Parallel ownership: Nordic firmware; tools CLI/codec; validation independent review;
integrator contract/docs/dependencies/build scripts. Test malformed CBOR/SMP,
fragmentation/log interleaving, ID replay/conflict, busy/offline, event overflow,
reset/session changes and CLI fake serial. Run all native/Python and embedded builds.
HIL gate: actual runtime operations, progress/completion and Nordic upload/test/reset.

## Phase 10 — Supervised signed STM A/B updates

Status (2026-10-23): software implemented and native/firmware builds verified;
physical acceptance pending. See `PHASE_REPORTS/PHASE_10.md` and
`PHASE_10_TESTING.md`. User confirmed reset and signed update after restoring
the custom loader; extended interruption/rollback checks are not separately confirmed.

Approved GPIO candidates: Nordic P1.08 (physical header P1 pin 11) to STM CN7 pin 14
NRST; Nordic P1.14 (physical P1 pin 17) to STM CN7 pin 7 BOOT0. Verify exact-revision
schematics, LED/button loading and Nordic startup errata before enabling physical
wiring. NRST sinks low or releases high-Z; BOOT0 defaults low with STM pull-down.
Disable conflicting DK button/LED software drivers. The associated button becomes
STM reset and the LED indicates ROM selection. Do not join power rails.

Stream PC-supplied signed slot packages through Nordic to existing BootProtocol,
without whole-image staging or embedded rescue firmware. PC verifies target/slot,
signature, size and compatibility before reset; STM bootloader re-verifies signatures.
Automatically enter custom recovery, write only inactive slot, finalize, set pending,
reboot, observe confirmation and report rollback. Serialize ownership with runtime
commands. Disconnect/abort preserves confirmed firmware; uncertain reconnect queries
state before restarting. HIL: both slots, bad image/wrong slot, interruption and rollback.

## Phase 11 — Factory-ROM bootloader repair

Status (2026-10-23): software implemented and native/firmware verification passed;
user hardware normal-repair/runtime/sensor smoke passed; extended destructive
acceptance pending. See `PHASE_REPORTS/PHASE_11.md` and the
`PHASE_11_TESTING.md` manual gates.

Add portable C11 `libraries/stm32-rom-boot`, CMake target
`Stm32RomBoot::Stm32RomBoot`, native tests and Zephyr integration. AN3155 state
machine implements sync, GET/GET_ID, READ_MEMORY, WRITE_MEMORY, EXTENDED_ERASE,
ACK/NACK, checksums, bounded retries/timeouts, ID 0x489, 256-byte chunks, 2 KiB pages.
USART1 PA9/PA10 is supported by U083 ROM (AN2606 table 206); use existing wires.
Assert BOOT0, pulse NRST, switch 115200 8N1 to 8E1, synchronize/verify capabilities,
erase/program/read-verify only bootloader pages 0–31, release BOOT0, reset and restore
8N1. If apps are invalid, chain custom recovery with signed Slot 0. Never mass erase
or change option bytes/protection. `NBOOT_SEL=0`, `nBOOT1=1`, `BOOT_LOCK=0` remain
manual verification prerequisites. ROM repair is an unauthenticated rescue path.

Release manifest v2 records board/system versions, Nordic signed artifact, STM
bootloader address/bounds/SHA-256/key ID, signed slot packages and API/protocol/image
versions. Relative paths only; reject traversal, missing/hash-mismatched artifacts,
wrong hardware, wrong slot or out-of-range addresses. Keep private keys external.
Emulate malformed ACK/NACK/checksum, wrong device, bounds, odd last chunk, timeouts,
interrupted writes and readback mismatch before hardware erase. HIL restores a corrupt
bootloader while preserving confirmed apps, then recovers empty apps by signed install.

## Phase 12 — Reliability, optimization and release

Status (2026-10-25): software implemented and verified (16 root CTests,
2 bootloader CTests,242 Python tests and firmware builds). Physical100/600
acceptance and destructive matrix remain separately pending. See
`PHASE_REPORTS/PHASE_12.md`, `PHASE_12_TESTING.md` and `FINAL_RELEASE.md`.

Provide a root-invoked non-destructive demo: discovery, service calls, events,
lifecycle, faults and reset recovery. Add latency/retry/UART-error/event-drop/reset
metrics, update throughput and flash/RAM/stack reports. Benchmark before optimizing
buffering, polling, logging and update chunks. Reconnect by stable device identity.
Extend CI/HIL for runtime, both updates, ROM repair, rollback and unplug/reconnect.
Release a deterministic versioned bundle and concise clean-checkout build/demo guide.

Acceptance: ten-minute event watch and 100 sequential sensor requests with no unexplained
loss or duplicate operation; reproducible update/repair/rollback commands and evidence.
Every phase report records exact commands, Git/SDK versions, size/stack data and
PASS/FAIL/NOT_RUN_HARDWARE. No BLE, GUI, multi-node bus, runtime AI, irreversible
protection or production secure-boot claims are included.


## Approved reliability continuation — 2026-10-25

Milestones 13–16 are authorized in [RELIABILITY_PLAN.md](RELIABILITY_PLAN.md).
Their scope is dual-watchdog health, bounded automatic STM reset recovery, persistent
node faults, negotiated acknowledged events and reproducible failure acceptance.
Existing profiles and update paths remain selectable. New functionality initially
uses a `reliability` profile; hardware acceptance precedes recommending it as default.
All destructive HIL is opt-in; no option-byte/protection changes or automatic ROM repair.


Continuation status (2026-10-26): Milestones13–16 software implementation and native/
firmware verification passed (26 root CTests,2 bootloader CTests,331 Python tests).
Hardware acceptance remains separate: see PHASE_REPORTS/PHASE_13.md through PHASE_16.md
and RELIABILITY_TESTING.md. Historical Phase12 hardware results remain historical;
no new watchdog/journal/IRQ/interruption acceptance is inferred from them.
