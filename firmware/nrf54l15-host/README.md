# nRF54L15 host firmware

This independent Zephyr application is the Phase 1 bring-up for the
nRF54L15-DK application core. It blinks the board's devicetree `led0`, logs on
the board-selected Zephyr console, and reports interrupts from `sw0` when that
alias is available. It defines no inter-MCU pins and requires no wiring.

From an initialized workspace using the SDK version pinned by the repository
manifest:

```sh
west build -b nrf54l15dk/nrf54l15/cpuapp firmware/nrf54l15-host \
  --build-dir build/nrf54l15-host
```

Inspect the image without changing the board:

```sh
west build -d build/nrf54l15-host -t rom_report
west build -d build/nrf54l15-host -t ram_report
```

Flashing is a separate, explicit hardware step. With exactly one DK attached
and its debug probe visible, run `west flash -d build/nrf54l15-host`. Expected
hardware evidence is a 500 ms LED toggle and a log count increment for each
onboard button press. Do not attach the STM32 or any inter-board wires during
this test.
