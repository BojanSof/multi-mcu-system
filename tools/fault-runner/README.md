# Fault runner and simulated traces

This helper prints operator guidance; it does not directly control either board.
Use the current [fault-injection commands](../../docs/FAULT_INJECTION.md) for a
signed Debug node and the [hardware guide](../../hardware/LOGIC_ANALYZER.md) for probes.

```sh
.venv/bin/python tools/fault-runner/fault_runner.py fault list
mkdir -p build/traces
.venv/bin/python tools/fault-runner/fault_runner.py scenario duplicate_operation \
  --mode buggy --seed 42 --duration-ms 1000 \
  --csv build/traces/duplicate.csv --vcd build/traces/duplicate.vcd
```

All commands run from the repository root. Generated CSV/VCD data is labeled
`SIMULATED`; it is not a hardware capture or hardware acceptance result.
