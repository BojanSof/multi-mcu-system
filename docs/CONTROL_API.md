# Host control API v1 (Phase 9 contract)

Status: frozen for Phase 9 implementation. SMP v2 over the existing J-Link VCOM,
custom group 64, CBOR maps. No inter-MCU wire changes. All integer fields are unsigned
unless explicitly stated. API version is 1. Normal profile remains selectable;
`phase9` enables MCUboot, standard image management and this control group.

| Command | SMP operation | Request | Response |
|---|---|---|---|
| 0 status | read | empty map | `api`, `host_boot_id`, `state` (text), `session`, `identity`, `capabilities`, `timeouts`, `rejected`, `received_events`, `dropped`, `busy`, `firmware` (node version text or unknown) |
| 1 services | read | empty map | `services`: maps with `id`, `major`, `minor`, `flags` |
| 2 call | write | `expected_boot_id` u32, `request_id` u64 nonzero, `service` u16, `command` u16, `data` bytes | `request_id`, `operation` u32; or `error` text |
| 3 events | read | `after` u32 (default 0) | `host_boot_id`, `events`: retained records newer than cursor; `dropped`, `latest` |
| 4 supervision | read/write | Phase 10 contract below | job status/admission |
| 5 STM update | read/write | Phase 10 contract below | job status/admission |
| 6 ROM recovery | read/write | Phase 11 contract below | recovery status/admission |
| 7 metrics | read | empty map; Phase 12 only | lifetime counters and stack snapshot below |

An event record contains `id`, `time_ms`, `session`, `operation`, `service`,
`code`, `kind` (text: response/event/error), `status`, `data` (bytes).
Keep 16 retained records, overwrite oldest and increment `dropped`. Read up to
4 records per response, without removing them; advance the PC cursor to the last
returned ID. `latest` is the newest retained ID. Session changes invalidate pending
operations; expose an error record instead of replaying an operation. Nordic creates
a nonzero random `host_boot_id` at startup. Calls reject mismatching expected boot ID
with `host_restarted`, before consulting the request cache. Clear event cursor when
boot ID changes. Compare event IDs with modular u32 sequence ordering.

Calls are handed through a bounded mailbox to the main thread, which exclusively
owns HostNodeManager. Cache the last 8 admitted request IDs and exact arguments/results.
Retry of identical arguments returns the same operation ID; changed arguments with
the same ID return `request_conflict`. Failed admission returns `busy` and is not
cached. Cache scope is one host boot; never retry a mutating call across host reboot.
SMP handlers must not block waiting for node responses. Responses and progress are
read through command 3. API validation failures use SMP errors; domain failures use
`error` values (`busy`, `offline`, `unsupported`, `request_conflict`).

Maximum service payload is 64 bytes (the existing HostNodeManager limit). Validate all lengths
before copying. Control profile disables periodic demo actions and automatic
diagnostic draining so operator requests/results are unambiguous. Onboard UI can
remain, but must use the same owner thread. Standard Nordic image commands use
MCUmgr groups 0/1. CLI alone owns the VCOM. Phase 9 routes application logs to RTT
in drop mode and disables UART printk/banners, because UART logging can otherwise
interleave inside an SMP frame. The decoder tolerates complete legacy log lines
between frames; it does not make arbitrary mid-frame logging reliable.

CLI entry: `scripts/mmctl`; implementation `tools/cli/mmctl.py`, SMP codec/client
`tools/cli/smp.py`. Python argparse, pyserial 3.5, cbor2 6.1.5. Default baud 115200,
RTS/CTS enabled. Require explicit port unless exactly one matching J-Link VCOM is
found. No hardware reset via DTR/RTS toggling. Commands use JSON-compatible output
(byte payloads become hex). State-changing calls use random nonzero u64 IDs and
bounded retries with the same ID within the same connection.

Phase 9 CLI: device list; status [--watch]; services list; sensor sample;
actuator start [--duration-ms]; events watch; diagnostics status|inject|clear|records;
node maintenance enter|exit, shutdown enter|exit; generic service call;
node status|versions;
host image list|upload|test|confirm|reset|update. Dangerous commands require --yes.
STM update and physical reset require the independently selectable `phase10`
profile; ROM recovery remains unavailable until Phase 11.

## Phase 10 additive contract (API v1)

Status command 0 adds `supervision` and `stm_update` booleans. Commands 4/5
return unsupported in Phase 9. All Phase 10 writes require nonzero `request_id`
u64 and `expected_boot_id` u32. Only the main thread touches GPIO/UART; update
and runtime service calls are mutually exclusive.

Command 4 write accepts `action:"reset"`. Command 5 write accepts:

| Action | Additional fields |
|---|---|
| `begin` | `header`: exactly 512 bytes of signed MMFW v2; `total`: whole package size u32 |
| `chunk` | `job`: u64; `offset`: u32; `data`: 1–256 bytes |
| `finish` | `job`: u64 |
| `abort` | `job`: u64 |

