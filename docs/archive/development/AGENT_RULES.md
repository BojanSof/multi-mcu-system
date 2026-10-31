# AGENTS.md — Codex rules for Multi-MCU Lab

**Read `DEVELOPMENT_PLAN.md` first.** This is the execution contract for a twelve-phase baseline plus Milestones 13–16, generic dual-MCU engineering demonstration using an **nRF54L15-DK** host and **NUCLEO-U083RC** node. Phases 1–8 are the delivered baseline; Phases 9–12 add operator control and automated recovery. The platform is reusable; virtual-I/O services are only an example. Do not reintroduce product-specific test-fixture terminology or hardwire it into portable libraries.

## Execution contract

1. At the start run a **dependency audit** (`scripts/doctor.py`, or implement it using the Python standard library first), verify the OS/SDK/toolchain/connected devices, and **tell the user** every required missing non-Python tool, required hardware step and exact install/verification instructions. Do not assume sudo/root, internet access or boards are accessible. You may set up project `.venv` and install pinned Python tool dependencies locally. Never use global Python `pip install`.
2. Follow the **twelve baseline phases** in `DEVELOPMENT_PLAN.md` and `docs/POST_BASELINE_PLAN.md`, followed by the approved `docs/RELIABILITY_PLAN.md` continuation. Before a phase, create a concise implementation plan and **freeze shared interfaces**. Run feasible tests and write `docs/PHASE_REPORTS/PHASE_N.md` before calling it finished. If tools or boards are missing, report blocked tests honestly and continue unaffected native/firmware work.
3. Use **parallel agents** where available, with clearly separated directory ownership and an integrator. Delegate independent library, Nordic, STM32, Python tooling and validation work. Don't allow parallel edits of common headers/schema, `west.yml`, root CMake, `flash_layout.yaml`, image signatures or pinout decisions. If agents unavailable, perform the same tasks sequentially.
4. Preserve working code: never overwrite a previous valid update/firmware profile in order to satisfy a new experimental bootloader variant. New CMake targets and configurations are independently selectable. Do not change critical boot option bytes without verified human authorization.
5. When implementing later signed A/B bootloader work, include native state-machine and malformed-image tests before writing hardware flash. Never claim production-level secure boot while SWD/protection are open or private keys are mishandled.

## Code and package ownership

- **All project firmware and portable library code is C**, using C11 for new portable libraries. Python is allowed for CLI, test harness, log collection, flash package/signing tools and test helpers; CMake/Kconfig/DTS/YAML for integration. No C++ classes, exceptions or project-specific heap allocation in MCU control paths.
- Functions are `Module_Function` (`Protocol_EncodeFrame`, `NodeApi_DispatchRequest`, `Diagnostics_RecordFault`, `VirtualActuator_Start`, `BootControl_ConfirmImage`). Types `Module_Type`; constants `MODULE_NAME_VALUE`. Explicit arguments, bounded buffers, clear return values, straightforward readable source. Use `clang-format`.
- **Independent CMake packages:** `libraries/{protocol,node-api,diagnostics,firmware-image,boot-protocol}`; each has its own public `include/<package>/`, `src/`, `CMakeLists.txt`, tests, reusable CMake target and Zephyr integration where required. No board or SDK dependencies in portable libraries. `examples/virtual-io` is a removable demonstration, not part of the protocol.
- Root CMake = **native tests/libraries only**. Nordic Zephyr application = `firmware/nrf54l15-host/`; STM32 CMake application = `firmware/stm32u083/application/`; independent STM32 **bare-metal bootloader** = `firmware/stm32u083/bootloader/`; related target HAL adapters = `firmware/stm32u083/platform/`.
- STM32 uses a pinned `third_party/stm32-cmake` submodule from <https://github.com/ObKo/stm32-cmake>, U0 CMSIS/HAL via validated `stm32_fetch_cmsis(U0)` and `stm32_fetch_hal(U0)`, pinned FreeRTOS Cortex-M0+ support, and generated linker layouts. Keep host MCU and STM32 cross builds independent.
- Zephyr integration: `zephyr/module.yml`, `zephyr/CMakeLists.txt`, `zephyr/Kconfig` per library that Nordic consumes. Set `EXTRA_ZEPHYR_MODULES` **before** `find_package(Zephyr ...)`. Use the exact same `src/` inventory as native CMake (no forked source copies). Pin NCS stable LTS patch (researched starting point **3.4.1**) and `west manifest --freeze` output.
- Dependency pins, licensing and setup in `deps/LOCK.md`; generated output in ignored directories. No private signing keys, debug captures with secrets or downloaded SDK trees committed.

