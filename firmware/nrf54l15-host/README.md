# nRF54L15 host firmware

This independent Zephyr application contains the Phase 3 host link for the
nRF54L15-DK application core. It retains the onboard LED/button bring-up and
the independent debug console on `uart20`. A separate interrupt-driven
`uart21` transport uses fixed 1024-byte RX/TX rings and feeds the portable
COBS/CRC decoder and bounded discovery state machine. After management
discovery, the same host image enumerates the node's bounded service registry;
it does not assume a particular node personality.

The board overlay assigns host TX P1.11, host RX P1.12, active-low node IRQ
P1.07 with a pull-up, active-high node READY P1.06 with a pull-down, and the
inactive-low analyzer output P2.10. These are SoC GPIO names; consult the
authoritative `hardware/PINOUT.md` for physical header positions and the
electrical checklist. Do not connect either board until the nRF DK VDDIO is
physically verified at 3.3 V. Never connect the boards' power rails.

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
standalone evidence is a 500 ms LED toggle, a log count increment for each
onboard button press, and `discovering` state with no application-UART traffic while READY is low.
With a
safely wired compatible node, the log progresses through `discovering`,
`identity`, `capabilities`, `services`, and `online`, then sends a heartbeat
every two seconds. While online, it requests a deterministic virtual-sensor
sample every five seconds. The DK button starts a two-second virtual-actuator
operation when that optional service was enumerated; on personality A, where
the actuator is absent, the button requests an additional sensor sample.
Sequence-zero service events report the STM32 button and actuator progress or
completion on the DK debug console. Transport traffic is binary only; logs
remain on that separate console.
