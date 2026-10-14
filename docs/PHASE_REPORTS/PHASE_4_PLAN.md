# Phase 4 plan and frozen interfaces

## Scope

Phase 4 adds bounded structured diagnostics, development-only deterministic fault injection,
correlated dual-console log collection, simulated trace export, and three analyzer tutorials. It
does not add firmware recovery policy or watchdog reset escalation, which remain Phase 5 work.

## Frozen contracts

- Diagnostic records contain source, severity, domain, code, local monotonic milliseconds,
  correlation/operation ID, and a 32-bit detail value. A fixed 16-record ring drops the oldest
  record when full and increments a saturating overflow counter.
- Injection IDs are stable across native tests, both firmware applications, Python tooling, and
  documentation. Runtime masks are cleared at reboot and exist only when the development fault
  option is compiled in.
- IDs: `EARLY_READY=1`, `SHORT_IRQ=2`, `DELAY_RESPONSE=3`, `DROP_ACCEPTANCE=4`,
  `CORRUPT_UART=5`, `TRUNCATE_UART=6`, `EARLY_VIRTUAL_OUTPUT=7`, `TASK_STALL=8`.
- The generic diagnostics service is `0x0002`. Its bounded commands are `GET_STATUS=1`,
  `ENABLE_INJECTION=2`, `DISABLE_ALL=3`, `GET_RECORD=4`; its record event is `1`.
- Diagnostic service payloads use explicit little-endian fields and never cast structs onto wire
  storage. Injection commands carry `injection_id:u8`; status returns `enabled_mask:u32` plus
  counters. Access is development-only.
- A record encodes to exactly 17 bytes: `timestamp_ms:u32, correlation_id:u32, detail:u32,
  code:u16, source:u8, severity:u8, domain:u8`. `GET_STATUS`, successful `ENABLE_INJECTION`, and
  `DISABLE_ALL` return `enabled_mask:u32, total_recorded:u32, overflow_count:u32,
  queued_count:u8`. `GET_RECORD` returns `available:u8` and an optional encoded record; record
  events carry one encoded record.
- `DROP_ACCEPTANCE` drops exactly one successful actuator START response after execution. Host
  retry reuses the exact sequence and operation ID; STM duplicate handling returns the cached
  acceptance without a second execution.
- Correct IRQ behavior is level-held while event work is pending, with host UART polling as a
  fallback. `SHORT_IRQ` is an intentionally buggy demonstration mode only.
- The virtual-output lesson is software-only. Correct mode enforces a modeled prerequisite before
  enable; `EARLY_VIRTUAL_OUTPUT` deliberately violates ordering for deterministic reproduction.
- Generated CSV/VCD traces are labeled `SIMULATED`; hardware captures require explicit provenance.
- Existing Phase 2 pin assignments remain unchanged. `NODE_DEBUG` PB3 is an optional marker and
  is never described as a voltage sensor.

## Parallel ownership

1. Integrator: this contract, common headers/IDs, root integration, final builds and report.
2. Diagnostics agent: `libraries/diagnostics/` and portable native tests.
3. STM agent: STM application fault adapter and example integration, no common-header edits.
4. Nordic agent: Nordic fault controller/counters and retry/poll behavior, no common-header edits.
5. Tools/validation agent after integration: `tools/log-collector/`, `tools/fault-runner/`,
   simulated traces, independent tests and tutorial review.

## Exit verification

- Native queue/malformed/bounds tests and deterministic buggy-versus-fixed scenario regressions.
- Python tests for bounded-duration runners, correlation and clearly labeled simulated traces.
- Pristine Nordic build and both STM personality builds with size reports.
- User HIL steps for enable/disable, lost IRQ, dropped acceptance/deduplication, output ordering,
  corrupt/truncated/delayed UART, and analyzer CH0/CH1/CH2 evidence.
