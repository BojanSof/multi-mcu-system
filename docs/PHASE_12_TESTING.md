# Phase 12 — final operator acceptance

All commands run from the project root. Keep the verified Phase11 wiring and
existing signed-A/B STM bootloader/application; no new wires or option-byte changes.
Power Nordic first. Close other users of Nordic VCOM. The demo never flashes firmware.

## Build, flash Nordic and smoke test

```sh
source ./.env.local
scripts/build/build_nordic.sh --profile phase12
.venv/bin/west flash -d build/nrf54l15-host-phase12
scripts/mmctl device list
export NRF_PORT=/dev/serial/by-id/YOUR_NORDIC_JLINK_PORT
scripts/mmctl --port "$NRF_PORT" status
scripts/mmctl --port "$NRF_PORT" metrics
scripts/demo.sh --port "$NRF_PORT" --samples 5 --watch-seconds 10 --report build/phase12-smoke.json
```

Replace the port with the actual discovered Nordic VCOM. Status must be online
and metrics:true. Smoke exercises lifecycle/version read, sensor requests and
one simulated actuator execution through completion. Press the STM user button
during the watch to generate asynchronous events. No button events means
NOT_OBSERVED, not evidence that no events were lost. Short smoke cannot satisfy
full acceptance, even when its requested checks pass.

## Full 100-request / ten-minute acceptance

```sh
scripts/demo.sh --port "$NRF_PORT" --require-acceptance --report build/phase12-runtime.json
```

Default100 sequential sensor reads, actuator progress25/50/75 and completion, then
600-second event watch. **Press/release the STM user button several times during
the watch.** Reports must show PASS and acceptance_complete:true, correct unique
operations/sensor indices, no unread event gaps or transport-error deltas.
The retained-history overwrite count may rise even if every record was read;
that counter alone is not event loss. PC latency includes SMP/polling overhead;
firmware latency measures runtime UART request/reply including retries.

Optional operator controls (maintenance enter/exit, supported development fault
exercise with cleanup, remote reset/fresh-session validation):

```sh
scripts/demo.sh --port "$NRF_PORT" --samples 5 --watch-seconds 10 \
  --exercise-controls --yes --report build/phase12-controls.json
```

Release STM builds can report fault injection NOT_SUPPORTED; no protection or
real power switching is exercised. Reset is explicit in this optional command.
No mutating operations are replayed across disconnect/host/node session changes.

## HIL matrix and remaining destructive checks

```sh
# Safe no-op: every hardware scenario remains NOT_RUN_HARDWARE.
.venv/bin/python tests/hil/acceptance_matrix.py

# Actual runtime acceptance only; choose fresh report paths.
.venv/bin/python tests/hil/acceptance_matrix.py --run-hardware \
  --electrical-verified --port "$NRF_PORT" \
  --runtime-report build/phase12-hil-runtime.json --report build/phase12-hil-matrix.json
```

The matrix does not automatically flash/reset/erase. Nordic update, both STM slots,
ROM repair, interruption, rollback and unplug/reconnect remain separate operator
evidence, not inferred PASS. Reuse [Phase10](PHASE_10_TESTING.md) and
[Phase11](PHASE_11_TESTING.md) procedures and known matching signing keys.
Nordic explicit reset/update reconnect uses the recorded USB identity, never an
unrelated singleton port. Ambiguous identity stops read-only verification.

## Build resources and reproducible release

```sh
.venv/bin/python tools/firmware/resource_report.py \
  --elf host=build/nrf54l15-host-phase12/nrf54l15-host/zephyr/zephyr.elf \
  --elf stm-loader=firmware/stm32u083/bootloader/build/release/multi_mcu_stm32_bootloader.elf \
  --size-tool arm-none-eabi-size --stack-dir build/nrf54l15-host-phase12/nrf54l15-host \
  --output build/phase12-resources.json

# Reuse a validated release v2 bundle; choose a fresh output archive filename.
.venv/bin/python tools/firmware/release_archive.py create \
  build/phase11-release/release.json build/multi-mcu-release.zip \
  --public-key keys/stm32-dev-public.pem
.venv/bin/python tools/firmware/release_archive.py verify \
  build/multi-mcu-release.zip --public-key keys/stm32-dev-public.pem
```

The archive contains exactly manifest+four artifacts, no private key. To distribute
the new Phase12 host rather than an older bundle's host, rebuild/revalidate the v2
bundle using the Phase12 zephyr.signed.bin first; see [release instructions](FINAL_RELEASE.md).
Static stack frames are not call-chain bounds or target high-water measurements.
`metrics` reports live stack scan availability/free bytes on actual firmware.
