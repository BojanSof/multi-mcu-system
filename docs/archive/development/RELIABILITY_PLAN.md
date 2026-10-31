# Reliability extension — approved 2026-10-25

Status: software implemented and native/build verified on 2026-10-26; hardware
acceptance pending. Reports: PHASE_REPORTS/PHASE_13.md through PHASE_16.md.
This extends, rather than replaces, Phases 1–12.
No hardware verification is implied by a software implementation or native test.

## Shared contracts and preservation gates

- Keep all existing build profiles, wire-major v1, SMP API v1, signed image schema,
  slot addresses, boot journal and ROM/SWD recovery paths. Add a `reliability`
  Nordic profile and independently selectable STM reliability option.
- Host main owns runtime/update/recovery state; SMP uses mailboxes and snapshots.
  No reset escalation or runtime traffic while another owner holds the UART.
- Add fields/commands rather than changing existing meanings. Unsupported new
  features are explicitly reported. New event delivery requires negotiation.
- No extra electronics, option-byte/protection writes, power switching, BLE,
  atomic dual-image update or production secure-boot claims.

## Milestone 13 — Watchdog and bounded recovery

- Nordic hardware watchdog timeout 30 seconds; main and system-workqueue progress
  must both advance before feed. Node offline is not a host-health failure.
  Long update/ROM transactions must remain bounded below that window.
- Retain 500 ms request timeout, three retries and two-second heartbeat. Exhaustion
  fails pending work once; rediscover for five seconds, pulse open-drain NRST for
  20 ms, allow ten seconds for discovery. Retry with one/five-second backoffs;
  at most three automatic pulses in 60 seconds, then latch failed.
- Preserve repeated-reboot latch; no replay after reset/session loss. READY loss
  first permits watchdog recovery/discovery. Suppress escalation during startup,
  maintenance, shutdown, update and repair. Never automatically assert BOOT0.
- Add health (`healthy`, `degraded`, `recovering`, `failed`), reason, attempt count,
  watchdog/reset information. Lifecycle mode stays separate from health.
- CLI `node recovery status|clear`; clear requires `--yes`, new admission ID and
  expected host boot ID, then discovery without replay of previous operations.

## Milestone 14 — Persistent node faults

- Allocate two 2-KiB pages at 0x0803D000 and 0x0803D800 from existing reserved
  metadata. Generate bounds from flash_layout.yaml; preserve both boot-journal pages.
- Portable fixed-record journal with CRC, version, generation and commit-last
  marker; STM flash adapter in task context. Preserve newest committed record
  during rotation and report overwritten records/storage failures.
- Persist reset/boot summaries and critical transitions with firmware, session,
  correlation and details. Coalesce repeats; do not journal routine traffic.
  Serialize flash writes; none in ISR/fatal handlers or during update ownership.
- Lifecycle service gains bounded history read/clear. CLI `node faults list|clear`;
  clear requires `--yes`. Reads preserve data. Journal failure must not block boot.
- Nordic exposes hardware reset cause, without its own persistent journal.

## Milestone 15 — Acknowledged events and wakeups

- Legacy events remain unchanged. Negotiate a new capability before enabling
  numbered, session-scoped event envelopes and cumulative acknowledgment.
- STM queue capacity 16; retain unacknowledged events and hold IRQ_N low while
  pending. Bound retransmission; expose overflow/retry-exhaustion counters.
- Nordic acknowledges only after bounded local-history admission and deduplicates
  retransmissions. ACK does not mean PC consumption. Reject stale-session ACKs.
- Propagate event-sink backpressure. Retain actuator completion for retry; count
  button loss. Service-response sequence handling is independent.
- IRQ/READY GPIO and UART callbacks wake main only; main parses/processes.
  Check initial IRQ level, retain periodic timer/missed-notification fallback.
  No unmeasured low-power claims.

## Milestone 16 — Evidence and acceptance

- Root-invoked suites: startup, compatibility, watchdogs, automatic recovery,
  persistence, reliable events and opt-in update interruption.
- Publish legacy/new compatibility matrix and two walkthroughs: dropped acceptance
  with one execution; stall/watchdog/session change/rediscovery without replay.
- Guard destructive HIL with explicit opt-in and verified rescue artifacts/access.
  Cover interrupted transfer, pre-activation reset and trial-failure rollback.
  Exhaust metadata corruption boundaries natively; separately authorize hardware
  corruption. Never mass erase or modify protection/options as a test side effect.
- Distinguish software reset from manual power interruption. No automated USB
  power switching. Test startup delay and independent reset within verified
  electrical power order; arbitrary power-order support remains a hardware limit.
- Optional real analyzer capture manifest: two UART directions and DEBUG/IRQ;
  illustrative traces remain labeled simulated. No capture is fabricated.
- Re-run 100 samples/600-second watch with latency/stack/recovery/loss counters;
  distinguish history overwrites from event delivery failures.

## Ownership and gates

Integrator freezes shared headers, protocol IDs, flash layout and control API.
Nordic agent owns host watchdog/recovery/control adapters; STM agent owns application
and flash adapter; portable agent owns journal/event primitives; tools agent owns
CLI; validation agent independently owns HIL/tests. No concurrent header/schema edits.

For each milestone run all native/bootloader/Python tests and relevant firmware
builds (standalone STM and both slots, bootloader and Nordic), record flash/RAM/stack,
Git/SDK versions and exact commands in docs/PHASE_REPORTS/PHASE_N.md. Hardware is
PASS only with actual evidence; otherwise NOT_RUN_HARDWARE with root commands.

Native gates include reset-latch timing, no duplicate completion/replay, update
ownership exclusion, every journal write boundary, corrupt/rollover records,
lost edge/ACK, duplicate/full queue/session changes and legacy negotiation.


## Implementation refinements recorded during review

- Reliable delivery uses stop-and-wait (cumulative window1). HELLO pauses delivery
  until explicit enable or legacy LIST; enable after reconnect rearms retained data,
  while duplicate enable does not reset retries. Same-host/session cursors survive
  manager reinitialization. Legacy fallback drains retained data in original format.
- Debug-only trial-failure slot presets provide a reproducible non-confirming
  candidate; release rejects the option. Hardware trial/power-loss evidence remains
  a manual gate and is not inferred from compile or simulated results.
- Full verification:26 root CTests,2 bootloader CTests,331 Python tests; Nordic
  reliability/Phase12/default and STM release/debug reliability, legacy, failing-trial
  and bootloader builds passed. Hardware is NOT_RUN_HARDWARE.


Startup correction (2026-10-26): real-board status exposed boot_id0 because optional
Port2 GPIO interrupts returned ENOTSUP before control initialization. Preserve the
verified wiring: UART wake/10ms polling is the supported fallback, explicitly exposed
as gpio_interrupts:false. Native actual-adapter regressions now cover unsupported
interrupts/callback failures and keep required pin-configuration errors fatal.
Physical interrupt wake on another pin remains unverified and is not claimed here.


User hardware acceptance recorded before commit: corrected host starts online with
watchdog enabled; signed reliability update, five-sample/ten-second event demo and
journal retention through node reset passed. Extended100/600, watchdog expiry,
power-loss/interruption and rollback gates remain pending; see individual reports.
