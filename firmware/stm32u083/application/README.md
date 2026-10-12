# NUCLEO-U083RC application bring-up

This independent STM32 application uses the pinned stm32-cmake integration, STM32CubeU0 HAL, and
the FreeRTOS Cortex-M0 port. It uses static task storage, blinks the on-board LED after a user-button
edge, and writes a separate bring-up log over the board debug VCOM candidate.

The platform values PA5 (LED), PC13 (button), and USART2 PA2/PA3 (VCOM) are based on ST's MB1932B
NUCLEO-U083RC BSP. They still require confirmation against the exact board revision and solder
bridge state before hardware acceptance. They are not approved inter-MCU signals.
The B1 user button on PC13 is active-low and is configured as an input with pull-up, as required by
the MB1932 user manual. During Phase 1 bring-up, LD4 also toggles every 500 ms and the firmware emits
a heartbeat log every two seconds so scheduler and VCP operation remain observable.

Prepare the ignored repository-local pinned dependencies, then run from the repository root:

```sh
git submodule update --init --recursive third_party/stm32-cmake
git clone --branch v5.6.0 --depth 1 https://github.com/STMicroelectronics/cmsis_core.git .deps/cmsis_core
git clone --branch v1.0.0 --depth 1 https://github.com/STMicroelectronics/cmsis-device-u0.git .deps/cmsis-device-u0
git clone --branch v1.0.0 --depth 1 https://github.com/STMicroelectronics/stm32u0xx-hal-driver.git .deps/stm32u0xx-hal-driver
git clone --branch V11.3.0 --depth 1 https://github.com/FreeRTOS/FreeRTOS-Kernel.git .deps/FreeRTOS-Kernel
source ./.env.local
cmake --preset debug -S firmware/stm32u083/application
cmake --build firmware/stm32u083/application/build/debug --parallel
arm-none-eabi-size firmware/stm32u083/application/build/debug/multi_mcu_stm32_application.elf
```

The preset exports `build/debug/compile_commands.json`; the repository `.clangd` selects it for
editors. See the root README for the clangd cross-compiler query setting. After a successful build,
inspect the ELF/map before any flash action.
With one Nucleo connected and its exact revision/routing confirmed, use the installed programmer or
OpenOCD according to the board manual. Expected hardware evidence is the startup log
`stm32: phase-1 bring-up ready`, a 500 ms LD4 heartbeat, a heartbeat log every two seconds, and an
additional LED toggle and log record per button press. Do not attach the Nordic board or any
inter-board wires during this test.
