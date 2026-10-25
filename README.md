# Multi-MCU Lab

Reusable dual-MCU engineering demonstration for an nRF54L15-DK host and NUCLEO-U083RC node.
Phase 1 establishes independent build boundaries. Phase 2 adds the bounded COBS/CRC32 UART
protocol, node discovery, READY and active-low IRQ sidebands. Phase 3 adds a portable service
registry and a removable deterministic virtual-I/O demonstration. Phase 4 adds bounded structured
diagnostics, development-only fault exercises and correlated log/trace tooling. Phase 5 adds
lifecycle control, watchdog/reset supervision, reboot resynchronization and explicit service-version
compatibility. Phase 6 adds standard recovery/update paths, and Phase 7 adds the separately
selectable signed STM32 direct-A/B profile. Phase 8 adds canonical regression/build scripts,
CI, optional observation-only HIL tooling, and extraction/release documentation; see
`docs/COMPATIBILITY.md` and the phase reports.

Phase 9 adds an opt-in USB operator CLI and typed host control interface. The
approved continuation is in [docs/POST_BASELINE_PLAN.md](docs/POST_BASELINE_PLAN.md),
with the frozen protocol in [docs/CONTROL_API.md](docs/CONTROL_API.md).

## Final operator demo (Phase 12)

Keep the working signed-A/B STM installation and verified Phase11 wiring. From root:

```sh
source ./.env.local
scripts/build/build_nordic.sh --profile phase12
.venv/bin/west flash -d build/nrf54l15-host-phase12
scripts/mmctl metrics
scripts/demo.sh --samples 5 --watch-seconds 10 --report build/smoke.json
```

Close other Nordic VCOM users; add `--port /dev/serial/by-id/...` when ambiguous.
The default demo runs100 sensor requests and a ten-minute watch; press the STM
button during watch. No firmware writes or resets occur unless explicitly requested.
[Full acceptance commands](docs/PHASE_12_TESTING.md),
[reproducible release guide](docs/FINAL_RELEASE.md) and phase reports distinguish
implemented software from outstanding hardware acceptance.

## USB operator control (Phase 9)

From the project root, build and initially flash the dedicated Nordic profile:

```sh
source ./.env.local
.venv/bin/python -m pip install -r tools/requirements.lock
scripts/build/build_nordic.sh --profile phase9
.venv/bin/west flash -d build/nrf54l15-host-phase9
scripts/mmctl device list
```

Use the Nordic J-Link VCOM shown by device discovery; close any log terminal using
that port. Existing STM firmware and the five application wires suffice for this
phase. The CLI owns the Nordic serial connection while executing. See
[docs/CLI.md](docs/CLI.md) for operations and expected results. Additional reset/BOOT0
wires and streamed signed STM updates are available in the separate `phase10`
profile. See [Phase 10 testing](docs/PHASE_10_TESTING.md) for wiring gates and
root-directory build/flash/update commands. The separate `phase11` profile adds
explicit factory-ROM bootloader repair with a validated release bundle; see
[Phase 11 testing](docs/PHASE_11_TESTING.md). Hardware acceptance is recorded
separately from native tests and firmware builds.

## Local setup

Create the ignored project Python environment, load the ignored local tool paths, and audit the
host:

```sh
scripts/bootstrap-venv.sh
source ./.env.local
python3 scripts/doctor.py
.venv/bin/python -m pip check
```

`.env.local` and `.venv/` are deliberately ignored. The current local environment uses:

```text
Arm GNU:     /home/bojan/toolchains/arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi
CubeCLT:     /home/bojan/st/stm32cubeclt_1.22.0
Python venv: /home/bojan/projects/multi-mcu-system/.venv
NCS:         /home/bojan/projects/multi-mcu-system/.ncs-workspace
```

