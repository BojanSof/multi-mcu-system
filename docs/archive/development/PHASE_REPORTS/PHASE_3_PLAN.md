# Phase 3 plan and frozen interfaces

## Scope

Phase 3 adds a portable, bounded node-service registry and a removable deterministic virtual-I/O
example. The STM32 publishes service events and the Nordic invokes and displays them without
requiring a personality-specific host image.

## Frozen contracts

- Phase 2 framing, management IDs and request/response prefixes remain unchanged.
- `LIST_SERVICES` (`0x0005`) returns bounded, versioned service descriptors.
- `SERVICE_CALL` (`0x0100`) carries `service_id:u16`, `command_id:u16`, `operation_id:u32`, then
  bounded command data. Its response is command acceptance/result, not asynchronous completion.
- `SERVICE_EVENT` (`0x8000`) is an unsolicited protocol event with sequence zero. It carries
  `service_id:u16`, `event_id:u16`, `operation_id:u32`, then bounded event data.
- GPIO `BUTTON_CHANGED` event ID `1` carries `pressed:u8`; its operation ID is a monotonic event
  counter.
- Service and command schemas are portable `node-api` declarations. Virtual sensor/actuator
  schemas remain under `examples/virtual-io`.
- Side-effecting virtual actuator starts are idempotent by operation ID. A retry returns the
  original acceptance and never starts a second operation.
- Registry, payload, service and event capacities are compile-time bounded. No heap allocation is
  permitted in portable or MCU control paths.
- The deterministic simulation receives time explicitly in milliseconds and uses an explicit
  seed. It does not represent physical voltage, current, power or motion.
- Personality A exposes GPIO events and the virtual sensor. Personality B additionally exposes
  the virtual actuator. Both are discoverable by the same Nordic firmware.

## Work split

1. Integrator: freeze headers/wire IDs, root build, integration, final builds and report.
2. Node/virtual-I/O agent: portable registry, deterministic example and native unit tests.
3. STM32 agent: service adapter, personality selection, button/event publication.
4. Nordic agent: generic service discovery/client, button invocation and event logging.
5. Integrator/validator: independent review, malformed/replay simulation and all builds.

## Verification

- Native registry and deterministic simulation tests, including unsupported service/command,
  duplicate START and progress/completion ordering.
- Existing protocol malformed/partial-stream tests remain green.
- Pristine Nordic build and STM32 cross-build with size reports and compile database.
- Hardware procedure verifies both personalities with sensor samples, button events, actuator
  progress/completion and a single execution for a duplicated START.

