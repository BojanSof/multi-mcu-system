# Logic-analyzer connections and fault exercises

A logic analyzer is optional. Connect its ground to the common board ground and use:

| Channel | Probe point | Decode |
|---|---|---|
| CH0 | nRF P1 pin 14 / STM PA10 | UART 115200 8N1, host to node |
| CH1 | STM CN10 pin 21 / nRF P1.12 | UART 115200 8N1, node to host |
| CH2 | STM CN10 pin 27 (PB4) | active-low IRQ level |

Alternative CH2 during timing work is STM CN10 pin 31 (PB3 `NODE_DEBUG`). It is a firmware event
marker, not a voltage or power-safety measurement. Use the `0x00` frame delimiters to locate COBS
packets. CRC-valid decoded traffic is authoritative; UART bytes alone do not prove application
acceptance.

Never label an illustrative or simulated trace as a hardware capture. Record analyzer model, sample
rate, threshold, firmware Git revision and whether the trace is real before storing any capture.

Use at least 1 MS/s (4 MS/s or higher preferred), digital thresholds compatible with the verified
3.3 V I/O, and a common ground. CH0/CH1 UART decode is 115200 8N1, idle high, LSB first. Never
connect analyzer power to either board. For exercises using `NODE_DEBUG`, move only CH2 from PB4
to PB3; do not change the MCU wiring.

Development injection is compiled only into compatible STM Debug builds. For the
USB operator system select a signed Debug slot and use `scripts/mmctl diagnostics`
as described in [fault injection](../docs/FAULT_INJECTION.md). Resetting STM clears
all runtime injections. Injection 1 (early READY) requires its separate analyzer-only STM build option because
runtime state deliberately does not persist across reset.

## Tutorial A — missed IRQ edge

Injection: `SHORT_IRQ` (2). Probe CH0 host TX, CH1 node TX, and CH2 PB4 `IRQ_N`.

- Bug reproduction: the node releases IRQ immediately after queueing its event, producing a short
  active-low pulse. An edge-only host design could miss it. The Nordic log must still show the
  event because the implemented host polls and drains UART independently of IRQ.
- Correct behavior: with injection disabled, IRQ remains low across the queued event interval and
  is released after acknowledgment in reliable-event mode. UART event framing, not the edge alone, is authoritative.
- Expected markers: CH2 falls before the first event byte on CH1. Bug mode rises almost
  immediately; reliable mode remains low while an event is pending acknowledgment; legacy delivery may release it on a later transport pass.
- Regression: `fault_runner.py scenario lost_irq --mode buggy|fixed` emits deterministic
  `SIMULATED` CSV/VCD evidence; host firmware always drains UART in either case.
- HIL: enable ID 2, press/release the STM button once, verify the short CH2 pulse and the CLI
  button event. Disable/reset and repeat to observe the held level.

## Tutorial B — dropped acceptance and duplicate operation

Injection: `DROP_ACCEPTANCE` (4). Probe CH0 host TX, CH1 node TX, and CH2 PB3 `NODE_DEBUG`.

- Bug reproduction: the node accepts one actuator START and deliberately drops its first response.
  The host times out after 500 ms and retransmits the exact sequence, operation ID and payload.
  A non-idempotent implementation would execute twice.
- Correct behavior: the virtual actuator's bounded operation history returns its cached execution
  number. `NODE_DEBUG` marks the single accepted modeled-output transition; only one progress and
  completion sequence is emitted.
- Expected markers: two equivalent request frames appear on CH0 about 500 ms apart, one DEBUG
  marker pulse appears on CH2, and the retry acceptance appears on CH1.
- Regression: the portable virtual-I/O test asserts that duplicate START does not increment the
  execution or transition counters. The simulated `duplicate_operation` trace contrasts the
  intentionally buggy double execution with the fixed cache.
- HIL: enable ID 4, start one actuator through the CLI, and verify a timeout/retry diagnostic, exactly one
  execution number and one completion sequence.

## Tutorial C — virtual output ordering

Injection: `EARLY_VIRTUAL_OUTPUT` (7). Probe CH0 host TX, CH1 node TX, and CH2 PB3 `NODE_DEBUG`.
The terms prerequisite/output describe software state only, never electrical power.

- Bug reproduction: the portable model records `output_enable_order < prerequisite_order`, raises
  its violation counter and emits a warning diagnostic with detail bit 1.
- Correct behavior: prerequisite order precedes output-enable order, violation count remains zero,
  and the diagnostic detail is bit 0. The DEBUG marker identifies the evaluated transition.
- Expected markers: the START request is visible on CH0, the DEBUG marker follows on CH2, and the
  structured diagnostic/event follows on CH1. One GPIO cannot expose both internal transitions;
  the typed diagnostic record supplies their interpreted order.
- Regression: native `virtual_io.behavior` tests both orders and verifies a duplicate operation
  creates no second transition. `fault_runner.py scenario early_output` exports an illustrative
  `SIMULATED` ordering trace.
- HIL: capture once with faults off and once with ID 7, then compare diagnostic detail/severity and
  ensure the actuator still completes within its bounded virtual duration.

## Additional transport injections

- `DELAY_RESPONSE` (3): one clean enable response is followed by responses delayed 650 ms, beyond
  the 500 ms host timeout. Expect bounded retries and later recovery.
- `CORRUPT_UART` (5): one clean enable response is followed by one encoded node frame with a flipped
  byte. Expect CRC/framing rejection, a counter increment and resynchronization at `0x00`.
- `TRUNCATE_UART` (6): one clean enable response is followed by half of one encoded frame. Expect a
  partial stream, timeout/retry and recovery at the next delimiter.
- `TASK_STALL` (8): the node communication task pauses for six seconds, long enough to
  prevent progress-based IWDG refresh. Expect a watchdog reset, new session and rediscovery.

The generator at `tools/fault-runner/fault_runner.py` can create analyzer-like CSV/VCD for all
transport exercises when no analyzer is available. These outputs are illustrative simulations and
must never be renamed or reported as real captures.
