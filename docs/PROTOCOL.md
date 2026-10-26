# UART protocol v1

The inter-MCU link is binary UART at 115200 8N1. Each frame is COBS encoded and terminated by a
zero byte. The decoded bytes are bounded to 256 bytes and use explicit little-endian fields:

| Offset | Field | Bytes |
|---:|---|---:|
| 0 | magic `0x4d43` | 2 |
| 2 | wire major version (`1`) | 1 |
| 3 | kind: request `1`, response `2`, event `3` | 1 |
| 4 | flags | 1 |
| 5 | sequence | 2 |
| 7 | payload length | 2 |
| 9 | payload | 0–243 |
| 9 + length | IEEE CRC32 over header and payload | 4 |

Only bit 0, `RESPONSE_ERROR`, is defined, and it is valid only on response frames. CRC32 uses the
reflected polynomial `0xedb88320`, initial value `0xffffffff`, and final XOR `0xffffffff`. The
maximum encoded frame, including delimiter, is 259 bytes. Sequence zero is reserved for
unsolicited events.

## Phase 2 management payloads

Requests begin with `message_id:u16, session_generation:u32`. Responses repeat the message ID and
the node's current nonzero session generation. The messages are:

| ID | Request | Successful response tail |
|---:|---|---|
| `0x0001` | HELLO | management major/minor `u16`, wire major `u8` |
| `0x0002` | GET_IDENTITY | length `u8`, ASCII identity (at most 32 bytes) |
| `0x0003` | GET_CAPABILITIES | capability mask `u32` |
| `0x0004` | HEARTBEAT | zero to 16 echoed request bytes |

An error response sets `RESPONSE_ERROR` and appends one status byte. Non-HELLO requests with a
stale session are rejected. The host sends nothing while READY is low, performs HELLO, identity,
and capability discovery after READY rises, and uses bounded timeouts and retries thereafter.

## Phase 3 service payloads

`LIST_SERVICES` (`0x0005`) returns `count:u8` followed by that many eight-byte descriptors:
`service_id:u16, version_major:u16, version_minor:u16, flags:u16`. The list is bounded by
`NODE_API_MAX_SERVICES`.

`SERVICE_CALL` (`0x0100`) adds `service_id:u16, command_id:u16, operation_id:u32` after the normal
request/response prefix, followed by at most 64 bytes of service-owned data. A successful response
means that a command was accepted or completed synchronously; it does not claim that an
asynchronous operation has completed.

`SERVICE_EVENT` (`0x8000`) is a frame of kind `EVENT` with sequence zero. After the normal
message/session prefix it contains `service_id:u16, event_id:u16, operation_id:u32` and bounded
event-owned data. The active-low IRQ sideband indicates that node event traffic is pending; UART
framing remains authoritative if an IRQ edge is missed.

Generic GPIO event IDs live in `node-api`; removable virtual sensor and actuator schemas live in
`examples/virtual-io`. Side-effecting actuator calls require a nonzero operation ID. Retrying an
accepted ID returns its original result without repeating the operation.

The node also keeps one bounded replay entry for the most recent successful service request, keyed
by service, command, operation ID and exact request data. An identical transport retry receives
the cached response without invoking the service again. Transient error responses are not cached
because their underlying condition may change.

## Phase 4 diagnostics service

Development builds may advertise generic diagnostics service `0x0002`. Commands are `GET_STATUS`
(`1`), `ENABLE_INJECTION` (`2`), `DISABLE_ALL` (`3`) and `GET_RECORD` (`4`). Runtime injection
state is volatile and clears at reboot; non-Debug STM builds reject injection enablement.

A diagnostic record is exactly 17 bytes in explicit little-endian order:
`timestamp_ms:u32, correlation_id:u32, detail:u32, code:u16, source:u8, severity:u8, domain:u8`.
The timestamp is local to its source and is not globally synchronized. Status data is 13 bytes:
`enabled_mask:u32, total_recorded:u32, overflow_count:u32, queued_count:u8`. `GET_RECORD` returns
`available:u8` followed by an optional record. A diagnostic record event uses event ID `1` and the
same 17-byte encoding.

The record queue is bounded to 16 entries. On overflow it drops the oldest entry and increments a
saturating overflow counter. Host tooling correlates records by operation/correlation ID and
collector receipt time without claiming MCU clock synchronization.

## Phase 5 lifecycle service

Lifecycle service `0x0003`, version `1.0`, provides `GET_STATUS` (`1`), `ENTER_MAINTENANCE` (`2`),
`EXIT_MAINTENANCE` (`3`), `GET_VERSIONS` (`4`), `ENTER_SHUTDOWN` (`5`) and `EXIT_SHUTDOWN` (`6`).
Shutdown is software-only quiescence and does not control power. Status is 12 bytes:
`state:u8, reset_reason:u8, raw_reset_flags:u16, boot_count:u32, progress_count:u32`. Versions are
16 bytes: firmware major/minor/patch as `u16`, wire major/minor as `u8`, management major/minor as
`u16`, and `build_id:u32`. All multibyte fields are explicit little-endian values.

Compatible services require equal major versions and an offered minor greater than or equal to the
required minor. An incompatible optional service is skipped without disabling other services. See
`docs/COMPATIBILITY.md` for session, reboot and fault-latch policy.

The canonical constants and APIs are in `libraries/protocol/include/protocol/` and
`libraries/node-api/include/node_api/NodeCommands.h`. Portable tests cover the golden frame,
maximum length, partial streams, deterministic varied-length round trips, malformed COBS, invalid
fields, CRC corruption, overrun dropping, and resynchronization.


## Negotiated reliable events (Milestone 15)

Wire-major v1 and all legacy messages remain unchanged. Capability bit5 advertises
reliable events. A capable host sends ENABLE_RELIABLE_EVENTS0x0006 after capabilities,
with normal session prefix and no tail. Its response prefix has next_event_id:u32:
first queued ID or next allocator value. Enable is idempotent for that session and
does not clear outstanding events. Legacy peers keep SERVICE_EVENT0x8000.

RELIABLE_EVENT0x8001 uses kind EVENT, sequence0, normal prefix, event_id:u32,
then the legacy service/event/operation header and data. IDs are allocated contiguously
only on queue admission; capacity16. EVENT_ACK0x0007 uses REQUEST, sequence0,
normal prefix plus admitted event_id:u32 and has no response. ACK is cumulative,
validated against actually transmitted IDs and current session. Retry is bounded;
unacknowledged entries remain pending/IRQ-held on exhaustion, with explicit counters.
Node reset clears volatile events. Host acknowledges only after local history admission,
deduplicates retransmissions and never interprets ACK as PC consumption. Periodic
fallback remains authoritative if an IRQ edge is missed.


Reconnect semantics: HELLO pauses queued event delivery until negotiation finishes.
ENABLE after HELLO rearms an exhausted head without changing IDs; duplicate ENABLE
replies do not reset its retry budget. LIST_SERVICES without ENABLE selects legacy
format and drains retained entries once with legacy delivery semantics. A host
retains its receive cursor across same-session manager rediscovery within one host
boot, so lost ACKs do not cause duplicate history publication. A changed node session
starts a new cursor; this is not durable exactly-once delivery across power loss.

Lifecycle service command0x000c returns24 bytes, six LE u32 values: pending,
rejected_admissions, retries, exhausted, button_dropped, next_id. Rejections include
producer backpressure retries and are not synonymous with lost events. Terminal
actuator events remain pending in their producer until queue admission succeeds.
