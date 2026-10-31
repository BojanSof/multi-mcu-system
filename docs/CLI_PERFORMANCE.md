# CLI transport performance

The PC client reads available serial bytes instead of waiting to fill a 256-byte
read. When idle it waits for one byte; transports without `in_waiting` use one-byte
reads. Parsing remains incremental with absolute request deadlines.

Operation/chunk completion polling backs off through 5/10/20/50 ms, clipped to the
remaining deadline. Each new operation/chunk starts a fresh backoff. Passive watches
and reconnect intervals are unchanged. Chunk size, baud, signatures, readback,
slot guards and conservative reconnect behavior are not weakened for speed.

## Measurements

A user hardware comparison with 100 sensor requests measured median PC latency
falling from about 329 ms to 70 ms after the serial-read/polling change. A later
reliability smoke measured about 86 ms median. These are different runs/profiles;
neither is raw UART latency or a universal performance guarantee.

A 24,072-byte signed STM update completed in 51.372 seconds (468 bytes/s whole-job
rate). That includes entry, erase, signature checking, reboots and journal/runtime
verification. No per-stage before/after update speedup was established.

The in-memory pyserial loopback benchmark measured roughly 100 ms for a fixed-size
read versus 0.032 ms for available-byte reads with a 24-byte reply and 100 ms timeout.
Those are host read costs, not board timings.

## Reproduce

Run from the project root after [setup](SETUP.md):

```sh
scripts/test/run_native.sh
.venv/bin/python tools/benchmarks/serial_read_benchmark.py --iterations 10
scripts/demo.sh --port "$NRF_PORT" --samples 100 --watch-seconds 10 \
  --report build/latency.json
```

For extended acceptance, use `--require-acceptance` and the default 600-second watch.
Compare distributions, errors and loss counters with the same versions and workload.
Keep whole-job firmware timing separate from transfer-only throughput. Deliberate
interruption and reconnect scenarios require the [guarded testing procedure](TESTING.md).