Recreate `.env.local` with appropriate absolute paths on another machine. It adds Arm GNU,
STM32CubeProgrammer, and `.venv/bin` to `PATH`; sets `STM32_TOOLCHAIN_PATH`, `NCS_ROOT`, and
`ZEPHYR_BASE`; and directs ccache to the ignored repository-local `.cache/ccache`. The doctor also
probes the current machine's known local paths directly.

Verify the local command-line tools:

```sh
arm-none-eabi-gcc --version
arm-none-eabi-objcopy --version
STM32_Programmer_CLI --version
.venv/bin/west --version
```

The verified versions are Arm GNU 15.2.Rel1 (GCC 15.2.1/binutils 2.45.1), STM32CubeCLT 1.22.0
with STM32CubeProgrammer 2.23.0, pytest 8.4.2, and west 1.5.0.

## Native build and tests

The root build contains native libraries and tests only:

```sh
source ./.env.local
cmake --preset native-debug
cmake --build --preset native-debug
ctest --preset native-debug
```

The canonical Phase 8 regression entry point performs the native library tests, bootloader
state-machine tests, Python tooling tests, and HIL-runner unit tests:

```sh
scripts/test/run_native.sh
```

Cross-build every STM32 application/slot plus the bootloader using an external generated public-key
header, or build the normal Nordic host, from the repository root:

```sh
scripts/build/build_stm32.sh --public-key-header /absolute/path/to/BootPublicKey.h
scripts/build/build_nordic.sh --profile default
```

These scripts never flash hardware. See `docs/USER_DEMO.md` for the integrated board demonstration,
`tests/hil/README.md` for explicitly enabled serial observation, `docs/RELEASE_MATRIX.md` for version
compatibility, and `docs/EXTRACTION.md` / `docs/API_CONTRACTS.md` for reusable package boundaries.

## nRF Connect SDK workspace

Create the ignored NCS v3.4.1 LTS workspace inside this repository:

```sh
source ./.env.local
west init -m https://github.com/nrfconnect/sdk-nrf --mr v3.4.1 .ncs-workspace
cd .ncs-workspace
../.venv/bin/west update
../.venv/bin/west manifest --validate
../.venv/bin/west packages pip --install
cd ..
```

The last command installs the Python requirements selected by the exact NCS manifest into the
project `.venv`; do not install them globally. Validate an existing workspace with:

```sh
cd .ncs-workspace
../.venv/bin/west topdir
cat nrf/VERSION
../.venv/bin/west manifest --validate
../.venv/bin/west list | wc -l
cd ..
```

Expected values are this repository's `.ncs-workspace` path, NCS `3.4.1` with `lts` metadata, and
54 manifest projects. Build the Nordic host with output under the ignored root build directory:

```sh
source ./.env.local
cd .ncs-workspace
../.venv/bin/west build -b nrf54l15dk/nrf54l15/cpuapp \
  ../firmware/nrf54l15-host \
  --build-dir ../build/nrf54l15-host \
  --pristine=always
../.venv/bin/west build -d ../build/nrf54l15-host/nrf54l15-host -t rom_report
../.venv/bin/west build -d ../build/nrf54l15-host/nrf54l15-host -t ram_report
cd ..
```

`west zephyr-export` is optional for these explicit workspace builds. It writes to the user's CMake
package registry and can fail in restricted environments without preventing `west build`.

## STM32 application setup

The STM32 application uses the pinned `third_party/stm32-cmake` submodule and ignored, repository-
local dependency checkouts under `.deps/`. Follow
`firmware/stm32u083/application/README.md` to populate them. Then build from the repository root:

```sh
source ./.env.local
cmake --preset debug -S firmware/stm32u083/application
cmake --build firmware/stm32u083/application/build/debug --parallel

# Optional personality B adds the virtual actuator.
cmake --preset debug-personality-b -S firmware/stm32u083/application
cmake --build firmware/stm32u083/application/build/debug-personality-b --parallel
```

