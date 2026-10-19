# Portable C API contracts

Public functions use caller-owned, fixed-capacity storage and return explicit status values. Inputs
are not wire-cast structs; multibyte wire fields are encoded explicitly little-endian. APIs are
reentrant only when callers provide distinct state objects. No package allocates heap memory.

## Protocol

`Protocol_EncodeFrame` serializes one frame as COBS plus a terminating `0x00`; it checks kind,
flags, payload length, output capacity and appends CRC32. `Protocol_DecodeFrame` receives one
encoded frame without the delimiter and rejects malformed COBS, magic/version/kind/flag/length/CRC
errors without returning a partially accepted frame.

`Protocol_ConsumeBytes` incrementally consumes arbitrary chunks. Initialize each
`Protocol_Decoder` with `Protocol_DecoderInitialize`. Complete valid frames invoke the frame
callback; rejected frames invoke the error callback and update counters. Overflow enters dropping
mode until the next delimiter, after which decoding resynchronizes. Callback frame storage belongs
to the decoder call and must be copied if retained.

Limits are 256 decoded bytes, 243 payload bytes and 259 encoded bytes. Sequence IDs correlate
transport request/response only; service operation IDs provide application duplicate defense.

## Node API and services

Initialize a `NodeApi_Registry`, then register at most eight unique service IDs. Registration stores
the handler/context and descriptor by value. Descriptor lookup pointers remain owned by the
registry. A handler must bound response data to 64 bytes and return a `NodeApi_Status`.

`NodeApi_DispatchRequest` validates pointers and sizes, locates the service and calls its handler.
It caches the most recent request bytes and response. An exact duplicate of service ID, command ID,
operation ID and request data returns the cached response without calling the handler again. Reusing
an operation ID with different request data is not an exact duplicate, so the registry calls the
service handler; services that require stronger operation-ID conflict detection must implement it.

`NodeApi_IsVersionCompatible` is true only for equal majors and an offered minor not less than the
required minor. Optional incompatible services do not invalidate compatible services.

## Diagnostics

`Diagnostics_Initialize` clears a caller-owned 16-record state. `Diagnostics_RecordFault` appends a
record; once full, the oldest record is overwritten and `overflow_count` advances. `total_recorded`
tracks accepted records. `Diagnostics_PopFault` removes the oldest queued record. Injection IDs are
development controls stored as a volatile bit mask; invalid IDs are rejected and reset-time
initialization disables every injection.

Diagnostics timestamps are producer-local milliseconds. Correlation IDs associate a record with an
operation but do not establish synchronized clocks between MCUs.

## Virtual-I/O example

`VirtualIo_Initialize` requires persistent caller storage and establishes deterministic state from
the supplied seed. `VirtualIo_RegisterServices` registers the sensor and, for personality B, the
actuator. `VirtualIo_Tick` advances bounded asynchronous work and invokes the event sink from the
caller's context; the sink must not retain the temporary event pointer.

Actuator duration is 100–60,000 ms. Completed operation IDs are retained in an eight-entry bounded
history so a retry does not execute an accepted operation twice. All prerequisite/output fields are
software model state, never electrical state.

## Firmware image

`FirmwareImage_ParseManifest` (MMFW v1) and `FirmwareImage_ParseSignedHeader` (MMFW v2) validate
format and bounds and return a payload pointer into the caller's input buffer; that buffer must
remain alive. Compatibility checks are separate from parsing. Hash validation compares a supplied
calculated digest; the portable package does not calculate hashes or access keys.

For signed v2, `FirmwareImage_GetCanonicalSignedBytes` exposes the exact header prefix covered by
the signature. Cryptographic verification is deliberately supplied by host tooling or the target
bootloader. Slot, vector address, target/hardware IDs, algorithm, key ID, size and required protocol
versions must all pass compatibility before installation.

## Boot protocol

`BootProtocol_Encode` and `BootProtocol_Decode` operate on one complete CRC-protected frame, with a
20-byte header and at most 256 payload bytes. They do not perform stream buffering, UART I/O, flash
writes, retry policy or signature validation. Current decoding requires boot-protocol 1.0.

Responses set `BOOT_PROTOCOL_FLAG_RESPONSE` and echo command and sequence. The target accepts the
expected sequence and may replay an exact cached response after a lost acknowledgement; it must not
repeat the flash mutation. Slot flags select Slot 0/1. `WRITE_CHUNK` offsets are relative to the
slot base. `FINALIZE` verifies the complete signed image before `SET_PENDING` can change boot state.
The confirmed slot is never an erase target through this protocol.

## Ownership and concurrency

All state objects are caller-owned and must outlive calls that reference them. The libraries do not
provide locking or interrupt exclusion; firmware adapters serialize access or protect state when
used across ISR/task contexts. Callbacks execute synchronously in the invoking context unless the
target adapter explicitly queues them. Status must be checked on every call; failure never grants
permission to use partially filled output as valid state.
