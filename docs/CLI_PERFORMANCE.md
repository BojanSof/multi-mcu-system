# Compatible CLI performance pass — 2026-10-25

Scope/interface freeze: host Python only, no firmware, baud, chunk size, wire
format, CLI/JSON changes or reduced safety checks. Preserve all response/job/boot
checks, deadlines, identical-request retries, readback/signature/slot/rollback
policy and conservative reconnects. No hardware update is performed by the agent.

Parallel ownership: serial agent owns smp.py, serial-performance tests and loopback
benchmark; integrator owns shared polling helper, callers, polling tests, docs and
full verification. Existing251 Python tests are the starting baseline.

## Changes

- Read currently available serial bytes, at most256; when idle block for one byte.
  A transport without in_waiting falls back to one-byte reads. Continue bounded
  incremental framing and absolute request deadlines; no non-blocking busy loop.
- Completion waits use5/10/20ms then cap50ms, clipped to the
  remaining deadline. Fresh operation/chunk starts a fresh backoff. Passive watch,
  reconnect, ROM runtime discovery and reset-only monitoring intervals stay unchanged.
- Fixed256-byte update chunks and every ACK/readback/status guard remain unchanged.

## Validation

Test short/fragmented/large replies, availability races, malformed CRC/headers,
timeouts/retries, no unsupported-property masking, bounded polling and guard failures.
Run full native/Python suite. Loopback timings are informational host observations,
not predicted MCU/firmware speedups. Record before/after, sample count and environment.

Hardware comparison: no reflash needed. Repeat the100-request/600-second runtime
test (previous p50≈329ms), and—with explicit operator approval—time valid inactive
STM updates, Nordic updates and ROM repair using the established procedures.
Compare per-stage timings and whole-job metrics, never skip checks or force slots
just for a benchmark. Hardware measurements remain pending until supplied.

## Results

Base commit30df171 plus this uncommitted host-only pass. Python3.14.8,
pyserial3.5, Arch Linux x86_64. Dependency audit: all required tools present,
optional cppcheck missing, no accessible ttyACM/ttyUSB/libusb hardware.

```sh
source ./.env.local
scripts/test/run_native.sh
.venv/bin/python tools/benchmarks/serial_read_benchmark.py --iterations 10
git diff --check
```

- PASS:16 native CTests,2 bootloader CTests,289 Python tests; dependency consistency
  and whitespace checks pass. No firmware source changed; cross builds not repeated.
- Deterministic tests cover bounded available/idle/legacy serial reads, partial and
  delayed frames, availability races, CRC recovery, timeout/identical retry semantics,
  5/10/20/50ms polling, deadline clipping and fresh-chunk reset. Wrong boot/job/offset/
  total/failure still stops immediately without advancing or replaying writes.
- Real pyserial loop:// benchmark,24-byte reply,100ms timeout,10 iterations:
  fixed256-byte read median100.162ms; available-byte read median0.032ms.
  These are host in-memory short-read costs, not target throughput or a claimed
  firmware speedup. No timing thresholds are imposed on CI.
- NOT_RUN_HARDWARE: actual update timings, live latency comparison and interruption/
  reconnect regression. Existing safety checks and passive watch intervals unchanged.

## User comparison (no rebuild/reflash)

```sh
scripts/demo.sh --port "$NRF_PORT" --samples 100 --watch-seconds 10 \
  --report build/cli-performance-after.json
```

Press the STM user button during the watch. Compare latency_ms with the earlier
~329ms median; the short watch does not constitute full acceptance. For complete
runtime revalidation use `--require-acceptance` with the default600-second watch.
For update timing, keep the JSON output from the normal update commands and compare
elapsed_ms/bytes_per_second where exposed; preserve signed image compatibility and
inactive-slot selection. Whole-job figures include erase/verification/reboot time.

## User hardware follow-up

Runtime comparison PASS:100 sensor responses, all requested runtime checks passed,
no retries/timeouts/protocol/UART errors/overflows or session changes during the run.
Median PC latency69.943ms (previous328.684ms, about79% lower); p95=115.749ms
(previous329.406ms, about65% lower), max116.604ms. Watch10s with20 asynchronous
events; this short watch is not a fresh full600-second acceptance run.

Signed STM Slot1 update PASS: confirmed_slot1, boot_state1, offset=total24072,
elapsed_ms51372, bytes_per_second468, error0, protocol0, activefalse. Whole-job
timing includes erase/signature/reboot/journal verification; no before/after
update speedup or raw transfer rate is claimed without per-stage evidence.
Nordic OTA, ROM repair and interruption/reconnect performance regressions remain
NOT_RUN_HARDWARE for this host optimization pass.
