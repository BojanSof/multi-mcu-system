# Phase 10 plan and interface freeze — 2026-10-20

Implement an independently selectable `phase10` profile extending Phase 9. Preserve
Phase 7 embedded-image/demo and Phase 9 control profiles. No option-byte writes.
Native and firmware tests precede all physical reset/flash steps.

## Ownership

- Integrator: shared API/header freeze, portable image header parsing, root build,
  build wrapper, pinout verification, docs and final integration.
- Update-core agent: `src/bootloader/HostStmUpdate.c`, `tests/update/` only.
- Nordic adapter agent: Nordic `HostControl.c`, main/manager/transport/platform,
  new `HostUpdateAdapter.*`, profile overlay/config and application CMake only.
- CLI agent: `tools/cli/`, new `tools/tests/test_phase10_cli.py` only.

## SMP group 64 additive contract

Phase 9 API stays version 1. Status adds `supervision` bool and `stm_update` bool.
Only Phase 10 exposes them as true. Commands 4 and 5 are available only in Phase 10.
All writes require nonzero request_id u64 and expected_boot_id u32; stale host boot
is rejected before admission. Main-thread mailbox owns all UART/GPIO operations.
One update/reset job at a time; runtime calls return busy while an update is active.

Command 4 write: `action` text `reset`, request_id, expected_boot_id. Return
`request_id`, `job` (=request_id); duplicate identical reset must not pulse again.
Command 4 read: same response fields as command 5 status (reset shares job machinery).

Command 5 read: `host_boot_id`, `job` u64 (0 if none), `state` text,
`offset` u32 (bytes ACKed by STM), `total` u32, `slot` u8 (255 unknown),
`confirmed_slot` u8, `boot_state` u8 (BootJournal values), `error` signed int,
`protocol` u8, `active` bool. Error here is numeric status, not a domain error reply.
Do NOT put numeric error=0 in an SMP response through the Phase 9 client generic
error-text path; client recognizes numeric status separately.

Command 5 write actions:
- `begin`: request_id, expected_boot_id, `header` bytes exactly 512, `total` u32.
  Return request_id/job. Validate MMFW v2 header bounds, board, slot, link address,
  protocol versions before any reset/erase. Query boot state before erase; never
  erase confirmed/selected active slot. No confirmed slot permits explicit signed slot.
- `chunk`: request_id, expected_boot_id, `job` u64, `offset` u32, `data` bytes 1..256.
  Return request_id/job. Host ACK means admission only; poll status offset before
  sending the next chunk. Only exact contiguous chunks admitted; exact retry is
  deduplicated (including when already ACKed). No whole-image buffer in Nordic.
- `finish`: request_id, expected_boot_id, job. Only after offset==total. STM FINALIZE
  verifies full hash/signature; SET_PENDING occurs only after successful finalize.
- `abort`: request_id, expected_boot_id, job. Stops before SET_PENDING; reject abort
  once commit starts. Reset to confirmed app, release boot ownership. PC disconnect
  for 30s before commit triggers abort. After commit complete verification autonomously.

Replies with domain errors use text key `error` (`busy`, `host_restarted`,
`request_conflict`, `invalid_state`, `invalid_image`, `wrong_job`, `unsupported`).
Retain last 8 admitted mutating request fingerprints/results, including begin across
completion; identical retries cannot start another update. Cross-host-boot replay
never allowed. Status is not a heartbeat for transfer inactivity; accepted chunks are.

## Stream/boot algorithm

Main-thread portable `HostStmUpdate` owns bounded BootProtocol stop-and-wait exchange,
exact sequence/command/slot checks and three identical retries. QUERY entry uses
250ms retries for 15s; erase/finalize allow 5s per response. It keeps only a header,
one chunk and RX/TX frames, validates incoming lengths before copying and resyncs.
An ACK updates offset; never advance on send alone. Header bytes must match begin
header when the first two stream chunks arrive. Finalize only on exact total length.
Signed bytes and BootProtocol formats are unchanged.

After commit, pulse reset with BOOT0 low and resume application discovery. After
online or 30s, reset into the custom recovery window and query the journal to verify
CONFIRMED target or confirmed previous slot (rollback). Reset back to runtime and
rediscover; only then report complete/rolled_back. No inference from equal versions
or UART acceptance. Pending/trial at verification is a failure, not confirmed success.

Portable header `HostStmUpdate.h` is integrator-owned. Ops: bounded send/read,
monotonic clock, reset-to-custom-entry, runtime restart, runtime-online. Core must
not touch Zephyr/HAL. Adapter implements narrow GPIO pulse and UART flush. Host
manager is paused whenever core uses boot UART; its parser/session is reset on return.

## CLI

`node reset --yes`; `stm status`; `stm image inspect IMAGE --public-key PEM`;
`stm update IMAGE --public-key PEM --yes`; `stm abort --yes`.
Require trusted external P-256 public key and verify MMFW signature/hash/target/slot/
link address/size/versions before connecting or sending begin. Reuse existing mmfw
parser; do not duplicate crypto. Stream whole MMFW including header in <=256B chunks,
wait for STM ACKed offset and final complete/rolled_back/error. Always check host_boot_id
and job during status polls. No mutation replay across reconnect. Report uncertain
outcome and provide `stm status` after transport failure. Ctrl-C makes best-effort
abort only before commit; never automatic option-byte/ROM operations.

## Hardware and verification gates

Approved candidates P1.08->STM NRST and P1.14->BOOT0 require exact DK schematic,
LED/button circuit and errata verification. NRST sinks low/releases high-Z; never
drives high. BOOT0 stays low in Phase 10, ROM assertion is Phase 11. High-Z at Nordic
reset plus STM pull-down supplies safe default. Disable conflicting DK consumers.
Unverified physical wiring is BLOCKED_ELECTRICAL; continue software/tests/builds.
No hardware access here: HIL will be NOT_RUN_HARDWARE with exact root commands.
