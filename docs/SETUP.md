# Setup and reproducible builds

Run commands from the repository root unless a subshell is shown. These instructions
use Linux with Bash or Zsh. Building and testing do not flash either board.

## Prerequisites

Native tests need Git, a host C compiler, CMake **3.25 or newer** (preset schema6),
Ninja and Python. Use **Python3.12 or newer** for the complete nRF Connect SDK workflow;
the bootstrap helper's older minimum is not the SDK requirement.

Cross builds additionally need:

| Component | Tested version | Purpose |
|---|---|---|
| nRF Connect SDK | 3.4.1 LTS / Zephyr4.4.2 | Nordic firmware |
| Zephyr SDK | 1.0.1, Arm GCC14.3.0 | Nordic cross compiler |
| Arm GNU Toolchain | 15.2.Rel1, GCC15.2.1 | STM32 cross compiler/binutils |
| STM32CubeCLT | 1.22.0, CubeProgrammer2.23.0 | STM32 SWD/ROM programming |
| SEGGER J-Link and nRF Util | Installed tools visible on PATH | Nordic flash/debug |
| west | 1.5.0, pinned in Python lock | NCS workspace/builds |

Install tools using your distribution or their vendors. Install the architecture-matching
Zephyr SDK and its host tools using its supplied instructions. Add J-Link/nRF Util
executables to PATH. Pins, upstream URLs and licenses are in [deps/LOCK.md](../deps/LOCK.md).
A logic analyzer and optional static-analysis tools are not required for normal use.

## Checkout and Python environment

Replace `<repository-url>` with this repository's actual remote URL:

```sh
git clone --recurse-submodules <repository-url> multi-mcu-system
cd multi-mcu-system
python3 scripts/doctor.py
scripts/bootstrap-venv.sh
.venv/bin/python -m pip check
```

If already cloned, run `git submodule update --init --recursive` instead. The
bootstrap installs `tools/requirements.lock` into the ignored `.venv/`. Never use
global pip. Native tests can now run with `scripts/test/run_native.sh`; SDK-dependent
CBOR/adapter tests are enabled when the NCS sources are present.

## Local environment

```sh
cp .env.local.example .env.local
# Edit the toolchain, CubeCLT and Zephyr SDK installation paths in .env.local.
source ./.env.local
```

On an existing checkout, keep your working `.env.local` rather than overwriting it.
The example uses `$HOME`, not a particular developer's username. Source it from the
project root because it derives repository paths from `$PWD`. `.env.local` is ignored;
keep `.venv`, SDK trees, caches and private signing keys out of Git as well.

Verify tools after editing:

```sh
arm-none-eabi-gcc --version
arm-none-eabi-objcopy --version
STM32_Programmer_CLI --version
.venv/bin/west --version
python3 scripts/doctor.py
```

The doctor may find local fallback installations, but a clean setup must configure
PATH/environment explicitly. Missing USB devices do not block native tests or builds.
Use the reported install/permission actions before physical flashing; do not assume sudo.

## Nordic workspace

The SDK is separate from the application and ignored by this repository. On a fresh
checkout, create the pinned workspace and install its Python dependencies into the
same project venv:

```sh
.venv/bin/west init -m https://github.com/nrfconnect/sdk-nrf --mr v3.4.1 .ncs-workspace
(
  cd .ncs-workspace
  ../.venv/bin/west update
  ../.venv/bin/west manifest --validate
  ../.venv/bin/west packages pip --install
)
.venv/bin/python -m pip check
```

The subshell leaves your terminal at the project root. For an existing workspace,
verify `.ncs-workspace/nrf/VERSION`, manifest validity and resolved revisions against
`deps/west-manifest-freeze.yml`; do not run `west init` over it. The root `west.yml`
records the selected SDK pin; this workflow uses NCS as the workspace manifest.
No global `west zephyr-export` or CMake package-registry write is necessary.

```sh
scripts/build/build_nordic.sh --profile reliability
```

The board target is `nrf54l15dk/nrf54l15/cpuapp`. Output includes MCUboot and the
application under `build/nrf54l15-host-reliability/`. Existing diagnostic/legacy build
selectors remain supported by `--help`; the full-system guide uses `reliability`.

## STM32 dependencies and builds

CMSIS core/device, HAL and FreeRTOS are separate ignored checkouts. Create their
parent directory and clone each dependency once:

```sh
mkdir -p .deps
git clone --branch v5.6.0 --depth 1 https://github.com/STMicroelectronics/cmsis_core.git .deps/cmsis_core
git clone --branch v1.0.0 --depth 1 https://github.com/STMicroelectronics/cmsis-device-u0.git .deps/cmsis-device-u0
git clone --branch v1.0.0 --depth 1 https://github.com/STMicroelectronics/stm32u0xx-hal-driver.git .deps/stm32u0xx-hal-driver
git clone --branch V11.3.0 --depth 1 https://github.com/FreeRTOS/FreeRTOS-Kernel.git .deps/FreeRTOS-Kernel
```

Compare each `git -C .deps/NAME rev-parse HEAD` to its exact commit in the dependency
lock. The submodules already pin stm32-cmake, micro-ecc and Mbed TLS. No downloaded
SDK source belongs in the tracked tree.

```sh
scripts/build/build_stm32.sh --profile reliability
```

This builds release images for both A/B slots without generating keys or flashing.
Bootloader construction additionally needs an external generated public-key header;
see [firmware installation](FIRMWARE_UPDATES.md) for fresh-board key generation and
existing-key reuse. Never replace an installed bootloader's trust key accidentally.

For independent standalone bring-up only:

```sh
cmake --preset debug-personality-b -S firmware/stm32u083/application
cmake --build firmware/stm32u083/application/build/debug-personality-b --parallel
```

That whole-flash image is incompatible with an installed A/B layout. The reliability
option intentionally requires the A/B layout to reserve its flash journal pages.

## clangd / Neovim

STM32 presets export `compile_commands.json` in their build directory. `.clangd`
currently selects `firmware/stm32u083/application/build/debug`; configure that preset
for this default database, or point your editor at the slot build you actually use:

```sh
cmake --preset debug -S firmware/stm32u083/application
cmake --build firmware/stm32u083/application/build/debug --parallel
```

Permit only your trusted cross-compiler when configuring clangd, for example:

```lua
cmd = {
  "clangd",
  "--query-driver=/absolute/path/to/arm-toolchain/bin/arm-none-eabi-*",
}
```

For Nordic, the database is under the application sub-build, for example
`build/nrf54l15-host-reliability/nrf54l15-host/compile_commands.json`.

## Next steps

Complete [electrical checks](../hardware/PINOUT.md) before signal wiring or flashing.
Follow [initial installation and updates](FIRMWARE_UPDATES.md), then the
[operator guide](CLI.md) and [testing guide](TESTING.md).
