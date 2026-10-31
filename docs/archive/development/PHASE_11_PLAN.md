# Phase 11 freeze — 2026-10-23

Separate `phase11` Nordic profile extends phase10; preserve all existing profiles.
No hardware writes by the agent. ROM recovery is explicit unauthenticated rescue,
not a secure boot path. No mass erase, option-byte/protection commands or GO.

## Ownership

Integrator owns this contract, public ROM header, root CMake/build wrapper,
release schema, docs/pinout and final validation. ROM agent owns implementation,
package CMake/Zephyr/tests in libraries/stm32-rom-boot except public header.
Nordic agent exclusively owns Nordic application integration/adapters/tests.
Tools agent owns tools/cli, tools/firmware/release_bundle.py and Python tests.

## Portable ROM interface

`Stm32RomBoot.h` frozen by integrator. Blocking bounded transactions, callbacks
send/read-exact with timeout, no HAL/SDK, heap or sleeping policy. Connect performs
0x7f sync, GET capabilities and GET_ID; require 0x489 and commands 0x02/0x11/0x31/0x44
before erase. Mutations never blindly retried after ACK loss. Caller may reset
and restart a fresh repair explicitly. Context poisoned/disconnected on errors.
Erase only explicit pages 0..31; write/read only [0x08000000,0x08010000).
Writes max256 bytes, offset aligned8, final short chunk padded FF to multiple8,
then byte-for-byte readback including padding. Read length1..256. Source hash
validation belongs caller; canonical layout schema remains authoritative.

## SMP/adapter contract

Command6 read status same shape as command5, plus `device_id` and `rom_version`.
Separate command6 recovery job but exclusive reservation with reset/update/runtime.
Command0 adds `rom_recovery` bool. All writes require request_id/expected_boot_id;
same exact replay protection as phase10, no mutations on staleboot or reconnect.

Command6 writes:
- begin: action, request_id, expected_boot_id, total:u32 (8..65536), sha256:bytes32,
  prerequisites:true. This assertion is operator confirmation of manual boot option
  checks, matching board/key, recovery wiring and SWD recovery path. Reject false.
- chunk: action, request_id, expected_boot_id, job, offset:u32, data:bytes1..256.
  offsets contiguous/aligned8; short chunk only at end. ACKed/readback offset only.
- finish: action, request_id, expected_boot_id, job. Require total and SHA256 match.
- abort: action, request_id, expected_boot_id, job. Abort before completion leaves
  possibly partial loader; always restore BOOT0 low, 8N1 and reset. Explicit report.

Reply admission request_id/job. Status has numeric error; domain error text.
States idle/querying/erasing/ready/writing/finalizing/returning/complete/failed/aborted.
Begin validates total/hash before setting BOOT0. Main owns GPIO/UART. Configure
115200 8E1, BOOT0 high, NRST20ms low/high-Z, settle20ms, sync. Connect before erase.
Erase only bootloader pages then accept stream. Finish recomputes SHA256, requires
readback equality perchunk, restores BOOT0 low/8N1 and resets. Complete means raw
loader readback/hash verified, not application confirmation. Report runtime separately.
30sec prefinish inactivity abort; status polls never extend it. Do not hold SMP
snapshot mutex while blocking ROM transactions; reject admission while processing.

## Release schema v2 and CLI

New release_bundle.py preserves old v1 tool. JSON exact top fields:
format:"multi-mcu-release", format_version:2, board:"nrf54l15dk+nucleo-u083rc",
system_version:text, api:1, wire:"1.0", management:"1.0", image_format:2,
nordic:{file,sha256}, bootloader:{file,sha256,address:134217728,size,key_id},
slots:[{file,sha256,slot:0},{file,sha256,slot:1}]. Paths relative to manifest
directory; reject absolute/traversal/symlink escape, unknown/missing fields,
hash/size/board/slot/link/version/key mismatch. Validate raw loader stack/reset
vectors inside40KiB RAM/64KiB flash; reject ELF/HEX as raw loader. Verify both
signed slots against supplied external P256 public key and ID. Nordic image
must parse as signed MCUboot. Manifest SHA256 is integrity, not authenticity.

CLI `stm recover MANIFEST --public-key PEM --yes --rom-prerequisites-verified`:
offline validate entire bundle before openingserial; stream rawloader via cmd6,
poll readback offsets, finish; then wait up to30sec for compatible runtime. If
online, preserve apps (no signed install). If offline, automatically invoke existing
signed Slot0 update through cmd5, which queries journal/protects confirmed slots.
Never erase apps viaROM or force around slot protection. If Slot0 chaining is
explicitly refused with -18/protocol6, sameboot/job status proves protection and
Slot1 is inactive, a NEW signed Slot1 job from the validated bundle is permitted.
Never fallback after timeout/boot change/uncertain outcome. This preserves journal
guards while allowing empty-app recovery with old confirmedSlot0 metadata; no
metadata erase. Otherwise stop/report. No Nordic image flash sideeffect of STMrepair.
`stm recover-status`, `stm recover-abort --yes`; offline `release_bundle.py verify`
and `create` commands. Safe restoration finally on errors/no CLI, bounded waits.

## Required gates

AN3155 Rev22 and AN2606 Rev70 source review, native ROM emulator for malformed
ACK/NACK/length/checksum, wrongID/caps, bounds, odd chunk, timeout/ACKloss/readback
mismatch, interruptedstream. Full existing tests, new tests and firmware builds.
HIL separately verifies NBOOT_SEL0/nBOOT1=1/BOOT_LOCK0 and unprotected reads/writes;
no software option-byte changes. Both boards3.3V, existing phase10 reset/BOOTwires,
UART PA9/10; separate logs unchanged. Document NOT_RUN_HARDWARE honestly.
