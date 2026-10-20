# USB operator CLI — Phase 9

Run every command from the repository root. Current STM application/slot firmware
already provides the services; only Nordic needs the new control profile. Keep the
five existing application wires. Power Nordic before STM as in `hardware/WIRING.md`.

```sh
source ./.env.local
.venv/bin/python -m pip install -r tools/requirements.lock
scripts/build/build_nordic.sh --profile phase9
.venv/bin/west flash -d build/nrf54l15-host-phase9
scripts/mmctl device list
```

Close the Nordic log terminal, then select its J-Link VCOM (prefer a stable
`/dev/serial/by-id/` path). Do not choose the STM ST-LINK logging VCOM.

```sh
NRF_PORT=/dev/ttyACM2  # replace with the Nordic port from device list
scripts/mmctl --port "$NRF_PORT" status
scripts/mmctl --port "$NRF_PORT" services list
scripts/mmctl --port "$NRF_PORT" node versions
scripts/mmctl --port "$NRF_PORT" sensor sample
scripts/mmctl --port "$NRF_PORT" actuator start --duration-ms 2000
scripts/mmctl --port "$NRF_PORT" events watch
```

Expected: status `online`, services 0x0001/2/3 and 0x1001/2 for personality B,
sensor signed integer/sample index, actuator execution ID, then retained progress
25/50/75 percent and completion in event watch. Stop watches with Ctrl-C. Event
history retains 16 records; `dropped` increases when oldest records are overwritten.
The CLI shows CBOR byte payloads as hex and adds decoded fields for known services.
`--json` produces one JSON object per line. Phase 9 Nordic logs use J-Link RTT;
the VCOM carries SMP only. `--logs` can forward legacy boot/console lines if present.
`firmware` reads `unknown` until the lifecycle versions service has been queried.

```sh
scripts/mmctl --port "$NRF_PORT" diagnostics status
```

Fault injection requires an STM Debug build with `MULTI_MCU_ENABLE_DEV_FAULTS=ON`.
Release and signed release-slot profiles intentionally reject `diagnostics inject`
with node status 6 (unsupported command); skip this exercise on those profiles.
Do not replace a signed A/B application with a standalone Debug image. With a
compatible development image installed, run:

```sh
scripts/mmctl --port "$NRF_PORT" diagnostics inject 3 --yes
scripts/mmctl --port "$NRF_PORT" sensor sample
scripts/mmctl --port "$NRF_PORT" diagnostics clear --yes
scripts/mmctl --port "$NRF_PORT" diagnostics records
scripts/mmctl --port "$NRF_PORT" node maintenance enter --yes
scripts/mmctl --port "$NRF_PORT" node maintenance exit --yes
```

Injection 3 delays responses. `diagnostics records` drains up to 32 node diagnostic
records and verifies the queue is empty. General service calls use numeric IDs and hex payloads, e.g.
read lifecycle versions (service 3 command 4):

```sh
scripts/mmctl --port "$NRF_PORT" service call 3 4 --yes
scripts/mmctl --port "$NRF_PORT" status --json
```

Service calls return admission/request/operation IDs, followed by their node result.
Actuator completion is asynchronous and appears in event history. The host has one
pending request at a time: `busy` is an explicit admission failure; retry later.
Do not blindly rerun an uncertain actuator operation after a cable disconnect.
Within the same host boot the CLI retries the same request ID; a changed host boot
ID terminates the operation instead of replaying it.

## Nordic image management

Use a distinct signed MCUboot `.bin`, built for the existing Phase 6 partition map.
The version/content/hash must differ from the active image. `dfu_application.zip`
is not accepted by this CLI. The current profile uses the SDK development signing
key, as documented in `FIRMWARE_UPDATES.md`.

```sh
scripts/mmctl --port "$NRF_PORT" host image list
scripts/mmctl --port "$NRF_PORT" host image upload path/to/zephyr.signed.bin --yes
scripts/mmctl --port "$NRF_PORT" host image list
scripts/mmctl --port "$NRF_PORT" host image test SECONDARY_HASH --yes
scripts/mmctl --port "$NRF_PORT" host image reset --yes
scripts/mmctl --port "$NRF_PORT" host image list
```

The application confirms its trial after compatible node discovery. Inspect the
active/confirmed flags after reboot. An uploaded firmware that omits Phase 9 control
can still support standard image management but will no longer answer control group 64.
The `host image update` convenience command performs upload/test/reset and reads back
image state. Manual `host image confirm` bypasses normal self-test confirmation and
is only a diagnostic operation.

## Verification and limitations

```sh
source ./.env.local
scripts/test/run_native.sh
```

Serial codec/CLI tests use fake UARTs, not attached boards. Live runtime and OTA tests
are `NOT_RUN_HARDWARE` until performed on the real DK/node. Physical STM reset,
PC-streamed STM A/B update and factory-ROM repair are implemented in subsequent
Phases 10–11; their CLI entries report unavailable in Phase 9.
