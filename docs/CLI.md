# Operator CLI

`scripts/mmctl` controls the Nordic host through SMP v2 over its J-Link virtual
COM port. Use the `reliability` host build for the complete interface. Complete
[setup](SETUP.md), [wiring](../hardware/WIRING.md) and
[firmware installation](FIRMWARE_UPDATES.md) first.

Run all commands from the project root. Close other applications using the Nordic
VCOM; the CLI owns that port. Nordic application logs use RTT, while STM32 logs
use the separate ST-LINK VCOM. Identify ports instead of assuming an ACM number:

```sh
scripts/mmctl device list
export NRF_PORT=/dev/serial/by-id/YOUR_NORDIC_JLINK_PORT
scripts/mmctl --port "$NRF_PORT" status
scripts/mmctl --port "$NRF_PORT" services list
scripts/mmctl --port "$NRF_PORT" node versions
scripts/mmctl --port "$NRF_PORT" sensor sample
scripts/mmctl --port "$NRF_PORT" actuator start --duration-ms 2000
scripts/mmctl --port "$NRF_PORT" events watch
```

If exactly one matching J-Link VCOM exists, port selection is automatic. Prefer
stable `/dev/serial/by-id/` names when several devices are attached. Use `--json`
for one JSON object per line and `--logs` to forward complete legacy log lines to
stderr. `--timeout` and `--wait-timeout` bound transport and operation-result waits.
Common options can appear before or after subcommands.

Service calls print admission/request/operation identifiers followed by the node
response. Actuator acceptance differs from completion: progress and completion
arrive as events. Event history retains 16 records; `dropped` counts overwritten
host history entries, not necessarily lost UART events. Only one runtime request
is pending at a time; `busy` is an explicit admission failure.

Status should reach `online`, with a nonzero `host_boot_id`. Firmware can read
`unknown` until `node versions` has populated the version cache. With both
reliability applications, capabilities is 63. `gpio_interrupts:false` is expected
for the verified Port 2 sideband wiring; UART wake and 10 ms polling remain active.

## Lifecycle, health and history

```sh
scripts/mmctl --port "$NRF_PORT" node status
scripts/mmctl --port "$NRF_PORT" node recovery status
scripts/mmctl --port "$NRF_PORT" node faults list
scripts/mmctl --port "$NRF_PORT" metrics
scripts/mmctl --port "$NRF_PORT" --yes node maintenance enter
scripts/mmctl --port "$NRF_PORT" --yes node maintenance exit
scripts/mmctl --port "$NRF_PORT" --yes node reset
scripts/mmctl --port "$NRF_PORT" --yes node recovery clear
scripts/mmctl --port "$NRF_PORT" --yes node faults clear
```

Maintenance and software-only shutdown quiesce virtual-I/O services; neither
switches physical power. Recovery clear releases the automatic-reset latch and
restarts discovery without replaying failed commands or entering factory recovery.
Fault-history reads are non-destructive; clear commits a logical journal tombstone.
Storage-error counters are per boot; journal overwrite counts are not PC event loss.

Mutating operations require `--yes`. Never blindly repeat an uncertain actuator
operation after disconnect. Exact retries retain request identity within the same
host boot; a changed boot ID stops replay. Nordic restart does not restore volatile
operation history.

## Diagnostics and generic calls

Fault injection requires a compatible STM32 Debug image. Release images reject
injection as unsupported. Never overwrite the custom bootloader with a standalone
Debug image; use a signed Debug slot image as described in [testing](TESTING.md).

```sh
scripts/mmctl --port "$NRF_PORT" diagnostics status
scripts/mmctl --port "$NRF_PORT" --yes diagnostics inject 3
scripts/mmctl --port "$NRF_PORT" --yes diagnostics clear
scripts/mmctl --port "$NRF_PORT" diagnostics records
scripts/mmctl --port "$NRF_PORT" --yes service call 3 4
scripts/mmctl --port "$NRF_PORT" --yes service call 3 12
```

`diagnostics clear` disables injections; it does not clear persistent history.
`diagnostics records` drains the bounded volatile diagnostic queue. Generic calls
use numeric service/command IDs and optional `--data` hex bytes, and require
`--yes` because their effects cannot be inferred generically. Service 3 command 4
reads versions; command 12 reads reliable-event queue counters.

## Firmware operations

```sh
scripts/mmctl --port "$NRF_PORT" host image list
scripts/mmctl --port "$NRF_PORT" --yes host image update path/to/zephyr.signed.bin
scripts/mmctl --port "$NRF_PORT" stm status
scripts/mmctl --port "$NRF_PORT" stm image inspect path/to/slot1.mmfw --public-key "$STM_PUBLIC_KEY"
scripts/mmctl --port "$NRF_PORT" --yes stm update path/to/slot1.mmfw --public-key "$STM_PUBLIC_KEY"
scripts/mmctl --port "$NRF_PORT" --yes stm abort
```

Nordic image operations need a signed MCUboot `.bin`, not `dfu_application.zip`.
A candidate must differ from the active image. Manual image confirmation bypasses
normal application checks and is a diagnostic operation, not the release procedure.

STM packages are verified offline against the supplied trusted P-256 key before
serial opens. Choose the inactive slot and preserve the installed bootloader's key.
Initial idle `stm status` may show slot 255 (unknown); it is populated by an update
query, not a persistent database. See [update and recovery](FIRMWARE_UPDATES.md) for
installation, slot selection, confirmation and explicit factory-ROM repair.

After a timeout/disconnect, inspect `stm status` rather than replaying the job.
Before commit, abort can return to the confirmed image. After commit starts,
verification continues on Nordic even if the PC disconnects. There is no automatic
mutating reconnect across a host restart.

The complete binary control contract is in [CONTROL_API.md](CONTROL_API.md).
