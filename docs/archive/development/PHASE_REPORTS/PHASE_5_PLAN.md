# Phase 5 plan and frozen interfaces

## Scope

Phase 5 adds supervision, meaningful-progress watchdog policy, reset-cause reporting, lifecycle
control and service-version compatibility. It does not add firmware update or bootloader behavior.
Remote NRST driving remains electrically blocked until its exact pins, open-drain release behavior
and wiring are approved; the host escalation state machine must work with that adapter disabled.

## Frozen contracts

- Lifecycle service ID is `0x0003`, version `1.0`. Commands are `GET_STATUS=1`,
  `ENTER_MAINTENANCE=2`, `EXIT_MAINTENANCE=3`, `GET_VERSIONS=4`,
  `ENTER_SHUTDOWN=5`, and `EXIT_SHUTDOWN=6`.
- Lifecycle states are `INIT=0`, `READY=1`, `ONLINE=2`, `MAINTENANCE=3`, `FAULT_LATCHED=4`.
  `SHUTDOWN=5` is a software-only quiescent state and never claims physical power removal.
- Reset reasons are `UNKNOWN=0`, `POWER_ON=1`, `PIN=2`, `SOFTWARE=3`, `WATCHDOG=4`,
  `BROWNOUT=5`.
- Status data is exactly 12 bytes: `state:u8, reset_reason:u8, raw_reset_flags:u16,
  boot_count:u32, progress_count:u32`, little-endian.
- Version data is exactly 16 bytes: firmware major/minor/patch as `u16`, wire major/minor as `u8`,
  management major/minor as `u16`, and a non-secret 32-bit build identifier.
- Service compatibility requires equal major versions and an offered minor version greater than or
  equal to the required minor. An incompatible service is unavailable, while other compatible
  services continue operating.
- STM watchdog refresh depends on observed application progress. The development stall injection
  stops that progress and may cause a watchdog reset; unconditional refresh from the stalled task
  is forbidden.
- READY drops during initialization and maintenance transitions. Maintenance and software shutdown
  reject new virtual-I/O work without repeating or fabricating operations, while lifecycle,
  diagnostics and heartbeat remain available. Exit creates a clean online negotiation path.
- A changed/nonzero node session invalidates host pending work and replay assumptions. READY
  loss/re-rise also counts as a reboot and forces rediscovery even if volatile session storage was
  lost and the numeric session repeats. Host reset never automatically replays an unconfirmed
  application operation.
- Host supervision distinguishes transient request timeout, READY/link loss, changed node session,
  and repeated reboot. Three node resets inside 30 seconds latch a host fault until a deliberate
  local restart. Physical remote reset is disabled by default and cannot be used for transient UART
  errors.

## Parallel ownership

1. Integrator: this contract, common IDs/types, cross-component review, builds and report.
2. Compatibility agent: `libraries/node-api/` implementation and native version/replay tests.
3. STM agent: `firmware/stm32u083/application/` and STM platform watchdog/reset adapters.
4. Nordic agent: `firmware/nrf54l15-host/` supervision, compatibility filtering and lifecycle client.
5. Validation agent after integration: independent tests/review and HIL procedure.

## Exit verification

- Native compatibility, replay, lifecycle-model and malformed/bounds regressions.
- Nordic pristine build and STM Debug/Release builds with size reports.
- HIL instructions for independent host/node reset, watchdog stall recovery, maintenance, session
  resynchronization, incompatible optional service and repeated-reset fault latching.
