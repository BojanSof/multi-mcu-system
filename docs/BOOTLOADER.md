# STM32 custom bootloader

Phase 7 adds an explicitly selected development bootloader profile. The normal STM32 ROM-bring-up
profile remains available. Factory ROM USART recovery and SWD remain the last-resort recovery
paths.

## Flash and image contract

`firmware/stm32u083/memory/flash_layout.yaml` is authoritative. Its generated layout is:

| Region | Address | Size |
|---|---:|---:|
| Bootloader | `0x08000000` | 64 KiB |
| Slot 0 | `0x08010000` | 88 KiB |
| Slot 1 | `0x08026000` | 88 KiB |
| Metadata/reserved | `0x0803C000` | 16 KiB |

Each slot begins with a 512-byte signed `MMFW` v2 header. Application vectors therefore start at
`0x08010200` or `0x08026200`; the application is linked independently for each address. SHA-256
authenticates the payload and ECDSA-P256 authenticates canonical header bytes `0..79`. Signatures
are raw low-S `r || s`; key ID and target slot are signed. CRC32 in the UART protocol only detects
transfer corruption and is not an authenticity mechanism.

Metadata uses two separate 2 KiB journal pages. A 64-byte record is committed by programming its
CRC/complement doubleword last. States are `CONFIRMED`, `PENDING`, `TRIAL`, `INVALID`, and
`RECOVERY`. The bootloader never erases the confirmed slot through its update protocol. There is
no anti-rollback version policy in this demonstration.

## Build and sign

Run from the repository root after `source ./.env.local`:

```sh
cmake --preset release-slot0 -S firmware/stm32u083/application
cmake --build firmware/stm32u083/application/build/release-slot0 --parallel
cmake --preset release-slot1 -S firmware/stm32u083/application
cmake --build firmware/stm32u083/application/build/release-slot1 --parallel
```

Create a local P-256 key only once. Never commit it:

```sh
mkdir -p keys
.venv/bin/python tools/firmware/generate_key.py \
  keys/stm32-dev-private.pem keys/stm32-dev-public.pem
.venv/bin/python tools/firmware/public_key_header.py \
  keys/stm32-dev-public.pem keys/stm32-dev-public.h --key-id 1
```

Package and independently verify a slot image:

```sh
.venv/bin/python tools/firmware/sign.py \
  --private-key keys/stm32-dev-private.pem --slot 0 --version 0.7.0 \
  --build-id 0x700 --wire 1.0 --management 1.0 \
  firmware/stm32u083/application/build/release-slot0/multi_mcu_stm32_application.bin \
  build/stm32-slot0.mmfw
.venv/bin/python tools/firmware/verify_signed.py \
  --public-key keys/stm32-dev-public.pem build/stm32-slot0.mmfw
```

The bootloader build and public-key conversion commands are defined in its README/presets. A
bootloader must be rebuilt when its trusted public key changes.

## Security boundary

This is a signed-update engineering demonstration, not production secure boot. SWD remains open,
flash/readout protections are not provisioned, public-key replacement is not protected, and there
is no production key ceremony or anti-rollback counter. Do not enable RDP Level 2 or irreversible
option-byte protection for this project.

## Hardware gate

Before claiming direct A/B completion, test on the board: boot and interrupt operation from both
slot link addresses; valid pending image confirmation; trial-crash rollback; wrong-slot, modified
hash, and modified-signature rejection; reset during erase/program; corrupt metadata recovery; and
factory ROM/SWD rescue. Bound destructive flash tests to protect endurance. Exact commands and
observed status belong in `docs/PHASE_REPORTS/PHASE_7.md`.