## Hardware restrictions and safety

- Only **nRF54L15-DK**, **NUCLEO-U083RC**, onboard LEDs/buttons, USB and direct jumper wires. **All sensors, actuators, DUT/power behavior are software simulations.** No extra electronics may be required, and a logic analyzer is optional.
- nRF54L15-DK defaults to **1.8 V** I/O. STM Nucleo typically runs at **3.3 V**. Physical wire connections are BLOCKED until Nordic DK VDD is configured/verified compatible, exact board pins/solder bridges confirmed and `hardware/PINOUT.md` documented. Do not connect power rails; connect GND only and application signals after electrical verification.
- Preserve exposed inter-MCU UART TX/RX and **one STM DEBUG GPIO**, plus READY, IRQ, RESET, optional BOOT for supervision. Keep independent logging UARTs and SWD. Debug GPIO is only an observable firmware event, not a voltage sensor.
- Open-drain/high-Z reset driver only; safe BOOT0 default; don't set RDP Level 2, irreversible option bytes, write protection or erase without an approved, validated recovery path.
- STOP unsafe **physical** steps when facts are unverified; continue native/host-code work. Provide user checklists and label blocked work. Never fabricate pin mappings or software safety protections.

## Functional requirements

- Binary UART framing COBS + `0x00`, explicit little-endian fields, CRC32, fixed capacity, sequence IDs, operation IDs, bounded timeouts/retries, duplicate-operation avoidance, version/capability negotiation and asynchronous node event IRQ.
- Generic node management and simulated services: service registry, deterministic virtual sensors/actuator with integer units, onboard GPIO events; two capabilities/personalities, one host app. No 'fixture', product-specific test scripts or real power-control code in `protocol`, `node-api` or diagnostics.
- Separate MCU logging transports; structured fault counters and reset reasons; watchdog and heartbeat recovery without unexpected duplicate commands.
- Development-only **fault injection** mandatory: missed IRQ, dropped acknowledgment/duplicate operation, early virtual output, UART corruption, delayed response, crash/stall and later bad firmware image. Each has buggy reproduction, fixed behavior, native regression, and analyzer probe instructions (UART host TX + node TX + one GPIO). Simulated traces must not masquerade as real captures.
- Firmware management: Nordic supported MCUboot/MCUmgr; STM ROM recovery first; later independent STM custom bootloader with signed per-slot images, two slot-specific link variants, redundant metadata, pending/trial/confirmed/rollback and UART recovery. Validate memory budget of 256 KiB flash/40 KiB RAM/2 KiB pages; read source docs before vector relocation/jump work. If direct A/B infeasible, document fallback—not false success.

## Multi-agent boundaries

| Agent | Exclusive primary ownership |
|---|---|
| `lead/integrator` | Overall phase plan, cross-component APIs, root build/manifest, global dependency lock and merge review |
| `protocol-agent` | `libraries/protocol/`, later `libraries/boot-protocol/` |
| `node-agent` | `libraries/node-api/`, `examples/virtual-io/` as specifically assigned |
| `diagnostics-agent` | `libraries/diagnostics/` |
| `nordic-agent` | `firmware/nrf54l15-host/` |
| `stm-agent` | `firmware/stm32u083/application/`, board/platform adapters as assigned |
| `bootloader-agent` | `firmware/stm32u083/bootloader/` (Phase 7), not common flash schema ownership |
| `tools-agent` | `tools/`, `scripts/doctor.py`, `.venv` bootstrap |
| `rom-recovery-agent` | `libraries/stm32-rom-boot/` (Phase 11) |
| `validation-agent` | `tests/`, independent code review, exact reproducible result report |