Write replies contain `request_id` and `job` (begin/reset request ID). This is
admission, not an STM acknowledgment. Exact retries return the cached admission;
changed arguments return `request_conflict`. Eight admissions are retained, with
the current begin/reset additionally pinned. Never replay across a host restart.

Reads of either command return `host_boot_id`, `job`, `state`, `offset`, `total`,
`slot`, `confirmed_slot`, `boot_state`, `protocol`, `error`, `active`. Slots use
255 for unknown. `error` here is a **signed numeric** status, zero on success;
domain rejection replies instead have textual `error`. Boot state follows the
STM journal: 1 confirmed, 2 pending, 3 trial, 4 invalid, 5 recovery.

States: idle, querying, erasing, ready, writing, finalizing, pending, booting,
verifying, returning, complete, rolled_back, failed, aborted. `offset` advances
only after STM ACK (including the two 256-byte header chunks). The begin job/total
are visible immediately after admission. Initial idle status does not query STM
and has unknown slots; `confirmed_slot` is populated by an update's boot query.

Before reset, validate header bounds, target, slot/link address and compatibility.
Before erase, query the journal and reject confirmed, selected or pending slots.
PC validates the complete signature/hash against its supplied trusted key before
opening serial; STM independently validates before setting pending. No whole-image
staging on Nordic. Runtime discovery is restarted after boot-UART ownership ends.

Thirty seconds without transfer progress before commit aborts and returns to the
confirmed application; status polls do not keep a transfer alive. After SET_PENDING
may have been transmitted, cancellation is rejected and verification continues even
if PC disconnects. Boot the trial, query the journal after another reset, require
confirmed target (or report confirmed previous slot as rollback), then rediscover
runtime before terminal success. Lost commit ACK is an uncertain outcome to verify,
not permission to replay. GPIO reset is open-drain; BOOT0 stays low throughout.

## Phase 11 ROM repair (API v1)

Independent `phase11` profile extends Phase 10 and adds `rom_recovery` status
capability. Prior profiles reject command6. ROM is an unauthenticated rescue path;
normal signed update remains command5. No automatic ROM entry on runtime faults.

Command6 writes require request_id u64 nonzero and expected_boot_id u32, with exact
admission replay cache and stale-boot rejection as in Phase 10:

| Action | Additional fields |
|---|---|
| `begin` | `total` u32 (8..65536), `sha256` bytes32, `prerequisites:true` |
| `chunk` | `job` u64, `offset` u32, `data` bytes1..256 |
| `finish` | `job` u64 |
| `abort` | `job` u64 |

Begin assertion confirms human checks of wiring, supply levels, boot selection,
protection and SWD rescue. It is not hardware measurement. Admission replies contain
request_id/job. Command6 read returns Phase 10 status fields plus device_id and
rom_version; numeric error is distinct from textual domain errors. Only one
runtime/reset/update/repair owner. Initial job/total are visible immediately; status
remains readable while main executes bounded ROM transactions.

Repair selects 115200 8E1, BOOT0 high and open-drain NRST pulse, then sync/GET/GET_ID.
Require device 0x489 and READ/WRITE/EXTENDED_ERASE before erase. Explicit erase list:
pages0..31 (2 KiB); writes/reads restricted to 0x08000000..0x0800ffff. No mass erase,
GO, option-byte/protection commands. Chunks contiguous with 8-byte aligned offsets;
short chunk only at end, FF-padded to 8 bytes. Offset advances only after ACK and
exact readback including padding. SHA256 covers original image bytes, not padding.

Finish requires total and streaming hash match; restore BOOT0 low, UART8N1, reset
and restart runtime discovery. `complete` confirms raw loader readback/hash, not
application boot. Errors, abort or 30-second transfer inactivity also restore
pins/UART, but explicitly report the possibly partial loader. Status polls do not
extend inactivity. No mutation replay/reconnect across host boot.
If restoration itself fails, latch `failed, active:true` and retain UART ownership;
runtime traffic stays blocked. Only explicit `abort` retries restoration, without
automatic repeated resets. Its numeric error reports the restoration failure.

CLI recover validates release v2, all hashes, loader vectors/bounds, both signed
slot signatures/key IDs/compatibility and Nordic signed image offline. After repair,
wait for compatible runtime with a fresh session; stale cached online never counts.
If absent, chain signed Slot0 command5 recovery. On definite -18/protocol6 protected
Slot0 refusal only, same-boot/job checks and ownership release permit a new signed
Slot1 job; its journal guards remain authoritative. Never fallback on uncertainty
or erase metadata. Nordic firmware is not updated as a side effect of STM repair.

## Phase 12 metrics (API v1)

`phase12` and `phase12-verbose` extend Phase11 with status0 `metrics:true` and
read-only command7. Snapshots are sampled on main and copied under the control
mutex; SMP never reads manager state directly. Counters live for one Nordic boot,
including across manager reinitialization. Always compare host_boot_id before deltas.

