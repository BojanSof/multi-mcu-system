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

The canonical constants and APIs are in `libraries/protocol/include/protocol/` and
`libraries/node-api/include/node_api/NodeCommands.h`. Portable tests cover the golden frame,
maximum length, partial streams, deterministic varied-length round trips, malformed COBS, invalid
fields, CRC corruption, overrun dropping, and resynchronization.
