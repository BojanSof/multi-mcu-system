# nRF54L15 host

Independent Zephyr/NCS application for node discovery, bounded service requests,
async events, supervision, SMP operator control and firmware-update orchestration.
Use the pinned SDK/toolchain from [setup](../../docs/SETUP.md). The complete build
and flash commands run from the repository root:

```sh
source ./.env.local
scripts/build/build_nordic.sh --profile reliability
.venv/bin/west flash -d build/nrf54l15-host-reliability
scripts/mmctl device list
```

The reliability profile includes MCUboot, serial MCUmgr, the operator API, signed
STM updates, explicit factory-ROM repair, metrics and a 30-second host watchdog.
Historical build interfaces remain available unchanged. `default` is a standalone
bring-up/demo host, not the full operator configuration. Keep earlier installations
and trust roots intact when selecting a different build.

J-Link VCOM is reserved for SMP; application logs use RTT. The inter-MCU UART is
115200 8N1, changed exclusively to 8E1 for ROM repair. One main owner serializes
runtime, reset/update and recovery operations; workqueue handlers use bounded
mailboxes/snapshots. Main and system-workqueue progress must both advance to feed
the watchdog, independently of whether the node is online.

READY/IRQ are on Port 2, which has no GPIOTE instance in this SDK. GPIO reads and
level-held IRQ remain valid; UART RX wake plus a 10 ms polling fallback provide
processing. Unsupported optional GPIO interrupts produce warnings, not fatal startup
failure. `gpio_interrupts:false` is expected; zero `host_boot_id` indicates startup
has not completed. No alternate GPIOTE mapping or rewiring is assumed.

[CLI commands](../../docs/CLI.md), [update procedures](../../docs/FIRMWARE_UPDATES.md),
[testing](../../docs/TESTING.md) and [architecture](../../docs/ARCHITECTURE.md) are
canonical. Keep the verified [pinout](../../hardware/PINOUT.md), Nordic-first power
order and open-drain reset restrictions. The host never automatically enters ROM
or repairs firmware solely because runtime communication failed.
