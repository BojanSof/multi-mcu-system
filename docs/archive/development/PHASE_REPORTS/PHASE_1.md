# Phase 1 report — repository foundation, tools, independent builds

Date: 2026-10-12 (Europe/Skopje)
Repository revision tested: `25262ae200b1df539fd08f01b3e08f0ced2c07cd` plus the documented
STM32 build changes in the working tree.

## Outcome

Phase 1 native, Nordic, and STM32 builds pass with their pinned dependencies. Hardware acceptance
remains unavailable, so Phase 1 software implementation is complete but is not hardware-verified.

Delivered boundaries:

- Native-only root CMake with reusable targets `Protocol::Protocol`, `NodeApi::NodeApi`,
  `Diagnostics::Diagnostics`, `FirmwareImage::FirmwareImage`, and `BootProtocol::BootProtocol`.
- Independently configurable portable packages; the first three contain compiled C11 code, native
  tests, and Zephyr modules sharing the same source inventory. The two later-phase packages are
  interface-only reservations and do not advertise fake implementations.
- nRF54L15-DK Zephyr bring-up using `led0`, optional `sw0`, and the selected Zephyr debug console.
- NUCLEO-U083RC HAL/FreeRTOS bring-up with static task storage, onboard LED/button behavior, and a
  separate USART2 debug log adapter.
- Standard-library environment doctor, idempotent project `.venv` bootstrap, pinned Python lock,
  NCS west manifest, dependency lock, formatter configuration, authoritative flash-layout schema,
  and a physical voltage/wiring gate.
- The normal STM bring-up stays at the factory flash origin; the provisional A/B layout is disabled
  and reserved for Phase 7.
- Hardware feedback found and corrected the MB1932 B1 polarity: PC13 is active-low with pull-up.
- SWD fault inspection found that HAL enabled SysTick before FreeRTOS initialized its lists. Startup
  now suspends the HAL tick until the scheduler configures the kernel tick.

## Tool audit

Host: Arch Linux x86_64, Linux 7.2.9; native tools ready: Git 2.56.0, CMake/CTest
4.4.4, Ninja 1.13.2, GCC 16.2.1, Python 3.14.7, clang-format 23.1.1, clang-tidy,
J-Link 9.24a, and OpenOCD 0.12.0.

Resolved local tools:

- Arm GNU Toolchain 15.2.Rel1 is available at
  `/home/bojan/toolchains/arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi` and reports GCC 15.2.1,
  objcopy/binutils 2.45.1.
- STM32CubeCLT 1.22.0 is available at `/home/bojan/st/stm32cubeclt_1.22.0`; its bundled
  STM32CubeProgrammer reports 2.23.0.
- The project `.venv` contains pytest 8.4.2 and west 1.5.0; `pip check` reports no broken
  requirements.
- Nordic nRF Connect SDK v3.4.1 LTS is initialized under the ignored `.ncs-workspace`; its
  54-project manifest validates and workspace-selected Python requirements are installed.
- The ignored `.env.local` adds those tools and project `.venv/bin` to `PATH` for this checkout.

Remaining optional tool action:

- Optional: `pacman -S --needed cppcheck` and verify `cppcheck --version`.
- Make the nrfutil log directory writable in the user environment, then verify `nrfutil --version`.

The local `.venv` was created and the exact pins from `tools/requirements.lock` were installed.
Use `.venv/bin/west` directly or source `.env.local`.

## Verification matrix

