# Phase 2 implementation plan and frozen contracts

Date: 2026-10-12 (Europe/Skopje)
Baseline: `d99c830e4c3e71e3e2f3243b77de89615405e794`

## Scope

1. Implement and test the portable COBS-delimited, CRC32-protected protocol library.
2. Integrate fixed-capacity UART transports and discovery state machines on both firmware images.
3. Document electrically verified application UART, READY, active-low IRQ and analyzer pins.
4. Build both firmware images and native malformed/partial/corrupt stream tests.
5. Perform two-board HIL only after the user confirms the Nordic I/O rail is physically compatible.

Remote reset and BOOT0 control are excluded until their electrical behavior and recovery path are
verified. No power rails may be connected between boards.

## Frozen wire frame v1

The decoded packet is at most 256 bytes and contains these fields in order, encoded explicitly
little-endian:

| Offset | Field | Size |
|---:|---|---:|
| 0 | magic `0x4D43` | 2 |
| 2 | wire major version `1` | 1 |
| 3 | frame kind | 1 |
| 4 | flags | 1 |
| 5 | sequence | 2 |
| 7 | payload length | 2 |
| 9 | payload | 0..243 |
| 9 + length | CRC32 of header and payload | 4 |

COBS encodes the complete decoded packet, followed by delimiter `0x00`. The maximum encoded packet
including delimiter is 259 bytes. CRC uses the reflected IEEE polynomial `0xEDB88320`, initial and
final XOR `0xFFFFFFFF`. Kinds are request `1`, response `2`, event `3`; flags other than the frozen
response-error bit are rejected in v1. Sequences correlate request/response pairs and zero is
reserved for unsolicited events.

The decoder owns fixed storage, accepts arbitrary partial byte chunks, emits at most one callback
per complete delimiter-terminated frame, drops overlong frames through the next delimiter, and
resynchronizes after malformed COBS, length, enum, version or CRC failures. It performs no heap
allocation and never aliases a C struct onto wire bytes.

## Frozen Phase 2 management IDs

| ID | Name | Direction |
|---:|---|---|
| `0x0001` | `HELLO` | host request / node response |
| `0x0002` | `GET_IDENTITY` | host request / node response |
| `0x0003` | `GET_CAPABILITIES` | host request / node response |
| `0x0004` | `HEARTBEAT` | host request / node response |

Every request begins with `message_id:u16` and `session_generation:u32`; every response echoes the
message ID followed by the node's current session generation. HELLO appends management major/minor
as `u16` and wire major as `u8`. Identity appends `length:u8` and at most 32 ASCII bytes;
capabilities append a `u32` mask; heartbeat echoes at most 16 request bytes after the prefix. Error
responses set the response-error frame flag and append `status:u8`. The STM node chooses a nonzero
session generation at boot. A host accepts ONLINE state only after a valid HELLO response for the
current session. No payload contains a pointer, native enum, padding or unbounded text.

## Ownership

- Integrator: this contract, public headers, global CMake, pinout/wiring decisions, final builds.
- Protocol agent: `libraries/protocol/` implementation and native tests after this freeze.
- Nordic agent: `firmware/nrf54l15-host/` transport and host link state machine.
- STM agent: `firmware/stm32u083/application/` and `platform/` UART/sideband integration.
- Validation agent: independent native/build review after integration.
