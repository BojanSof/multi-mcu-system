# Multi-MCU Lab

A reusable dual-MCU demonstration with an **nRF54L15-DK host** and a
**NUCLEO-U083RC node**. It covers discovery, versioned services, reliable UART events,
watchdog supervision, persistent fault reporting, signed firmware updates and ROM recovery.
Virtual sensors and actuators are software simulations; no external peripherals are needed.

```text
PC / mmctl
    | USB / J-Link VCOM: SMP + CBOR
nRF54L15 host: Zephyr / nRF Connect SDK
    | UART + READY / IRQ_N / RESET / BOOT0
STM32U083 node: HAL / FreeRTOS
    | signed A/B application slots
    + independent custom bootloader / factory-ROM recovery
```

## What it demonstrates

- Portable C11 libraries for COBS/CRC32 framing, service dispatch, diagnostics,
  signed-image formats, custom boot messages and STM32 ROM communication.
- Session and operation IDs, bounded retries, duplicate suppression and acknowledged
  events. The host remains available when the node is offline.
- Deterministic sensor samples, asynchronous actuator progress and onboard button events.
- Local watchdogs, bounded automatic node reset, maintenance modes and a flash fault journal.
- Nordic MCUboot/MCUmgr updates, PC-streamed signed STM32 slot updates and explicit
  factory-ROM bootloader repair, using separate logging channels and preserved SWD access.

This is a development reference, not certified safety or production-secure boot.
SWD and factory recovery remain open; images and runtime traffic are not encrypted.

## Hardware first

Use the documented board revisions, configure and **measure Nordic I/O at 3.3 V**,
then follow the [pinout](hardware/PINOUT.md) and [wiring guide](hardware/WIRING.md).
Never join the boards' power rails. Use the verified Nordic-first power order.
RESET must sink low or release high-Z; BOOT0 stays low except during explicit ROM repair.

The existing Port 2 READY/IRQ pins do not support GPIO interrupts in the pinned SDK.
The host uses UART wakeups and a 10 ms polling fallback; `gpio_interrupts:false`
is expected. This does not disable acknowledged events.

## Get started

Clone this repository, then run from its root:

```sh
git submodule update --init --recursive
python3 scripts/doctor.py
scripts/bootstrap-venv.sh
scripts/test/run_native.sh
```

The native tests require a host C compiler, CMake, Ninja and Python. The
[setup guide](docs/SETUP.md) covers the separate SDK/toolchain requirements, pinned
STM dependencies, local `.venv`, ignored `.env.local` and editor configuration.
No global pip installs or developer-specific directory are required.

After completing setup:

```sh
source ./.env.local
scripts/build/build_nordic.sh --profile reliability
scripts/build/build_stm32.sh --profile reliability
```

Builds never flash hardware. Nordic output is `build/nrf54l15-host-reliability/`.
STM32 slot outputs are under
`firmware/stm32u083/application/build/release-reliability-slot0/` and
`firmware/stm32u083/application/build/release-reliability-slot1/`.
Each STM32 slot must be signed for its own address with a key trusted by the bootloader.

For a fresh board, follow [initial installation](docs/FIRMWARE_UPDATES.md).
For an existing signed installation, reuse its signing key and update only the inactive slot.
Do not flash a standalone whole-flash application over the custom bootloader.

## Operate the system

After installing the matching host and node firmware:

```sh
scripts/mmctl device list
export NRF_PORT=/dev/serial/by-id/YOUR_NORDIC_JLINK_PORT
scripts/mmctl --port "$NRF_PORT" status
scripts/mmctl --port "$NRF_PORT" services list
scripts/mmctl --port "$NRF_PORT" node versions
scripts/mmctl --port "$NRF_PORT" sensor sample
scripts/mmctl --port "$NRF_PORT" actuator start --duration-ms 2000
scripts/mmctl --port "$NRF_PORT" node faults list
scripts/mmctl --port "$NRF_PORT" demo --samples 5 --watch-seconds 10
```

Close other users of the Nordic VCOM port; it carries binary SMP, while Nordic logs
use RTT and STM32 logs use ST-LINK VCOM. Press the STM user button during the watch.
An accepted actuator command finishes asynchronously. `firmware:unknown` is normal
until `node versions` populates the host's version cache.

Mutating recovery/update actions require `--yes`. See the [CLI guide](docs/CLI.md)
for reset, maintenance, diagnostics, events, metrics and image-management commands.

## Build, test and reuse

- [Setup and dependencies](docs/SETUP.md)
- [Architecture](docs/ARCHITECTURE.md) and [portable API boundaries](docs/API_CONTRACTS.md)
- [Wire protocol](docs/PROTOCOL.md), [host control API](docs/CONTROL_API.md),
  and [compatibility](docs/COMPATIBILITY.md)
- [Firmware updates and recovery](docs/FIRMWARE_UPDATES.md) and [bootloader design](docs/BOOTLOADER.md)
- [Testing](docs/TESTING.md), [validation evidence](docs/VALIDATION.md),
  [fault exercises](docs/FAULT_INJECTION.md) and [optional analyzer probes](hardware/LOGIC_ANALYZER.md)
- [Release reproduction](docs/FINAL_RELEASE.md) and [package extraction](docs/EXTRACTION.md)

Root CMake builds native libraries/tests. Nordic, STM32 application and STM32 bootloader
are independent cross builds. Dependency pins and licenses are in [deps/LOCK.md](deps/LOCK.md).
The removable `examples/virtual-io` component keeps demonstration behavior out of generic libraries.

Normal hardware operation, signed updates, warm-reset journal retention and a prolonged
runtime test have been reported working. Deliberate watchdog expiry, power-loss and
rollback fault cases still have separate acceptance gates; see the validation page.
Earlier engineering records are retained in the [development archive](docs/archive/development/README.md).

## License

Project-owned code and documentation are licensed under [MIT](LICENSE).
Third-party components retain their own licenses; see the dependency lock and upstream submodules.
