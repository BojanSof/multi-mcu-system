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
| 8 | `TASK_STALL` | Pauses the communication task once for a six seconds |

Use signed `debug-reliability-slot0` or `debug-reliability-slot1` firmware for an
existing A/B installation. Sign for the inactive slot and update normally; never
replace the loader with a standalone Debug image. ID 1 needs its explicit early-
READY boot option; enabling it after boot cannot recreate a startup race.

```sh
scripts/mmctl --port "$NRF_PORT" --yes diagnostics inject 4
scripts/mmctl --port "$NRF_PORT" actuator start --duration-ms 2000
scripts/mmctl --port "$NRF_PORT" diagnostics records
scripts/mmctl --port "$NRF_PORT" --yes diagnostics clear
```

For dropped acceptance, compare execution/operation IDs to prove one execution,
not merely one printed response. For stall injection, expect watchdog reset and
rediscovery; it must not replay a failed command. Release rejection is not proof
of fault reproduction. See [testing](TESTING.md) for guarded hardware procedures.

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
  --csv build/duplicate.csv --vcd build/duplicate.vcd
```

Every generated row and VCD header is marked `SIMULATED`. See `hardware/LOGIC_ANALYZER.md` for the
three mandatory analyzer tutorials, exact probe locations and the distinction between UART bytes,
the DEBUG marker and application state.

Collect STM ST-LINK logs and exported Nordic RTT logs with
[the log collector](../tools/log-collector/README.md). Do not open the Nordic SMP
VCOM as a text log stream. Receipt timestamps are correlation aids, not clock
synchronization. Raw captures belong in ignored build/private directories until
reviewed for publication.
