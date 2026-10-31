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
maximum encoded frame, including delimiter, is 259 bytes. Sequence zero is reserved for unsolicited events and the response-free
reliable-event acknowledgment request described below.

## Management payloads

Requests begin with `message_id:u16, session_generation:u32`. Responses repeat the message ID and
the node's current nonzero session generation. The messages are:

| ID | Request | Successful response tail |
|---:|---|---|
| `0x0001` | HELLO | management major/minor `u16`, wire major `u8` |
| `0x0002` | GET_IDENTITY | length `u8`, ASCII identity (at most 32 bytes) |
| `0x0003` | GET_CAPABILITIES | capability mask `u32` |
| `0x0004` | HEARTBEAT | zero to 16 echoed request bytes |

An error response sets `RESPONSE_ERROR` and appends one status byte. Non-HELLO requests with a
stale session are rejected. The host waits for READY before ordinary discovery requests, then performs HELLO,
identity and capability discovery with bounded timeouts and retries.

Capability bits are identity (0), heartbeat (1), asynchronous-event IRQ (2),
lifecycle (3), reset reason (4) and negotiated reliable events (5). Unsupported
optional bits are ignored. Management/service error statuses are:

| Value | Status |
|---:|---|
| 0 | OK |
| 1 | MALFORMED_REQUEST |
| 2 | UNKNOWN_MESSAGE |
| 3 | STALE_SESSION |
| 4 | UNSUPPORTED_VERSION |
| 5 | UNSUPPORTED_SERVICE |
| 6 | UNSUPPORTED_COMMAND |
| 7 | BUSY |
| 8 | INVALID_ARGUMENT |
| 9 | CAPACITY_EXCEEDED |

## Services and operation identity

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

## Example and GPIO service data

The generic GPIO service is `0x0001`; button-changed event `0x0001` carries one
byte (zero released, nonzero pressed). It does not implement a general GPIO-write
command. The removable example defines:

| Service | Command/event | Data |
|---|---|---|
| Sensor `0x1001` | SAMPLE command `0x0001` | No request data; response `value_milli_units:i32, sample_index:u32` |
| Actuator `0x1002` | START command `0x0001` | Request `duration_ms:u32`; response `execution:u32` |
| Actuator `0x1002` | PROGRESS event `0x0001` | `progress_percent:u8, execution:u32` |
| Actuator `0x1002` | COMPLETE event `0x0002` | `execution:u32` |

Actuator durations are 100–60000 ms and require a nonzero operation ID. Sensor
samples are deterministic for a supplied seed/index and represent no physical
measurement. Acceptance and asynchronous completion are separate messages.

## Diagnostics service

The node advertises generic diagnostics service `0x0002`. Commands are `GET_STATUS`
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

## Lifecycle service

Lifecycle service `0x0003` provides `GET_STATUS` (`1`), `ENTER_MAINTENANCE` (`2`),
`EXIT_MAINTENANCE` (`3`), `GET_VERSIONS` (`4`), `ENTER_SHUTDOWN` (`5`) and `EXIT_SHUTDOWN` (`6`).
Shutdown is software-only quiescence and does not control power. Status is 12 bytes:
`state:u8, reset_reason:u8, raw_reset_flags:u16, boot_count:u32, progress_count:u32`. Versions are
16 bytes: firmware major/minor/patch as `u16`, wire major/minor as `u8`, management major/minor as
`u16`, and `build_id:u32`. All multibyte fields are explicit little-endian values.

Compatible services require equal major versions and an offered minor greater than or equal to the
required minor. An incompatible optional service is skipped without disabling other services. See
[COMPATIBILITY.md](COMPATIBILITY.md) for session, reboot and fault-latch policy.

The canonical constants and APIs are in `libraries/protocol/include/protocol/` and
`libraries/node-api/include/node_api/NodeCommands.h`. Portable tests cover the golden frame,
maximum length, partial streams, deterministic varied-length round trips, malformed COBS, invalid
fields, CRC corruption, overrun dropping, and resynchronization.


## Negotiated reliable events

Wire-major v1 and legacy messages remain supported. Capability bit 5 advertises
reliable events. A capable host sends `ENABLE_RELIABLE_EVENTS` (`0x0006`) after
capabilities, with the normal session prefix and no tail. Its response tail is
`next_event_id:u32`: the first queued ID, or next allocator value when empty.
Enable does not clear outstanding events. Legacy peers use `SERVICE_EVENT` (`0x8000`).

`RELIABLE_EVENT` (`0x8001`) uses kind EVENT, sequence zero, the normal prefix,
`event_id:u32`, then the legacy service/event/operation header and data. IDs are
allocated contiguously only on queue admission; capacity is 16. IDs do not wrap:
admission refuses allocation at the exhausted u32 limit.

`EVENT_ACK` (`0x0007`) uses REQUEST, sequence zero, normal prefix plus admitted
`event_id:u32`, with no response. Stop-and-wait gives a cumulative window of one;
ACK is validated against the transmitted head and current session. Retries are
250 ms apart, with at most five sends. On exhaustion, unacknowledged data remains
pending and IRQ-held with an explicit counter.
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

Lifecycle command `0x000c` returns 24 bytes, six little-endian u32 values:
`pending, rejected_admissions, retries, exhausted, button_dropped, next_id`. Rejections include
producer backpressure retries and are not synonymous with lost events. Terminal
actuator events remain pending in their producer until queue admission succeeds.


## Persistent fault-history payloads

Reliability nodes advertise lifecycle version 1.1; other nodes retain version 1.0.
`READ_FAULT_HISTORY` (`0x000a`) takes a four-byte LE cursor: the last journal
generation, initially zero. Its 60-byte result is:

| Offset | Field | Bytes |
|---:|---|---:|
| 0 | found | 1 |
| 1 | journal healthy | 1 |
| 2 | reserved, zero | 2 |
| 4 | overwritten journal generations | 4 |
| 8 | observed storage errors during this boot | 4 |
| 12 | record, or zeros when not found | 48 |

The 48-byte record is `generation:u32, boot_count:u32, session:u32, build:u32,
time_ms:u32, correlation:u32, detail:u32, code:u16, reset_reason:u8, severity:u8,
reserved:bytes16`. All integers are little-endian; reserved bytes encode zero.
Generation is monotonic and refuses wrap. `CLEAR_FAULT_HISTORY` (`0x000b`) takes no
arguments and returns the same 12-byte status header without a record. Clearing is
a committed logical tombstone, not secure erasure. Reads do not consume records.

UART/READY/IRQ wiring, including the Nordic Port 2 interrupt limitation and polling
fallback, is documented in [PINOUT.md](../hardware/PINOUT.md). Application framing
and journal CRCs are software IEEE CRC32 implementations; image hashing and
signatures are a separate trust mechanism, described in [BOOTLOADER.md](BOOTLOADER.md).