| Check | Status | Evidence |
|---|---|---|
| Doctor Markdown/JSON, Python compile, strict nonzero exit | PASS | 22 checks; strict exit `1` for current missing requirements |
| Bootstrap shell syntax/idempotent venv creation | PASS | `.venv` created and locked dependencies installed |
| Root native configure/build | PASS | GCC/Ninja, 12 build steps |
| Root CTest | PASS | 3/3: protocol version, node API version, diagnostics counter |
| Standalone portable package configure/build | PASS | all five packages configure/build independently |
| Standalone implemented-library CTest | PASS | protocol 1/1, node-api 1/1, diagnostics 1/1 |
| clang-format dry-run | PASS | all project C/H files |
| STM preset discovery | PASS | `debug` preset listed |
| Arm GNU compiler/binutils | PASS | GCC 15.2.1 and binutils 2.45.1 from local 15.2.Rel1 toolchain |
| STM32CubeProgrammer | PASS | bundled v2.23.0 in STM32CubeCLT 1.22.0 |
| Project Python environment | PASS | pytest 8.4.2, west 1.5.0, `pip check` clean |
| STM cross configure/build/size | PASS | Arm GNU 15.2.1; text 14,040 B, data 24 B, BSS 3,584 B, total 17,648 B |
| STM compilation database | PASS | 20 entries, no duplicate source paths; clangd check passes with trusted cross-driver query |
| Nordic pinned west build | PASS | NCS 3.4.1/Zephyr 4.4.2, exact nRF54L15 application-core board target |
| Nordic memory reports | PASS | flash 40,052 B / 1,524 KiB (2.57%); RAM 8,600 B / 256 KiB (3.28%) |
| STM board detection and flash | PASS | MB1932-U083RC-B01, STM32U083 Rev A; ELF programmed and verified over SWD |
| STM LED, button, and VCP logs | PASS | user confirmed LD4 heartbeat, USART2 VCP logs, and corrected B1 behavior on hardware |
| Nordic board detection and HIL | NOT_RUN_HARDWARE | no Nordic hardware evidence recorded in this report |
| Inter-board wiring | BLOCKED_ELECTRICAL | exact revisions and compatible I/O voltages not physically verified |

## Exact commands executed

```sh
python3 scripts/doctor.py
python3 scripts/doctor.py --json | python3 -m json.tool >/dev/null
python3 scripts/doctor.py --strict
python3 -m py_compile scripts/doctor.py
sh -n scripts/bootstrap-venv.sh
cmake --preset native-debug
cmake --build --preset native-debug
ctest --preset native-debug
clang-format --dry-run --Werror $(find libraries firmware -name '*.c' -o -name '*.h')
cmake -S libraries/<package> -B /tmp/multi-mcu-<package> -G Ninja
cmake --build /tmp/multi-mcu-<package>
ctest --test-dir /tmp/multi-mcu-<package> --output-on-failure
cmake --list-presets -S firmware/stm32u083/application
cmake --preset debug -S firmware/stm32u083/application
cmake --build firmware/stm32u083/application/build/debug --parallel
arm-none-eabi-size firmware/stm32u083/application/build/debug/multi_mcu_stm32_application.elf
cd .ncs-workspace
../.venv/bin/west manifest --validate
../.venv/bin/west build -b nrf54l15dk/nrf54l15/cpuapp ../firmware/nrf54l15-host \
  --build-dir ../build/nrf54l15-host --pristine=always
../.venv/bin/west build -d ../build/nrf54l15-host/nrf54l15-host -t rom_report
../.venv/bin/west build -d ../build/nrf54l15-host/nrf54l15-host -t ram_report
```

Additional embedded diagnostic:

```sh
cmake -S firmware/nrf54l15-host -B /tmp/multi-mcu-nrf-check -G Ninja \
  -DBOARD=nrf54l15dk/nrf54l15/cpuapp
```

The earlier Nordic diagnostic against an unrelated incomplete Zephyr checkout failed and was not
counted. It was superseded by the successful pinned NCS 3.4.1 build above.

## Hardware acceptance steps

1. Connect only one board at a time by its debugger USB connector; expose USB/libusb and serial
   devices to the build environment and rerun `python3 scripts/doctor.py`.
2. Nordic build and memory reports now pass. After checking the attached target, run
   `cd .ncs-workspace && ../.venv/bin/west flash -d ../build/nrf54l15-host`; record the 500 ms LED
   toggle and button count logs. Flashing and observation remain `NOT_RUN_HARDWARE`.
3. STM: follow `firmware/stm32u083/application/README.md`, inspect ELF/map/size, then flash through
   the verified board debugger route. Record startup log, one LED toggle per button edge, and VCOM.
4. Do not connect the two boards. Record their exact PCB revisions, confirm BSP/solder-bridge routing,
   use Nordic Board Configurator, and physically verify compatible I/O voltage before Phase 2.
   Never join the USB 5 V or board 3.3 V rails.

## Outstanding risks / Phase 2 gate

- Save `west manifest --freeze` as a tracked artifact after deciding the repository policy for the
  large generated manifest.
- The STM32 generated linker script currently creates an RWX load segment; review linker segment
  permissions before later update/bootloader work.
- PA5/PC13 and USART2 PA2/PA3 are based on ST MB1932B BSP data but still require exact board-revision
  and solder-bridge confirmation for hardware acceptance.
- No physical inter-MCU pin assignment, UART protocol, option-byte change, bootloader flash, or
  hardware success claim was made in Phase 1.
