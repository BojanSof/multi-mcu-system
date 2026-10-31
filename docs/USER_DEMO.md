# Two-board demonstration

This demo uses the nRF54L15-DK, NUCLEO-U083RC, independent USB supplies and the
verified jumper wiring. Virtual sensors/actuators are software simulations.

Complete [setup](SETUP.md), [wiring](../hardware/WIRING.md) and
[initial firmware installation](FIRMWARE_UPDATES.md) first. Power Nordic before
STM32; never connect supply rails. No terminal may share the Nordic SMP VCOM.

From the project root:

```sh
source ./.env.local
scripts/mmctl device list
export NRF_PORT=/dev/serial/by-id/YOUR_NORDIC_JLINK_PORT
scripts/mmctl --port "$NRF_PORT" status
scripts/mmctl --port "$NRF_PORT" services list
scripts/mmctl --port "$NRF_PORT" node versions
scripts/mmctl --port "$NRF_PORT" node recovery status
scripts/mmctl --port "$NRF_PORT" sensor sample
scripts/mmctl --port "$NRF_PORT" actuator start --duration-ms 2000
scripts/mmctl --port "$NRF_PORT" events watch
```

Expect online/healthy, a nonzero session and host boot ID, sensor integer/index,
and one actuator acceptance followed by 25/50/75 percent and completion events.
Press and release the STM user button during event watch. Stop with Ctrl-C.
`gpio_interrupts:false` is expected with the current Port 2 wiring; UART wake and
polling provide delivery. Firmware may read `unknown` before querying versions.

## Automated runtime check

```sh
scripts/demo.sh --port "$NRF_PORT" --samples 5 --watch-seconds 10 \
  --report build/demo-smoke.json
scripts/demo.sh --port "$NRF_PORT" --require-acceptance \
  --report build/demo-acceptance.json
```

The extended check uses 100 samples and a 600-second watch. Press the STM button
several times during the watch. Expect `status:PASS`; only the extended run can
report `acceptance_complete:true`. Normal demo does not update firmware or reset
boards. Explicit control exercises require `--exercise-controls --yes`.

`dropped` means old host history was overwritten; assess unread loss with cursors
and reliable-event counters. Check protocol/UART errors, retries, overflows and
session changes separately. Acceptance output includes measured PC latency and
stack snapshots, not guaranteed worst-case bounds.

## Reset and history

```sh
scripts/mmctl --port "$NRF_PORT" node faults list
scripts/mmctl --port "$NRF_PORT" --yes node reset
```

After rediscovery, repeat `node faults list` and `sensor sample`. Earlier journal
records should remain and a new boot record/session should appear. Reset does not
replay an uncertain operation. See [testing](TESTING.md) for maintenance exclusion,
watchdogs and guarded update failures, and [validation](VALIDATION.md) for observed
hardware results versus pending checks.
