# Development fault injection

Fault injection is a Debug-build teaching facility, not a production feature. STM runtime state
always starts disabled and clears on reset. Release-like builds omit the enabling behavior. All
virtual outputs and prerequisites are software state; they do not control or measure power.

| ID | Name | One-shot/bounded behavior |
|---:|---|---|
| 1 | `EARLY_READY` | Separate boot profile asserts READY 50 ms before UART setup |
| 2 | `SHORT_IRQ` | Events use an intentionally short IRQ pulse; UART polling still recovers |
| 3 | `DELAY_RESPONSE` | Responses delay 650 ms, beyond the 500 ms host timeout |
| 4 | `DROP_ACCEPTANCE` | Drops exactly one successful actuator START acceptance |
| 5 | `CORRUPT_UART` | Corrupts one encoded response after the clean enable response |
| 6 | `TRUNCATE_UART` | Sends half of one encoded response after the clean enable response |
| 7 | `EARLY_VIRTUAL_OUTPUT` | Reverses modeled prerequisite/output ordering |
| 8 | `TASK_STALL` | Pauses the communication task once for a bounded 1.5 seconds |

On the Nordic DK, a short button press retains the normal Phase 3 service action. A press held for
at least 1.5 seconds cycles `OFF -> 1 -> ... -> 8 -> OFF`. Each selection disables the previous
mask before enabling the new one and logs the returned status. ID 1 cannot alter the current boot;
use the STM `debug-early-ready` preset for that analyzer lesson.

The operator helper never claims direct hardware control:

```sh
.venv/bin/python tools/fault-runner/fault_runner.py fault list
.venv/bin/python tools/fault-runner/fault_runner.py fault enable duplicate_operation
.venv/bin/python tools/fault-runner/fault_runner.py fault status
.venv/bin/python tools/fault-runner/fault_runner.py fault disable
```

Generate deterministic illustrative evidence without hardware:

```sh
.venv/bin/python tools/fault-runner/fault_runner.py \
  scenario duplicate_operation --mode fixed --seed 7 --duration-ms 1000 \
  --csv /tmp/duplicate.csv --vcd /tmp/duplicate.vcd
```

Every generated row and VCD header is marked `SIMULATED`. See `hardware/LOGIC_ANALYZER.md` for the
three mandatory analyzer tutorials, exact probe locations and the distinction between UART bytes,
the DEBUG marker and application state.

Collect independent console logs for a bounded interval:

```sh
.venv/bin/python tools/log-collector/log_collector.py \
  --host serial:/dev/ttyACM0 --node serial:/dev/ttyACM1 \
  --duration 30 --format jsonl --output /tmp/phase4.jsonl
```

Confirm which ACM device belongs to each board before running. The collector preserves raw bytes,
adds host receipt timestamps and labels `clock_synchronized=false`; receipt ordering does not make
the two MCU uptime clocks globally synchronized.
