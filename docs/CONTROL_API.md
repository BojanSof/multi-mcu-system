# Host control API v1

The PC interface uses SMP v2, custom group **64**, with CBOR maps over Nordic's
J-Link VCOM at 115200 baud, RTS/CTS enabled. This is separate from the inter-MCU
UART protocol. The `reliability` profile implements every command below; clients
must inspect runtime capability flags because other firmware may omit features.
All integer fields are unsigned unless explicitly stated.

| Command | SMP operation | Request | Response |
|---|---|---|---|
| 0 status | read | empty map | Identity, link/capability and health snapshot |
| 1 services | read | empty map | `services`: maps with `id`, `major`, `minor`, `flags` |
| 2 call | write | `expected_boot_id:u32`, nonzero `request_id:u64`, `service:u16`, `command:u16`, `data:bytes` | `request_id`, `operation:u32`; or `error:text` |
| 3 events | read | `after:u32`, default 0 | `host_boot_id`, `events`, `dropped`, `latest` |
| 4 supervision | read/write | write `action:"reset"` plus admission identity | Job status or admission |
| 5 STM signed update | read/write | update actions below | Job status or admission |
| 6 factory-ROM repair | read/write | repair actions below | Job status or admission |
| 7 metrics | read | empty map | Lifetime counters and stack snapshot |
| 8 recovery policy | read/write | write `action:"clear"` plus admission identity | Health snapshot or `request_id`, `accepted:true` |

CLI entry is `scripts/mmctl`; implementation is `tools/cli/mmctl.py` with SMP
codec/client in `tools/cli/smp.py`. Select an explicit port unless exactly one
matching J-Link VCOM is found. The CLI owns the port and does not reset boards
through DTR/RTS. JSON output represents byte payloads as hex. Commands that reset,
clear, inject faults or change firmware require `--yes`; see [CLI.md](CLI.md).

## Ownership, admission and retries

The main thread exclusively owns the node manager and GPIO/UART/update state.
SMP handlers submit bounded mailbox work and read snapshots; they never wait for a
node response. A maximum 64 bytes of service data is accepted.

Nordic creates a nonzero random `host_boot_id` at initialization. Mutating requests
carry `expected_boot_id` and a nonzero u64 `request_id`. A mismatched boot ID returns
`host_restarted` before replay lookup. The last eight admitted service requests
retain exact arguments/results: identical retries return the same operation ID;
changed arguments with the same ID return `request_conflict`. Failed admission
returns `busy` and is not cached. Cache scope is one host boot, not persistent storage.
Never replay uncertain mutations across host restart.

Update/reset admissions retain eight exact requests and additionally pin the
current begin/reset admission. Recovery clear retains eight same-boot clear IDs.
Mailbox admission is not a node acknowledgment or asynchronous completion.
Validation errors use SMP errors; domain rejection uses text `error` values such
as `busy`, `offline`, `unsupported` and `request_conflict`.

The operator profile suppresses periodic demonstration requests and automatic
node-diagnostic draining. Logs use nonblocking RTT; VCOM carries SMP only. The
parser can tolerate complete legacy text lines between frames, not arbitrary
text interleaving inside frames. Standard Nordic OS/image operations use MCUmgr
groups 0/1 independently of custom group 64.

## Status and health

Status contains `api:1`, `host_boot_id`, `state:text`, `session`, `identity`,
`capabilities`, `timeouts`, `rejected`, `received_events`, `dropped`, `busy` and
`firmware:text`. Firmware is `unknown` until lifecycle versions have been queried
for the current node session. `supervision`, `stm_update`, `rom_recovery` and
`metrics` indicate available features; unavailable runtime hardware can make the
first three false even when their code is compiled.

The reliability health snapshot, also returned by command 8, contains:

- `host_boot_id:u32`, `health:text`, `reason:text`;
- `recovery_attempts:u32` (rolling 60-second pulse count), `recovery_total:u32`;
- `reset_cause:u32` (Zephyr hwinfo flags), `watchdog:bool`, `failed:bool`;
- `gpio_interrupts:bool`, true only if both optional sideband callbacks are enabled.

