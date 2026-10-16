# NUCLEO-U083RC application

This independent STM32 application uses the pinned stm32-cmake integration, STM32CubeU0 HAL, and
the FreeRTOS Cortex-M0 port. It uses static task storage, runs an independent on-board LED
heartbeat, logs user-button edges, and writes a separate log over the board debug VCOM candidate.

The platform values PA5 (LED), PC13 (button), and USART2 PA2/PA3 (VCOM) are based on ST's MB1932B
NUCLEO-U083RC BSP. They still require confirmation against the exact board revision and solder
bridge state before hardware acceptance. They are not inter-MCU signals.
The B1 user button on PC13 is active-low and is configured as an input with pull-up, as required by
the MB1932 user manual. LD4 also toggles every 500 ms and the firmware emits
a heartbeat log every two seconds so scheduler and VCP operation remain observable.

Prepare the ignored repository-local pinned dependencies, then run from the repository root:

```sh
git submodule update --init --recursive third_party/stm32-cmake
git clone --branch v5.6.0 --depth 1 https://github.com/STMicroelectronics/cmsis_core.git .deps/cmsis_core
git clone --branch v1.0.0 --depth 1 https://github.com/STMicroelectronics/cmsis-device-u0.git .deps/cmsis-device-u0
git clone --branch v1.0.0 --depth 1 https://github.com/STMicroelectronics/stm32u0xx-hal-driver.git .deps/stm32u0xx-hal-driver
git clone --branch V11.3.0 --depth 1 https://github.com/FreeRTOS/FreeRTOS-Kernel.git .deps/FreeRTOS-Kernel
source ./.env.local
cmake --preset debug -S firmware/stm32u083/application \
  -DMULTI_MCU_STM32_PERSONALITY=B
cmake --build firmware/stm32u083/application/build/debug --parallel
arm-none-eabi-size firmware/stm32u083/application/build/debug/multi_mcu_stm32_application.elf
```

Select personality `A` for GPIO events plus the deterministic virtual sensor, or `B` to add the
virtual actuator. The selection is a CMake cache setting, so reconfigure and rebuild before
flashing a different personality. The preset exports `build/debug/compile_commands.json`; the repository `.clangd` selects it for
editors. See the root README for the clangd cross-compiler query setting. After a successful build,
inspect the ELF/map before any flash action. With exactly one Nucleo attached, flash and verify it:

Debug builds enable the development-only diagnostics injection commands. Configure with
`-DMULTI_MCU_ENABLE_DEV_FAULTS=OFF` to remove runtime injection, which is also the default for
non-Debug builds. The analyzer-only early-READY bug must be selected explicitly with
`-DMULTI_MCU_EARLY_READY_BOOT_DEMO=ON`; CMake rejects that option when development faults are off.
All runtime injection state is cleared by reset.

```sh
STM32_Programmer_CLI -c port=SWD \
  -w firmware/stm32u083/application/build/debug/multi_mcu_stm32_application.elf \
  -v -rst
```

Expected standalone evidence is `stm32: phase-5 node ready, personality A` (or `B`), an LD4
transition every 500 ms, a heartbeat log every two seconds, and a log for each button press and
release. The button does not alter LD4; with a connected host it publishes a generic GPIO service
event. For the two-board test, first complete `hardware/PINOUT.md`'s voltage gate, power both
boards off, and follow `hardware/WIRING.md`. Do not connect power rails, RESET, or BOOT0.

## Phase 5 supervision

The lifecycle traverses INIT, READY and ONLINE during startup. It keeps heartbeat, diagnostics and
lifecycle control available in maintenance and software-only shutdown, while virtual sensor and
actuator calls return `BUSY`. Shutdown only quiesces simulated services; it never claims physical
power removal. READY pulses low for one 20 ms application poll during each enter/exit transition,
then returns high so the host can rediscover and issue EXIT.

The IWDG is nominally 4.096 seconds at the 32 kHz LSI clock (at least about 2.6 seconds over the
documented oscillator tolerance). A separate static FreeRTOS monitor refreshes it only after the
application progress counter advances. Development fault 8 stalls progress for six seconds and is
therefore expected to reset the node. The startup line and lifecycle status report the normalized
reset cause and a checksum-protected boot count retained in a `.noinit` SRAM record. Removing power
invalidates that volatile history; it is not persistent flash metadata.

Preset `debug-incompatible-service` is the development-only compatibility negative test. It
advertises only the virtual-sensor service with an intentionally unsupported major version. The
host must keep the node and its other services online while treating that sensor as unavailable.
