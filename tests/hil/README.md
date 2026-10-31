# Hardware-in-the-loop checks

The recommended operator harness is `scripts/test/reliability.sh`, run from the
repository root. It opens no devices by default and reports `NOT_RUN_HARDWARE`:

```sh
scripts/test/reliability.sh
scripts/test/reliability.sh --run-hardware --electrical-verified --port "$NRF_PORT" \
  --report build/reliability-checks.json
```

See [testing](../../docs/TESTING.md) for named suites, guard prerequisites and manual
watchdog/power/interruption evidence. Passing read checks does not convert missing
manual tests to PASS. A short demo is not extended runtime acceptance.

The legacy `run_hil.py` observer remains available for existing serial-log scenarios;
use its `--help` and `matrix.json` for identifiers. It observes logs only and never
builds, flashes, resets or changes options. Current Nordic logs use RTT rather than
its SMP VCOM, so do not point a text observer at that management port. Fixture-based
unit tests and generated traces are simulated and cannot establish hardware results.