Health is `healthy`, `degraded`, `recovering` or `failed`, separate from lifecycle.
Only main changes recovery policy. Update/repair ownership and intentional lifecycle
modes suppress reset escalation; there is no automatic ROM repair. Explicit clear
starts discovery without replaying old operations.

The verified Port 2 READY/IRQ wiring lacks GPIOTE support in the pinned SDK, so
`gpio_interrupts:false` is expected. UART interrupt wake and 10 ms polling remain
active. Required pin/input setup errors are fatal; optional callback/interrupt
errors log warnings and do not prevent initialization. A zero host boot ID means
control initialization has not completed, rather than a valid running session.

## Retained records

An event-history record contains `id`, `time_ms`, `session`, `operation`, `service`,
`code`, `kind:text` (`response`, `event` or `error`), `status`, and `data:bytes`.
The host retains 16 records, overwriting oldest entries and incrementing `dropped`.
Command 3 returns up to four records after the cursor without removing them.
Advance the cursor to the last returned ID; `latest` is the newest retained ID.
Compare record IDs with modular u32 ordering and reset the PC cursor if the host
boot ID changes. `dropped` is history overwrite, not inherently unread or wire loss.

Session changes invalidate pending operations and publish errors instead of
replaying commands. An operation ID can correlate an actuator event and a separate
diagnostic record: match record kind, service and command/event as well as operation.
The host history ID is distinct from the node's reliable-event delivery ID.

## Reset and signed STM update

Status flag `supervision` gates command 4; `stm_update` gates command 5. Writes
require the admission identity described above. Runtime operations and firmware
jobs are mutually exclusive. Command 4 accepts `action:"reset"`. Command 5 accepts:

| Action | Additional fields |
|---|---|
| `begin` | `header`: exactly 512 signed MMFW v2 bytes; `total:u32`: whole package size |
| `chunk` | `job:u64`, `offset:u32`, `data:bytes` of length 1–256 |
| `finish` | `job:u64` |
| `abort` | `job:u64` |

Admission returns `request_id` and `job` (the begin/reset request ID). Read status
contains `host_boot_id`, `job`, `state`, `offset`, `total`, `slot`, `confirmed_slot`,
`boot_state`, `protocol`, signed numeric `error`, and `active:bool`. Unknown slot
is 255. Numeric error zero means success; textual admission errors are separate.
Boot states are 1 confirmed, 2 pending, 3 trial, 4 invalid and 5 recovery.

Job states are idle, querying, erasing, ready, writing, finalizing, pending,
booting, verifying, returning, complete, rolled_back, failed and aborted. Initial
idle status does not query STM and can have unknown slots. An update boot query
populates `confirmed_slot`. Offset advances after STM ACK, including the two
256-byte header chunks. Begin job/total become visible immediately after admission.

PC tooling validates the whole package signature/hash against a trusted public
key before opening serial. Nordic checks target/slot/link/size compatibility before
reset, and the boot query rejects confirmed, selected or pending targets before
erase. STM independently verifies the signature before pending activation. Nordic
streams data without staging the entire image.

Thirty seconds without transfer progress before commit aborts toward the confirmed
application; status reads do not extend inactivity. After SET_PENDING may have been
transmitted, cancellation is rejected and verification continues without the PC.
The host boots the trial, queries persistent confirmation after another reset,
reports the confirmed target or rollback, then requires runtime rediscovery before
terminal success. A lost commit ACK is an uncertain outcome to inspect, not
permission to repeat a mutation. BOOT0 stays low; NRST sinks low or releases high-Z.

## Factory-ROM repair

Status flag `rom_recovery` gates command 6. ROM repair is unauthenticated rescue,
not a signed application update, and is never started automatically on runtime faults.
Writes require admission identity plus these action fields:

| Action | Additional fields |
|---|---|
| `begin` | `total:u32` from 8 to 65536, `sha256:bytes32`, `prerequisites:true` |
| `chunk` | `job:u64`, `offset:u32`, `data:bytes` of length 1–256 |
| `finish` | `job:u64` |
| `abort` | `job:u64` |

