# STM32 signed A/B bootloader

The custom loader is a separate bare-metal STM32 project. It implements signed
slot validation, bounded trial boots, confirmation, rollback and UART recovery.
It does not depend on FreeRTOS or the normal service protocol. For commands, start
with [installation and updates](FIRMWARE_UPDATES.md).

## Memory and signatures

The authoritative source is `firmware/stm32u083/memory/flash_layout.yaml`.
Generated linker scripts and constants preserve these boundaries:

| Region | Address | Capacity |
|---|---|---:|
| Bootloader | `0x08000000` | 64 KiB |
| Slot 0 | `0x08010000` | 88 KiB |
| Slot 1 | `0x08026000` | 88 KiB |
| Metadata/reserved | `0x0803C000` | 16 KiB |

Each slot begins with a 512-byte signed MMFW v2 header; vectors begin at
`0x08010200` or `0x08026200`. Each application is linked for its own address.
The canonical signed header bytes 0–79 contain target, key ID, slot/link address,
version and payload SHA-256. ECDSA P-256 signatures use raw low-S `r || s`.
Hash equality alone does not authenticate an image; the signature does.

Boot metadata uses two separate 2 KiB journal pages. Its 64-byte records commit the
CRC/complement doubleword last. Dedicated fault-history pages at `0x0803D000` and
`0x0803D800` are separate from boot selection. States include confirmed, pending,
trial, invalid and recovery. The update engine never erases the confirmed slot;
there is no anti-rollback version policy.

## Trial confirmation

Before a trial jump, the loader writes a slot-specific trial marker to backup
register `BKP1R`. After its scheduled initialization/self-test, the application
writes a confirmation marker to `BKP0R` and requests reset. Only the loader commits
flash boot metadata. Missing, malformed or wrong-slot confirmation leaves the image
unconfirmed. At most two trial attempts are allowed before falling back to a valid
confirmed image. With no boot record, the device stays in recovery until an update
creates one; directly flashing an application ELF does not perform this transition.

The loader opens a two-second recovery-entry window before cryptographic slot
validation. BootProtocol magic bytes `MB` select recovery; otherwise it validates
slots and follows the journal. No usable application leads to recovery. UART frame
parsing survives partial input and resynchronizes after malformed frames.

## UART ownership and verification

The loader uses its own bounded BootProtocol, not the application COBS protocol.
Stop-and-wait requests include sequence, offset and CRC. Exact retries return a
cached response; conflicting/stale requests cannot repeat flash transitions.
Physical final doublewords are padded with `0xFF`, while hashes retain logical
payload length. Finalize verifies hash, signature, vectors, target and slot bounds.
Normal update completion includes confirmation and runtime rediscovery.

SHA-256 uses Mbed TLS and P-256 verification uses micro-ecc, both currently software
implementations. Flash and UART adapters remain target-specific. Open SWD and ROM
recovery allow replacing the trust root; this is a signed-update demonstration,
not a production provisioning or protected-boot claim.

## Build, test and recover

See [setup](SETUP.md) for pinned dependencies and
[firmware installation](FIRMWARE_UPDATES.md) for fresh-board key/build/sign steps.
The bootloader accepts only an external public-key header, never a private key.

```sh
cmake -S firmware/stm32u083/bootloader -B build/stm32-bootloader-native -G Ninja \
  -DBOOTLOADER_BUILD_NATIVE_TESTS=ON
cmake --build build/stm32-bootloader-native
ctest --test-dir build/stm32-bootloader-native --output-on-failure
```

Hardware interruption and deliberately failing-trial procedures are in
[testing](TESTING.md). Keep SWD/factory-ROM rescue verified before destructive work.
