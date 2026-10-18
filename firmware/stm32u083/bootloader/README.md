# STM32U083 custom bootloader

This is the independent Phase 7 bootloader project. `BootJournal` is a portable C11 core for the
two-page boot metadata journal and is intentionally independent of STM32 HAL, crypto, FreeRTOS,
and the normal node protocol. Native tests use a constrained flash model with reset injection.

Configure and test the portable core independently:

```sh
cmake -S firmware/stm32u083/bootloader -B build/stm32-bootloader-native -G Ninja \
  -DBOOTLOADER_BUILD_NATIVE_TESTS=ON
cmake --build build/stm32-bootloader-native
ctest --test-dir build/stm32-bootloader-native --output-on-failure
```

The embedded executable is opt-in through `BOOTLOADER_BUILD_TARGET`; native journal tests remain
independent of the cross toolchain and signing material.

## Target build and trust input

The target uses Mbed TLS SHA-256 and micro-ecc P-256 verification. Its public key is deliberately
not embedded in the repository. Supply an absolute header path defining a 64-byte raw `X || Y`
public key and its key ID:

```c
#define BOOTLOADER_PUBLIC_KEY_ID 1UL
#define BOOTLOADER_PUBLIC_KEY_BYTES { /* 64 public bytes */ }
```

Then configure the `release` preset with
`-DBOOTLOADER_PUBLIC_KEY_HEADER=/absolute/path/public_key.h`. Never provide a private key to this
build.

Generate that header from the external PEM public key from the repository root:

```sh
.venv/bin/python tools/firmware/public_key_header.py \
  keys/stm32-dev-public.pem keys/stm32-dev-public.h --key-id 1
source ./.env.local
cmake --preset release -S firmware/stm32u083/bootloader \
  -DBOOTLOADER_PUBLIC_KEY_HEADER="$PWD/keys/stm32-dev-public.h"
cmake --build firmware/stm32u083/bootloader/build/release --parallel
```

## Trial confirmation handshake

The backup-register contract is defined by `BootConfirm.h`:

- Before jumping to a trial slot, the bootloader writes
  `BOOT_CONFIRM_MARKER(BOOT_CONFIRM_TRIAL_BASE, slot)` to `TAMP->BKP1R`.
- Before a confirmed boot or while remaining in recovery, the bootloader clears `BKP1R`.
- A trial application reads `BKP1R` to distinguish trial startup. After its self-tests pass it
  writes `BOOT_CONFIRM_MARKER(BOOT_CONFIRM_APPLICATION_BASE, slot)` to `BKP0R`, then requests a
  system reset. It does not write boot metadata.
- On reset the bootloader consumes and clears `BKP0R` before attempting the metadata confirmation.
  A malformed marker, wrong slot, backup-domain reset, or power loss therefore leaves the trial
  unconfirmed and the bounded-attempt policy eventually rolls back.

When at least one valid application exists, reset opens a two-second recovery-entry window. Sending
the two BootProtocol magic bytes (`MB`) during that window keeps the bootloader in recovery;
otherwise it makes the normal journal boot decision. With no valid application it enters recovery
immediately. The receiver retains partial frames across UART timeouts and searches byte-by-byte for
the next magic pair after malformed headers, lengths, or frames.

The UART recovery engine implements the frozen BootProtocol commands using stop-and-wait requests.
It caches every processed request and its exact response, including error responses, so retrying a
lost acknowledgement does not repeat flash writes or metadata transitions. A changed request with
the same sequence, an old sequence, or a future sequence is rejected. Requests also reject response
flags, unknown flags, and nonzero request status. The target buffers a partial final 64-bit flash
word and pads only its physical unwritten bytes with `0xFF`; the signed image size and hash retain
the exact logical byte length.

When at least one valid application exists, reset the STM32 while the Nordic Phase 7 client is
querying. The bootloader listens for the `MB` request prefix for two seconds before booting the
selected application. The host sends QUERY every 250 ms for at most 60 attempts (about 15 seconds);
subsequent update commands use a five-second response timeout and at most three attempts. With no
valid application the bootloader stays in recovery indefinitely. The bytewise receiver
resynchronizes after malformed or partial frames.