The configure preset always exports
`firmware/stm32u083/application/build/debug/compile_commands.json`. The repository `.clangd` points
clangd at that database. For clangd to discover the cross-compiler's built-in headers, configure
Neovim's clangd command with this trusted driver glob:

```lua
cmd = {
  "clangd",
  "--query-driver=/home/bojan/toolchains/arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi/bin/arm-none-eabi-*",
}
```

Adjust the path on another machine. See the application README for size and hardware verification
commands; configuring or building never flashes the board.

## Hardware safety

Firmware flashing is an explicit hardware step, never part of setup. Do not wire the boards until
`hardware/PINOUT.md` has been completed for the exact board revisions and compatible voltage levels
have been physically verified. Never connect their USB 5 V or board supply rails together.

## Phase 6 firmware lifecycle

Phase 6 adds an opt-in Nordic MCUboot/MCUmgr serial-update build, deterministic portable `MMFW`
image tooling, and documented STM32 SWD/factory-ROM recovery. It does not replace the earlier
non-MCUboot profiles. See [firmware update and recovery](docs/FIRMWARE_UPDATES.md) for exact build,
flash, OTA, rollback, and STM recovery commands. The Nordic build currently uses the public NCS
development key and `MMFW` provides integrity only; neither is a production authenticity claim.

## Phase 7 STM32 signed A/B profile

The custom bootloader profile does not replace the ROM-bring-up application. It builds two
independently linked STM32 applications and packages each with a signed `MMFW` v2 header. Private
keys are external and `keys/` is ignored. See [the bootloader guide](docs/BOOTLOADER.md) for the
layout, security limits, build/sign commands, and the hardware test sequence.

## Phase 2 two-board test

Read [the frozen pinout](hardware/PINOUT.md) and complete its voltage gate before attaching any
signal wire. In particular, set and measure Nordic `VDD:IO` at 3.3 V; its default may be 1.8 V.
Then follow [the wiring procedure](hardware/WIRING.md). Never connect a power rail between boards.

Build and flash the STM32 application and Nordic host independently. Keep the STM USART2 ST-LINK
VCP and Nordic console open as separate log streams. With STM powered first, Nordic should report
the link progressing to `online`, identity `stm32u083rc-node`, a nonzero session generation, and
capabilities `0x00000007`. Unplugging or resetting the STM board must take the host out of online;
after READY returns it must rediscover the node without requiring a Nordic reset. See
`docs/PROTOCOL.md` for the wire contract and `hardware/LOGIC_ANALYZER.md` for optional probes.

## Phase 3 service demo

The default STM `debug` preset builds personality A: GPIO button events plus the deterministic
virtual sensor. The `debug-personality-b` preset additionally exposes the asynchronous virtual
actuator. Both use the same Nordic image.

After discovery, the Nordic log lists the advertised service IDs and reaches `online`. It samples
the simulated sensor every five seconds. On personality A, the Nordic button requests an extra
sample. On personality B, it starts a two-second virtual actuator operation; the Nordic log then
shows acceptance followed separately by 25%, 50%, 75%, and completion events. Pressing the STM
button produces pressed/released GPIO events in both logs. These integer-valued services are
software simulations and do not represent physical sensing, voltage, power, or motion.

## Phase 4 diagnostics exercises

The normal Debug presets include volatile development fault controls; all injections boot off.
Short Nordic button presses retain the service demo, while presses held for at least 1.5 seconds
cycle fault IDs `OFF -> 1 .. 8 -> OFF`. Build the explicit early-READY analyzer profile with:

```sh
source ./.env.local
cmake --preset debug-early-ready -S firmware/stm32u083/application
cmake --build firmware/stm32u083/application/build/debug-early-ready --parallel
```

Read [the fault-injection guide](docs/FAULT_INJECTION.md) before enabling a case and use
[the analyzer tutorials](hardware/LOGIC_ANALYZER.md) for exact CH0/CH1/CH2 connections and expected
evidence. Python-generated traces are always labeled `SIMULATED`; no analyzer is required.
