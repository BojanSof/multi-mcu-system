# NUCLEO-U083RC application bring-up

This independent STM32 application uses the pinned stm32-cmake integration, STM32CubeU0 HAL, and
the FreeRTOS Cortex-M0 port. It uses static task storage, blinks the on-board LED after a user-button
edge, and writes a separate bring-up log over the board debug VCOM candidate.

The platform values PA5 (LED), PC13 (button), and USART2 PA2/PA3 (VCOM) are based on ST's MB1932B
NUCLEO-U083RC BSP. They still require confirmation against the exact board revision and solder
bridge state before hardware acceptance. They are not approved inter-MCU signals.

Prepare pinned dependencies outside generated build directories, then run:

```sh
git submodule update --init --recursive third_party/stm32-cmake
git clone --branch V11.3.0 --depth 1 https://github.com/FreeRTOS/FreeRTOS-Kernel.git /path/to/FreeRTOS-Kernel
cmake --preset debug -S firmware/stm32u083/application \
  -DFREERTOS_PATH=/path/to/FreeRTOS-Kernel
cmake --build --preset debug
arm-none-eabi-size firmware/stm32u083/application/build/debug/multi_mcu_stm32_application
```

The configure step fetches the stm32-cmake-pinned CubeU0 CMSIS/HAL sources unless offline source
directories are supplied. After a successful build, inspect the ELF/map before any flash action.
With one Nucleo connected and its exact revision/routing confirmed, use the installed programmer or
OpenOCD according to the board manual. Expected hardware evidence is the startup log
`stm32: phase-1 bring-up ready`, then one LED toggle and log record per button press. Do not attach
the Nordic board or any inter-board wires during this test.