If agent availability/worktree isolation is limited, enforce strict file ownership and sequential integration. Do **not** let an agent change published headers on its own after interface freeze. Conflict resolution and full builds are integrator-owned.

For Phases 9–12, the integrator owns `docs/CONTROL_API.md`, release manifest schema,
pinout and recovery policy. Nordic owns SMP/GPIO adapters; tools owns `tools/cli/`;
validation owns emulator/HIL acceptance. No runtime AI dependency is introduced.

## Required verification after each phase

- Run available native `cmake` / `ctest` targets, tests for malformed/partial UART streams, deterministic simulation, error and replay behavior.
- Run Nordic `west build` and STM32 CMake builds where required SDK/toolchain is actually installed; include build size and stack/memory reports.
- Execute HIL tests on the actual boards only when safely connected and accessible; otherwise mark `NOT_RUN_HARDWARE` with exact commands/user steps. Report `BLOCKED_TOOL` or `BLOCKED_ELECTRICAL` as appropriate.
- Capture complete commands, SDK versions, Git commit, `PASS`/`FAIL`/`NOT_RUN_HARDWARE` and outstanding problems in `docs/PHASE_REPORTS/PHASE_N.md`.
- Don't silently skip failing tests, assert a dependency has installed, forge a logic-analyzer capture, or claim firmware updates/revert work without flashing/testing.
- Demonstrate clean checkout and repro instructions as end-of-project acceptance. Concise README and project docs are part of implementation, not afterthoughts.

## Special gates for firmware updates

1. **First** prove recovery using ST ROM or SWD, verify BOOT0 selection and USART boot procedure on the real U083 board.
2. Firmware-image manifest must define canonical signed bytes and target slot/link address. Verify hash/signature with a vetted library; keep private key external to repository.
3. Generate linker constants from **one** `firmware/stm32u083/memory/flash_layout.yaml`; 64/88/88/16 KiB is provisional, must pass actual link/memory checks.
4. Don't assume Cortex-M0+ code is position-independent. Build Slot 0 and Slot 1 independently and validate interrupt-vector relocation on the actual STM32U083.
5. Bootloader writes only inactive application slot and its own approved metadata pages; must not erase itself or last confirmed image. Test interrupted update and metadata write boundaries.
6. Confirm trial firmware only after self-test; failed trial rolls back if last confirmed slot is valid. No automatic downgrade/anti-rollback claims without explicit policy/tests.

## Summary of phases

1. Repo, tool doctor/venv, pinned SDK/CMake components, independent board bring-up.
2. Safe wiring, UART framed protocol, node discovery and sideband GPIOs.
3. Portable node API, generic virtual-I/O services and demo events.
4. Diagnostics, fault injection and logic-analyzer debugging exercises.
5. Recovery, supervision, watchdogs, version/capability compatibility.
6. Nordic firmware OTA and STM32 ROM programming/recovery path.
7. Advanced custom STM32 bootloader: signed images and A/B selection/rollback.
8. CI, HIL, regression, documentation and extraction-ready release.
9. Structured USB SMP control, operator CLI and Nordic image management.
10. Remote reset and PC-streamed signed STM32 A/B updates.
11. Automated factory-ROM bootloader repair and chained signed recovery.
12. Reliability metrics, measured optimization, integrated HIL and final release demo.


## Approved reliability continuation

13. Nordic watchdog, additive health state and bounded automatic node recovery.
14. Persistent STM32 fault journal in dedicated reserved pages; host reset cause.
15. Negotiated acknowledged node events, level-held IRQ and host wake notifications.
16. Reproducible failure evidence, opt-in interrupted-update HIL and final acceptance.

Preserve historical profiles and application/boot metadata addresses. Shared
contracts and safety/test gates are in `docs/RELIABILITY_PLAN.md`; integrator owns
common APIs and flash schema. No automatic ROM repair or new electrical assumptions.
