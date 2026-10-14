# Fault runner

This CLI does not directly control either board. It prints explicit operator actions for the
Nordic onboard-button control implemented by the Phase 4 firmware:

```sh
python tools/fault-runner/fault_runner.py fault list
python tools/fault-runner/fault_runner.py fault enable lost_irq
python tools/fault-runner/fault_runner.py fault enable 8
python tools/fault-runner/fault_runner.py fault disable
python tools/fault-runner/fault_runner.py fault status
```

Deterministic native scenarios can produce analyzer-shaped evidence for documentation and tests:

```sh
python tools/fault-runner/fault_runner.py scenario duplicate_operation --mode buggy \
  --seed 42 --duration-ms 1000 --csv trace.csv --vcd trace.vcd
```

All generated data is prominently marked `SIMULATED`; it is not a hardware capture.