The prerequisite assertion records human verification of wiring, voltage, boot
selection, protection and SWD rescue. It is not an electrical measurement. Status
extends the shared job schema with `device_id` and `rom_version`.

Repair selects UART 115200 8E1, raises BOOT0, pulses open-drain NRST and performs
sync/GET/GET_ID. It requires device 0x489 plus READ/WRITE/EXTENDED_ERASE support.
Only explicit pages 0–31 (2 KiB each), addresses 0x08000000–0x0800ffff, may be
programmed. There are no mass-erase, GO, option-byte or protection commands.
Chunks are contiguous and offsets eight-byte aligned; only the final short chunk
is FF-padded. Offset advances after ACK and exact readback, including padding.
SHA-256 covers original image bytes rather than padding.

Finish requires total/hash agreement, restores BOOT0 low and UART 8N1, resets STM
and restarts runtime discovery. `complete` confirms raw-loader readback/hash, not
application boot. Failure, abort or inactivity also attempts restoration and
reports a possibly partial loader. If restoration fails, status is
`failed, active:true` and UART ownership remains held. Only explicit abort retries
restoration; runtime traffic cannot resume into unknown pin/parity state.

CLI recovery validates the release bundle, hashes, vectors, bounds, both slot
signatures and compatibility before serial access. After repair it waits for a
fresh compatible node session. If absent it may chain a signed Slot 0 update.
Only definite `-18/protocol6` protected-Slot-0 refusal, with same boot/job and
released ownership, permits a new Slot 1 job. It never falls back on uncertainty,
erases metadata or installs Nordic firmware as a side effect.

## Metrics and throughput

Command 7 snapshots are sampled on main and copied under the control mutex.
Counters persist for one Nordic boot, including manager reinitializations. Compare
`host_boot_id` before computing deltas. Fields are:

- `host_boot_id:u32`, `uptime_ms:u64`;
- `requests`, `retries`, `responses`, `timeouts`, `protocol_errors`, `uart_errors`;
- `rx_bytes`, `tx_bytes`, `rx_overflows`, `tx_overflows`;
- `events`, `dropped`, `manager_restarts`, `online_count`, `session_changes`,
  `ready_losses`, `node_reboots`;
- `latency_count`, `latency_sum_ms:u64`, `latency_max_ms`, `latency_last_ms`;
- `main_stack_free`, `work_stack_free`, `stack_supported:bool`;
- `reliable_duplicates`, `reliable_gaps`, `event_ack_retries` in reliability firmware.

Other integers are u32. Manager counters saturate; UART atomic counters can wrap.
Conservative acceptance rejects decreasing deltas rather than assuming wrap.
Requests count original enqueues; retries count retransmits; responses count
matched replies, including discovery/heartbeats. Latency measures original enqueue
to reply, including retries/scheduling, not PC round-trip or actuator completion.
Manager restarts include update-mode transitions; observed unexpected session
changes differ from physical reset pulses. UART errors count observed nonzero
error-bit batches. No globally simultaneous ISR/main snapshot is claimed.

Stack free is initialized-stack scan evidence, not a worst-case call-chain bound.
UINT32_MAX with `stack_supported:false` means unavailable. Update/repair status also
includes `elapsed_ms:u64` and `bytes_per_second:u32`, frozen at terminal state.
Whole-job acknowledged artifact throughput includes erase, validation and reboots;
it is not raw UART bandwidth. Reset-only jobs have zero transfer throughput.

## Lifecycle history and event counters

Reliability nodes offer lifecycle 1.1. Fault-history read/clear and reliable-event
counter layouts are in [PROTOCOL.md](PROTOCOL.md). CLI commands are
`node faults list|clear` and generic `service call 3 12` for event counters. Reads
consume neither journal records nor queued wire events. Storage-error counts live
for one node boot; overwritten journal generations are not unread-PC loss.
After event-send exhaustion, explicit `node recovery clear` renegotiates delivery
without discarding retained IDs. All volatile duplicate protection remains bounded.