Fields:

- host_boot_id (u32), uptime_ms (u64)
- requests, retries, responses, timeouts, protocol_errors, uart_errors
- rx_bytes, tx_bytes, rx_overflows, tx_overflows
- events, dropped, manager_restarts, online_count, session_changes, ready_losses,
  node_reboots
- latency_count, latency_sum_ms (u64), latency_max_ms, latency_last_ms
- main_stack_free, work_stack_free, stack_supported (bool)

Other integers are u32. Manager counters saturate; UART atomic counters can wrap.
Conservative acceptance rejects decreasing deltas rather than guessing a reset/wrap.
Requests count successful original runtime UART enqueues; retries count successful
retransmits. Responses count accepted correlated replies, including discovery and
heartbeats. Latency is first enqueue to matched reply, including retries/main-loop
scheduling, not asynchronous operation completion or PC round-trip time.

`manager_restarts` counts component reinitialization, including supervised update
transitions; `node_reboots`/session_changes describe observed unexpected session
changes, not every physical reset pulse. `dropped` counts overwritten retained
history records, not necessarily unread loss; detect unread loss with event cursors.
UART errors count positive uart_err_check results (one observed error-bit batch),
not unsupported driver return values. No globally simultaneous ISR/main snapshot
is claimed.

Stack free is initialized-stack scan evidence sampled on main; UINT32_MAX with
stack_supported=false means unavailable. It is not a guaranteed call-chain bound.
The response reserves32 CBOR map entries to support all26 fields.

Phase12 command5/6 status adds elapsed_ms (u64) and bytes_per_second (u32), frozen
at terminal state. Whole-job ACKed artifact bytes/time includes entry, erase,
hash verification and reboots; it is not raw UART bandwidth. Reads do not extend
transfer inactivity. A reset-only job has zero transfer throughput.

Normal phase12 compiles warning/error logs only; phase12-verbose retains info logs
for a measured resource baseline. Typed SMP events/metrics are identical; older
profiles keep their logging behavior.


## Reliability extension (Milestones 13–16, API v1 additive contract)

Available only in the independently selected `reliability` profile. Existing fields
and commands retain their semantics. Status adds health/reason, recovery attempts,
watchdog and reset-cause information. SMP command8 reads recovery status and accepts
write action `clear` with nonzero request_id/expected_boot_id as other mutations.
Only main changes recovery policy; updates/repair/lifecycle modes suppress escalation.
CLI `node recovery status|clear`; clear requires `--yes` and never replays old work.

Lifecycle service3 adds command0x000a READ_FAULT_HISTORY with four-byte LE cursor
(last journal generation, initially0), response60 bytes: found:u8, healthy:u8,
reserved:u16, overwritten:u32, storage_errors:u32, record:bytes48. If not found the
record bytes are zero. command0x000b CLEAR_FAULT_HISTORY accepts no arguments and
returns the same12-byte status header. Clear is a committed logical tombstone;
no pre-clear record remains visible after restart. CLI `node faults list|clear`;
clear requires `--yes`. Legacy firmware returns unsupported.

Record48 is explicitly LE: generation:u32, boot_count:u32, session:u32, build:u32,
time_ms:u32, correlation:u32, detail:u32, code:u16, reset_reason:u8, severity:u8,
reserved:bytes16. Generation is monotonic and refuses wrap. Timestamps are local
uptime, not synchronized wall clock. Status/history counters are not transport loss.


Reliability command8 read fields are `host_boot_id`, `health`, `reason`,
`recovery_attempts` (rolling60-second count), `recovery_total`, `reset_cause`
(Zephyr hwinfo flags), `watchdog`, `failed`. Write clear returns matching
`request_id`, `accepted:true`; eight same-boot clear admissions are retained.
Status0 includes the same health fields. Metrics7 additionally exposes
`reliable_duplicates`, `reliable_gaps`, `event_ack_retries`; duplicates suppressed
successfully are not failures. Journal `storage_errors` is a per-boot observed-I/O
counter; `overwritten` counts unavailable journal generations, not unread-PC loss.

Lifecycle command0x000c returns the event counters described in PROTOCOL.md and is
decoded by generic `service call 3 12`. Reliability nodes advertise lifecycle1.1;
legacy nodes keep1.0. Reads do not consume fault records or queued wire events.
After event-send exhaustion, explicit `node recovery clear` triggers renegotiation
and rearms delivery without discarding retained IDs. No automatic command replay.


Startup correction (2026-10-26): health/status responses add `gpio_interrupts`
(boolean, true only if both optional sideband callbacks are enabled). The wired
nRF54L15 Port2 has no GPIOTE instance in the pinned SDK; false is expected and uses
UART interrupt wake plus10ms polling. GPIO input/read configuration failures remain
fatal; optional interrupt/callback errors log warnings and must not abort startup.
