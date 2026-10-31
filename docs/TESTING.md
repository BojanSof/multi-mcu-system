# Testing and hardware verification

All commands below run from the project root. Keep the verified wiring and Nordic
3.3 V I/O configuration; power Nordic first. Close other Nordic VCOM users.
No new wires, protection changes or BOOT0 jumpers are needed. Native tests are not
hardware evidence. A suite reports each measured check separately and leaves
manual scenarios `NOT_RUN_HARDWARE` until the actual procedure has been performed.

## Preparation

Follow [setup](SETUP.md), [firmware installation](FIRMWARE_UPDATES.md) and the
[operator demo](USER_DEMO.md) before these checks. Use the current `reliability`
profiles and signed STM slot images. Preserve an existing bootloader's trusted key;
first-install and subsequent-update procedures are deliberately separate.

```sh
source ./.env.local
scripts/mmctl device list
export NRF_PORT=/dev/serial/by-id/YOUR_NORDIC_JLINK_PORT
scripts/mmctl --port "$NRF_PORT" status
scripts/mmctl --port "$NRF_PORT" node recovery status
scripts/mmctl --port "$NRF_PORT" node faults list
```

Expect online, nonzero host boot ID, healthy state and an active watchdog.
`gpio_interrupts:false` is expected on the verified Port 2 pins: UART wake and
10 ms polling remain available. It is not a failed electrical IRQ signal.

## Native checks and normal hardware observation

```sh
scripts/test/run_native.sh
scripts/test/reliability.sh
scripts/test/reliability.sh --run-hardware --electrical-verified --port "$NRF_PORT" \
  --report build/reliability/checks-001.json
scripts/demo.sh --port "$NRF_PORT" --require-acceptance \
  --report build/reliability/runtime-001.json
```

The first reliability invocation opens no devices and reports `NOT_RUN_HARDWARE`.
The hardware runner offers `--suite startup|compatibility|watchdog|recovery|persistence|events`
or `all` (the default). It reads actual status, service/version information, journal
and watchdog state. The events suite executes a five-sample/ten-second demonstration
and checks event-exhaustion/button-loss counters did not increase. Press/release the
STM user button during event watches. Use fresh evidence paths; existing reports
are never overwritten. `PASS` on individual read checks is not proof of manual
stall, power interruption or incompatible-image tests; overall incomplete suites
remain `NOT_RUN_HARDWARE`.

## Manual scenarios and expected results

| Scenario | Procedure | Expected evidence |
|---|---|---|
| Delayed node | Hold STM RESET, boot Nordic, then release RESET | No phantom operation; node discovers after release |
| Independent node reset | Reset STM while watching status/events | Pending work fails once; fresh session and rediscovery |
| Host reset | Reset Nordic during a finite virtual actuator operation | Host boot ID changes; no automatic actuator replay |
| Node watchdog | Development build: inject the documented task stall | STM watchdog reset cause, fresh session, recovery |
| Host watchdog | Debugger stop a monitored task, leaving watchdog running | Nordic resets; reset cause reflects watchdog |
| Automatic recovery | Development build: suppress node responses | Bounded discovery/reset attempts; failed latch after budget |
| Recovery exclusion | Enter maintenance/shutdown, then observe beyond recovery deadlines | No automatic reset; intentional mode remains separate from failure |
| Persistence | Save `node faults list`, reset STM and read again; repeat after controlled power cycle | Earlier committed record remains, new reset summary appears |
| Compatibility | Repeat with sensor-only, sensor+actuator, legacy, and incompatible-service builds | Available compatible services work; incompatible sensor rejected |
| Reliable IRQ | Development injection: short/missed IRQ and lost event ACK | Poll fallback/deduplication; no duplicate completion; counted retries |

The reliability profile pauses Nordic's watchdog while the CPU is debugger-halted.
To test the feed gate with a debugger, briefly halt, set the debug symbol
`HostReliability_Started` to false, then resume execution: the already-started
hardware watchdog should reset Nordic after approximately 30 seconds without feed.
Use the reliability ELF, save reset-cause evidence and reset afterward to restore
normal initialization. Merely leaving the CPU halted does not test this watchdog.
Debugger halt behavior depends on watchdog/debug configuration. A halt with a
paused watchdog is not a watchdog-reset test. Do not change production protections
or option bytes. Use development fault profiles only where the injection is
supported; release `UNSUPPORTED` is not successful fault reproduction.

After deliberately exhausting automatic recovery, restore the node and clear the
latch explicitly. Clearing does not replay the failed operation:

```sh
scripts/mmctl --port "$NRF_PORT" node recovery status
scripts/mmctl --port "$NRF_PORT" --yes node recovery clear
scripts/mmctl --port "$NRF_PORT" sensor sample
```

`scripts/mmctl --port "$NRF_PORT" --yes node faults clear` logically clears fault history; save
evidence before using it. Routine sensor traffic must not generate journal writes.

## Guarded update interruption

Set `STM_PUBLIC_KEY` and `STM_PRIVATE_KEY` to your existing trusted PEM files when
opening a new shell; do not generate replacement keys. Use a validated release-v2 manifest containing the matching-key bootloader rescue
binary, both signed slot packages and Nordic signed image. The existing
[firmware update procedure](FIRMWARE_UPDATES.md) and
[release bundle documentation](../tools/cli/README.md) describe preparation.
Before authorizing erase, independently prove SWD/ROM rescue and record the ST-LINK
serial. Enumeration in the runner is only a presence check, not proof of rescue.

