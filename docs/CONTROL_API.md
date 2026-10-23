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
| 6 ROM recovery | write | reserved for Phase 11 | unsupported |

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
