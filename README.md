# Multi-MCU Lab

Reusable dual-MCU engineering demonstration for an nRF54L15-DK host and NUCLEO-U083RC node.
Phase 1 establishes independent native, Nordic Zephyr, and STM32 HAL/FreeRTOS build boundaries.

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
