# Phase 5 completion report

Date: 2026-10-16  
Baseline commit: `1442eb85cc9d2d3f74516edee55a759e725d3c7f`

## Delivered

- Lifecycle service 1.0 with status/version reporting and explicit INIT, READY, ONLINE,
  MAINTENANCE, software-only SHUTDOWN and FAULT_LATCHED states.
- STM32 meaningful-progress IWDG supervision, reset-cause reporting, retained warm-reset boot count
  and session regeneration. The six-second development stall injection stops progress and permits
  watchdog recovery.
- Nordic rediscovery with pending-operation invalidation, service compatibility filtering, expected
  lifecycle READY-transition handling, unexpected reboot counting and a three-reboots-in-30-seconds
  fault latch. Remote STM reset remains disabled.
- Maintenance and shutdown keep lifecycle, diagnostics and heartbeat reachable but reject/quiesce
  virtual I/O. Shutdown does not claim or perform physical power removal.
- Development-only `debug-incompatible-service` STM32 preset for an end-to-end optional-service
  compatibility rejection test.
- Compatibility policy is documented in `docs/COMPATIBILITY.md`; wire details are in
  `docs/PROTOCOL.md`.

The STM32U083 RCC interface exposes a combined power-reset indication. The adapter therefore
reports it conservatively as POWER_ON and retains normalized raw flags; it does not fabricate a
distinct brownout diagnosis.

## Verification

| Check | Result |
|---|---|
| Dependency audit | PASS for required build tools; optional `cppcheck` missing |
| Native CMake/CTest | PASS, 4/4 |
| Python tooling tests | PASS, 17/17 |
| Nordic pristine NCS 3.4.1 build | PASS, FLASH 49,952 B; RAM 12,984 B |
| STM32 Debug A | PASS, text 35,832 B; data 104 B; bss 9,440 B |
| STM32 Debug B | PASS, text 35,832 B; data 104 B; bss 9,440 B |
| STM32 early-READY | PASS, text 35,848 B; data 104 B; bss 9,440 B |
| STM32 incompatible-service | PASS, text 35,912 B; data 104 B; bss 9,440 B |
| STM32 Release B | PASS, text 23,280 B; data 104 B; bss 9,440 B |
| Formatting / whitespace | PASS (`clang-format`, `git diff --check`) |
| Two-board supervision and recovery | NOT_RUN_HARDWARE (USB/libusb unavailable to agent) |
| Physical remote NRST escalation | BLOCKED_ELECTRICAL (not wired/approved; disabled in firmware) |

Commands:

```sh
cmake --preset native-debug
cmake --build --preset native-debug --parallel
ctest --preset native-debug --output-on-failure
.venv/bin/python -m pytest tools/tests -q

source ./.env.local
cmake --preset debug -S firmware/stm32u083/application
cmake --build firmware/stm32u083/application/build/debug --parallel
cmake --preset debug-personality-b -S firmware/stm32u083/application
cmake --build firmware/stm32u083/application/build/debug-personality-b --parallel
cmake --preset debug-early-ready -S firmware/stm32u083/application
cmake --build firmware/stm32u083/application/build/debug-early-ready --parallel
cmake --preset debug-incompatible-service -S firmware/stm32u083/application
cmake --build firmware/stm32u083/application/build/debug-incompatible-service --parallel
cmake --preset release-personality-b -S firmware/stm32u083/application
cmake --build firmware/stm32u083/application/build/release-personality-b --parallel

cd .ncs-workspace
../.venv/bin/west build -b nrf54l15dk/nrf54l15/cpuapp \
  ../firmware/nrf54l15-host --build-dir ../build/nrf54l15-host --pristine=always
```

Tool versions: Linux 7.2.9; CMake 4.3.1; Ninja 1.13.2; native GCC 16.2.1; Arm GNU
15.2.1 for the recorded STM32 builds; west 1.5.0; NCS 3.4.1 / Zephyr 4.4.2; Zephyr SDK
1.0.1; STM32CubeProgrammer 2.23.0.

Existing warnings remain: Zephyr's deprecated `NRF_PLATFORM_LUMOS`, third-party STM32 HAL/newlib
warnings, and the STM32 ELF RWX LOAD-segment warning. No new project-source compiler warning was
observed.

## Required hardware acceptance

Use the already verified Phase 2 wiring; do not add RESET, BOOT0 or power-rail connections.

1. Flash normal Debug personality B and the Nordic image. Confirm discovery, lifecycle/version
   logs and normal sensor/actuator operation.
2. Nordic button: short press starts the demo; 5-8 s toggles maintenance; 8+ s toggles software
   shutdown. In both controlled states management remains reachable and virtual I/O is suppressed;
   exit returns ONLINE without adding a reboot count.
3. Select development fault 8 (1.5-5 s button holds cycle the selector). Confirm STM watchdog reset,
   rediscovery, reset reason WATCHDOG, increased boot count/new session, and no phantom operation.
4. Reset STM during an actuator operation. Confirm pending work is discarded and not replayed.
   Reset it three times within 30 seconds and confirm the Nordic host becomes `fault-latched`;
   restarting Nordic deliberately clears the latch.
5. Reset Nordic independently and confirm clean rediscovery without replaying an old actuator call.
6. Flash STM preset `debug-incompatible-service`. Confirm the host logs sensor service 0x1001 as
   unavailable while lifecycle, diagnostics, GPIO and actuator remain usable and the node stays
   online. Restore normal Debug B afterward.

Phase 5 implementation and software verification are complete. Hardware exit tests remain
`NOT_RUN_HARDWARE` until the user executes the checklist and supplies the logs.
