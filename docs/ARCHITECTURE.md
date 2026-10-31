# System architecture

Multi-MCU Lab is a host/managed-node reference system using an nRF54L15-DK and a
NUCLEO-U083RC. The virtual sensor and actuator are removable software examples;
communication, supervision and firmware management are generic platform features.

```text
PC operator CLI (mmctl)
        | SMP v2 / CBOR through J-Link USB-to-UART bridge
        v
nRF54L15 host: Zephyr / nRF Connect SDK
        | UART 115200 8N1 + READY / IRQ_N / NRST / BOOT0
        v
STM32U083 node: STM32 HAL / FreeRTOS
        | registered lifecycle, diagnostics and virtual-I/O services
        + independent signed A/B bootloader
        + factory-ROM repair path
```

The `reliability` host profile includes operator control, metrics, a hardware
watchdog, bounded automatic node recovery, signed STM updates and explicit ROM
repair. Related STM reliability slot presets add persistent fault history and
negotiated acknowledged events. Earlier build interfaces remain selectable for
compatibility, but new setups should follow [SETUP.md](SETUP.md).

## Component boundaries

| Component | Owns | Does not own |
|---|---|---|
| `libraries/protocol` | COBS framing, CRC32, incremental stream parsing | UART, RTOS, services |
| `libraries/node-api` | Service registry, dispatch, compatibility and bounded event queue | Board drivers or example-specific service IDs |
| `libraries/diagnostics` | Volatile fault records, injections and portable persistent-journal algorithm | Flash HAL or logging backend |
| `libraries/firmware-image` | Package parsing, signed-byte definition and compatibility checks | Keys, cryptographic primitives or flash writes |
| `libraries/boot-protocol` | Custom bootloader message encoding and validation | Boot selection or UART ownership |
| `libraries/stm32-rom-boot` | Restricted factory-ROM transactions and readback checking | Boot GPIOs, parity configuration or protection changes |
| `examples/virtual-io` | Deterministic sensor and asynchronous modeled actuator | Physical sensing, motion or power control |
| Nordic application | Discovery, PC control, supervision and update orchestration | Node application internals |
| STM application | Local services, FreeRTOS tasks, watchdog and fault flash adapter | Image signature or boot selection policy |
| STM bootloader | Signature verification, flash journal, inactive-slot update and handoff | FreeRTOS or application services |

Every portable package uses C11 with bounded caller-owned storage. Native CMake
and Zephyr consume the same source inventory. The root build runs native tests;
Nordic, STM application and STM bootloader are independent cross builds.

## Runtime ownership

The Nordic main thread owns the node manager and inter-MCU UART mode. UART
interrupts feed bounded buffers; SMP workqueue handlers submit bounded requests
and read snapshots rather than changing manager state concurrently. One runtime
request is outstanding at a time, while accepted actuator operations can publish
asynchronous progress.

The STM application receives UART bytes into a bounded ring and processes them in
its application task. Its event producer returns backpressure when the reliable
event queue is full. A separate watchdog task refreshes IWDG only while application
progress advances. No project MCU control path allocates heap memory.

Runtime, custom bootloader and factory ROM protocols share the UART exclusively.
Update/recovery ownership pauses runtime traffic and reset escalation; restoration
resets parser state and restarts discovery. Nordic forwards firmware chunks from
the PC without staging the complete STM image in RAM.

## GPIOs, logging and recovery

READY reports application availability. IRQ_N stays asserted while acknowledged
node events are pending. With the verified Port 2 wiring, the pinned Nordic driver
cannot supply GPIO interrupts: UART interrupt wake and 10 ms polling provide the
host fallback, reported as `gpio_interrupts:false`. Required GPIO configuration
errors remain fatal; optional interrupt errors do not stop startup.

Nordic application logs use RTT, PC SMP uses J-Link VCOM, and STM logs use its
separate ST-LINK VCOM. The inter-MCU UART never carries unframed application text.
NRST is open-drain and BOOT0 stays low outside explicit ROM repair. There is no
power-enable circuit or BLE transport in this demonstration.

A failed node does not prevent the healthy host watchdog from being fed. Automatic
node recovery is bounded and suppressed in intentional lifecycle/update modes.
[Compatibility and lifecycle policy](COMPATIBILITY.md) defines sessions and latches.

## Persistence and trust boundaries

Runtime event history, duplicate caches, metrics and diagnostic queues are volatile.
STM boot selection and important node fault records use separate flash journals;
boot count also has retained-RAM warm-reset tracking. A record's local uptime is
not a globally synchronized timestamp or proof of power-cycle session uniqueness.

Application and journal CRC32, firmware SHA-256 and STM ECDSA verification currently
run in software. STM signing authenticates images against the bootloader public
key, but open SWD, factory-ROM repair and development provisioning do not form a
production-secure platform. Neither runtime traffic nor image payloads are encrypted.
See [bootloader design](BOOTLOADER.md), [wire protocol](PROTOCOL.md) and
[PC control API](CONTROL_API.md) for the detailed contracts.
