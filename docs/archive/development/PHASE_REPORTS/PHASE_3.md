# Phase 3 report — generic services and virtual I/O

Date: 2026-10-13  
Base Git revision: `424c994df0023b975d5f7b4fcdcfffcbc670aa93`  
Working tree: Phase 3 changes are committed after final validation.

## Delivered contracts and behavior

- Bounded portable `node-api` registry with eight services maximum, versioned descriptors,
  duplicate registration rejection and explicit unsupported/capacity statuses.
- Generic `LIST_SERVICES`, `SERVICE_CALL` and sequence-zero `SERVICE_EVENT` wire messages.
- Generic GPIO button events and an example-owned deterministic virtual-I/O schema.
- Personality A: GPIO events and deterministic virtual sensor.
- Personality B: personality A plus the asynchronous virtual actuator.
- Actuator `START` acceptance is separate from 25/50/75 percent progress and completion events.
  A bounded operation history makes a retried accepted operation ID idempotent.
- One Nordic image discovers either personality, samples the virtual sensor, invokes the optional
  actuator from its onboard button, and logs generic node events.
- All service, payload, event and history storage is statically bounded; MCU control paths do not
  allocate from the heap.

The virtual services use deterministic integer values and explicit time. They do not model or
control physical voltage, power, sensors, or motion.

## Dependency audit

Command: `.venv/bin/python scripts/doctor.py`

- Required native, Arm GNU, CMake/Ninja, NCS 3.4.1, Zephyr SDK, J-Link and CubeProgrammer tools:
  `READY` for builds.
- `cppcheck`: `MISSING`, optional and not used.
- `nrfutil`: `VERSION_MISMATCH` in the restricted runner because its home log directory is not
  writable. This does not block compilation; flashing from this environment was not attempted.
- USB serial/debug enumeration: unavailable in the restricted runner.

## Verification results

### Portable native build

```sh
cmake --preset native-debug
cmake --build --preset native-debug
ctest --preset native-debug --output-on-failure
```

Result: `PASS`, 4/4 tests. Coverage includes framing/parser regressions, registry validation and
bounds, deterministic samples, unsupported service/command, personalities, busy behavior,
duplicate actuator start, and ordered progress/completion events.

Independent validation also ran an ASan/UBSan build: `PASS`, 4/4. LeakSanitizer itself was disabled
because it is unavailable under the validation runner's ptrace restrictions.

### Nordic firmware

```sh
source .env.local
.venv/bin/west build -p always -b nrf54l15dk/nrf54l15/cpuapp \
  firmware/nrf54l15-host -d build/nrf54l15-host-phase3
```

Result: `PASS` with NCS 3.4.1 / Zephyr 4.4.2. Flash `46,052 B`; RAM `11,520 B`. The existing
`NRF_PLATFORM_LUMOS` deprecation warning remains.

### STM32 firmware

```sh
cd firmware/stm32u083/application
source ../../../.env.local
cmake --preset debug && cmake --build --preset debug
cmake --preset debug-personality-b && cmake --build --preset debug-personality-b
```

Result: `PASS` for both personalities with Arm GNU 15.2.1. Each image: text `26,308 B`, data
`24 B`, BSS `7,128 B`, total `33,460 B`. Both build trees export `compile_commands.json`. Existing
third-party FreeRTOS/HAL warnings, newlib syscall-stub warnings and the RWX load-segment warning
remain visible.

`git diff --check`: `PASS`. `cppcheck`: `BLOCKED_TOOL` (optional tool missing).

The first hardware flash exposed a Nordic main-thread stack overflow during nested response decode
and discovery transmission. The host TX frame and encoded scratch buffers were moved from
automatic storage into the singleton manager context, and the configured main stack was raised
from 1,536 to 2,048 bytes. This keeps storage statically bounded while preventing RX decoder and
TX encoder scratch areas from accumulating on the same callback stack. The corrected Nordic image
was rebuilt; hardware retest remains required.

## Hardware status and test procedure

Status: `PASS` by user observation on the physical nRF54L15-DK and NUCLEO-U083RC on 2026-10-13.
The already verified Phase 2 wiring and 3.3 V voltage gate remained unchanged.

1. Build/flash the Nordic image above and STM personality A (`debug`).
2. Confirm Nordic discovery logs `services`, two descriptors (GPIO `0x0001`, sensor `0x1001`),
   then `online`. Confirm a deterministic sensor log about every five seconds.
3. Press and release the STM button. Confirm STM event logs and matching Nordic `node button
   pressed/released` logs.
4. Press the Nordic button. With personality A, confirm it requests an extra sensor sample and no
   actuator operation.
5. Build/flash STM personality B (`debug-personality-b`) without changing the Nordic image.
6. Confirm three services, then press the Nordic button once. Confirm one acceptance/execution and
   later 25%, 50%, 75%, and completion logs for the same nonzero operation ID.
7. Do not press the Nordic button again until completion when checking the nominal sequence. A
   press while active should receive explicit `BUSY`, not start a second operation.

Observed result: personality B advertised GPIO `0x0001`, virtual sensor `0x1001`, and virtual
actuator `0x1002`; discovery reached `online`; periodic sensor acceptance and sample output were
visible; STM button events, Nordic-button actuator invocation, progress/completion, and busy
behavior all worked as intended. The corrected Nordic image no longer produced the startup stack
overflow. Complete raw logs were not captured into the repository.

## Open items

- The current STM startup-derived session value is nonzero but is not guaranteed to differ across
  every reboot. Durable reboot/session generation and reset supervision are assigned to Phase 5;
  stale-session behavior across an unlucky repeated value must not be overclaimed meanwhile.
- Native tests do not yet compile the Zephyr host parser as a host-side unit. Malformed generic
  framing remains covered by `protocol`; service-list/event parser integration is covered by the
  firmware builds and should gain a dedicated harness in the broader regression phase.