```sh
STM32_Programmer_CLI -l stlink
scripts/test/reliability.sh --suite interruption --run-hardware \
  --electrical-verified --port "$NRF_PORT" --destructive --yes \
  --rescue-verified --swd-serial YOUR_STLINK_SERIAL \
  --manifest build/your-verified-release/release.json --public-key "$STM_PUBLIC_KEY" \
  --slot 0 --report build/reliability/interruption-001.json
```

Replace `--slot 0` with the inactive slot. This erases that slot, writes one
256-byte chunk, then explicitly aborts **before finalize/activation**. It verifies
the confirmed-slot identity and runtime rediscovery. It never sends `finish`,
changes protection, clears boot metadata or enters ROM automatically. Serial loss
does not authorize reconnection/replay. Inspect `stm status` before taking further
action after uncertainty.

This is a partial-transfer/abort test, not electrical power-loss proof. Manual
power interruption at selected offsets and commit boundaries remains a separate
test with verified rescue. Do not use repeated uncontrolled power cycling as a
substitute for a bounded test plan.

Build a deliberate non-confirming trial **only for the inactive slot**, for example
when Slot0 is currently confirmed:

```sh
cmake --preset debug-trial-failure-slot1 -S firmware/stm32u083/application
cmake --build firmware/stm32u083/application/build/debug-trial-failure-slot1 --parallel
.venv/bin/python tools/firmware/sign.py \
  --private-key "$STM_PRIVATE_KEY" --slot 1 --version 0.6.0 \
  --build-id 0xd01 --wire 1.0 --management 1.0 \
  firmware/stm32u083/application/build/debug-trial-failure-slot1/multi_mcu_stm32_application.bin \
  build/reliability/trial-failure-slot1.mmfw
scripts/mmctl --port "$NRF_PORT" --yes stm update \
  build/reliability/trial-failure-slot1.mmfw --public-key "$STM_PUBLIC_KEY"
scripts/mmctl --port "$NRF_PORT" stm status
```

Expect `rolled_back` and the original confirmed slot. The development-only candidate
resets before confirmation; release builds reject that option. This command is
explicitly destructive to the inactive slot and requires the same rescue checks.
Never install the failing trial over the confirmed image with SWD.

The `trial-rollback` suite uses the same destructive prerequisites but remains a
guided checkpoint: install the previously verified, correctly signed **non-confirming
trial application** in the inactive slot using the [signed-update procedure](FIRMWARE_UPDATES.md); observe
trial attempts, return to the previous confirmed slot and runtime recovery. A normal
application that confirms successfully does not test rollback. The runner does not
invent or automatically install a failing candidate.

## Two useful failure walkthroughs

**Lost acceptance:** on a development node enable dropped-acceptance injection,
start one virtual actuator, and record the operation/execution ID. Observe the host
retry and the same execution ID, followed by progress/completion. Record one
execution, not merely one printed response. Disable injection afterward. This
demonstrates bounded duplicate suppression, not exactly-once execution across
arbitrary power loss.

**Node stall:** save current session, fault history and recovery counters. Trigger
the documented development task stall. Collect the STM watchdog reset reason, new
session and host rediscovery. Any pending operation must terminate once; restarting
the node must not automatically issue it again. Save the new journal records and
verify another sensor request succeeds. If recovery latches after repeated failure,
restore healthy firmware before explicitly clearing it.

## Optional logic-analyzer evidence

Probe host TX, node TX and STM DEBUG or IRQ_N, with common GND, using the exact
[pinout](../hardware/PINOUT.md). Normal UART is 115200 8N1; factory recovery is 8E1.
Keep raw captures outside tracked source until reviewed. Attach a manifest such as:

```json
{
  "provenance": "real_hardware",
  "scenario": "dropped_acceptance_one_execution",
  "git_commit": "REPLACE_WITH_ACTUAL_REVISION",
  "board_revisions": ["PCA10156 1.0.0", "MB1932-U083RC-B01"],
  "channels": ["Nordic TX", "STM TX", "STM DEBUG"],
  "decoder": "UART 115200 8N1",
  "capture_file": "actual-capture.sr",
  "observations": "REPLACE_WITH_MEASURED_TIMES_AND_OPERATION_ID"
}
```

This is a template, not captured evidence. Simulated traces must use provenance
`simulated` and cannot satisfy hardware acceptance. Do not infer electrical power
or voltage behavior from a firmware debug marker.


## Compatibility matrix

| Host | STM application | Delivery | Fault history |
|---|---|---|---|
| Unacknowledged-event host | Legacy | Legacy unsolicited events | Unsupported |
| Unacknowledged-event host | Reliability | Legacy after HELLO/LIST negotiation | Available by generic lifecycle calls |
| Reliability | Legacy | Legacy; no event extension sent | Unsupported |
| Reliability | Reliability | Numbered events, ACK, level-held IRQ | Lifecycle1.1 journal |

Both sensor-only and sensor+actuator personalities preserve the same negotiation.
Unknown wire/management major versions must not become online. Incompatible optional
sensor services do not prevent using compatible lifecycle/diagnostic services.
When another legacy host has consumed events, subsequent reliable negotiation may
report a cursor gap; it cannot promise exactly-once delivery across multiple hosts.

A rollback trial is deliberately unsuccessful: `stm update` may exit nonzero while
reporting `rolled_back`. Inspect `stm status` and verify the original application
returns online rather than treating the failed candidate as a successful update.


## Recorded results

Normal signed updates, service/event operation, a prolonged 100-sample/600-second
run, and warm-reset fault-history retention were reported successful on the boards.
Deliberate watchdog expiry, exhausted automatic recovery, power-cycle retention,
physical update interruption and failing-trial rollback still need their own evidence.
See [validation status](VALIDATION.md). A successful normal run does not establish
all failure boundaries, and a missing capture is never replaced by a simulated one.
