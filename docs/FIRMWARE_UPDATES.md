# Firmware installation, updates and recovery

All commands run from the project root after [setup](SETUP.md). Use the verified
[wiring](../hardware/WIRING.md), Nordic-first power order and compatible 3.3 V I/O.
Keep independent SWD access. Builds never flash automatically.

There are three update mechanisms: Nordic MCUboot/MCUmgr, signed STM32 A/B update,
and explicit STM32 factory-ROM repair. They are not an atomic system-wide update.

## Build the current firmware

```sh
source ./.env.local
scripts/build/build_nordic.sh --profile reliability
scripts/build/build_stm32.sh --profile reliability
```

Nordic output is `build/nrf54l15-host-reliability/`. STM application outputs are
`firmware/stm32u083/application/build/release-reliability-slot0/` and
`firmware/stm32u083/application/build/release-reliability-slot1/`.

## Keys and STM32 packages

For an existing installation, reuse the private/public key pair matching its
bootloader. Do not generate a replacement merely to make a build pass. The public
key header is a bootloader build input; the private key is never a firmware input.

Only for a new board with no trust root to preserve, create a development key once:

```sh
umask 077
mkdir -p keys build/images
.venv/bin/python tools/firmware/generate_key.py keys/stm32-dev-private.pem keys/stm32-dev-public.pem
.venv/bin/python tools/firmware/public_key_header.py \
  keys/stm32-dev-public.pem keys/stm32-dev-public.h --key-id 1
```

`keys/` is ignored. Keep private-key backups outside the repository and never put
private keys in release bundles. Set these variables to the existing keys or the
new pair just created:

```sh
export STM_PRIVATE_KEY="$PWD/keys/stm32-dev-private.pem"
export STM_PUBLIC_KEY="$PWD/keys/stm32-dev-public.pem"
export STM_PUBLIC_HEADER="$PWD/keys/stm32-dev-public.h"
mkdir -p build/images
scripts/build/build_stm32.sh --profile bootloader --public-key-header "$STM_PUBLIC_HEADER"
for slot in 0 1; do
  .venv/bin/python tools/firmware/sign.py \
    --private-key "$STM_PRIVATE_KEY" --slot "$slot" --version 0.6.0 \
    --build-id 0x60001 --wire 1.0 --management 1.0 \
    "firmware/stm32u083/application/build/release-reliability-slot$slot/multi_mcu_stm32_application.bin" \
    "build/images/slot$slot.mmfw"
  .venv/bin/python tools/firmware/verify_signed.py \
    "build/images/slot$slot.mmfw" --public-key "$STM_PUBLIC_KEY"
done
```

The slot ID and execution address are signed. Never write a slot-0-linked image to
Slot 1. Package versions describe the signed candidate; the runtime firmware's
reported version is compiled separately.

## First installation on a new board

This writes firmware. Confirm the intended probes before proceeding. Install
Nordic MCUboot plus its application, then the STM custom loader:

```sh
.venv/bin/west flash -d build/nrf54l15-host-reliability
STM32_Programmer_CLI -l stlink
STM32_Programmer_CLI -c port=SWD mode=UR reset=HWrst \
  -w firmware/stm32u083/bootloader/build/release/multi_mcu_stm32_bootloader.elf -v -rst
scripts/mmctl device list
export NRF_PORT=/dev/serial/by-id/YOUR_NORDIC_JLINK_PORT
```

With no boot journal or valid application, STM stays in the custom recovery
loader. Host status can be offline while update commands remain available. Install
its first signed Slot 0 image through the loader, which creates the pending/trial
metadata and lets the application confirm itself. A recovery-only board can exhaust
the host startup reset budget while you prepare files. Before the first transfer,
inspect status; if health is `failed`, clear its latch and immediately begin the
explicit update. Update ownership then suppresses automatic reset escalation:


```sh
scripts/mmctl --port "$NRF_PORT" status
# Only if health is failed on this known recovery-only installation:
scripts/mmctl --port "$NRF_PORT" --yes node recovery clear
scripts/mmctl --port "$NRF_PORT" --yes stm update build/images/slot0.mmfw --public-key "$STM_PUBLIC_KEY"
scripts/mmctl --port "$NRF_PORT" stm status
scripts/mmctl --port "$NRF_PORT" status
scripts/mmctl --port "$NRF_PORT" sensor sample
```

This initial-install sequence is supported by the boot/update state machines;
its fresh-board hardware acceptance must be recorded separately from previously
reported updates of an installed system.

Expect update `complete`, confirmed Slot 0, node `online`, nonzero session and a
sample. If this is not a genuinely new board, do not assume Slot 0 is available:
follow inactive-slot selection below. Merely programming a slot ELF does not install
its signed header or boot-selection metadata. Never flash the standalone application
at `0x08000000` over an installed custom bootloader. Do not erase boot metadata or
mass-erase as an installation shortcut.

## Ordinary signed STM32 update

Read the status and use the slot opposite the last confirmed image. For example,
with Slot 0 confirmed:

```sh
scripts/mmctl --port "$NRF_PORT" stm status
scripts/mmctl --port "$NRF_PORT" --yes stm update build/images/slot1.mmfw --public-key "$STM_PUBLIC_KEY"
scripts/mmctl --port "$NRF_PORT" stm status
scripts/mmctl --port "$NRF_PORT" node versions
scripts/mmctl --port "$NRF_PORT" sensor sample
```

Status 255 means unknown, not "either slot is safe". Use your last confirmed update
record; the bootloader still enforces confirmed/selected/pending-slot protection.
`error=-18, protocol=6` means a protected-slot refusal: inspect status and choose the
inactive image. Query timeout `-116` before transfer can indicate a missing loader,
wrong UART wiring or a bootloader-entry problem.

The PC validates package bounds, hardware, hash and signature before opening serial.
Nordic streams acknowledged chunks without staging a complete image. STM verifies
again before selecting pending. Completion requires trial confirmation and runtime
rediscovery, not just upload. Do not press reset during normal verification.

If a transfer is uncertain, run `stm status`. The CLI does not reconnect and replay
writes. Ctrl-C attempts a pre-commit abort; explicit `stm abort --yes` cannot cancel
an already committed pending transition. A deliberate failing trial may report
`rolled_back` and a nonzero CLI exit; the original confirmed application must return.
See [bootloader design](BOOTLOADER.md) and [failure tests](TESTING.md).

## Nordic image update

The reliability build uses MCUboot and serial MCUmgr through J-Link VCOM. The
current configuration uses the SDK development signing key; it is not a production
trust root. STM uses the separate P-256 key above.

Build a candidate with distinct version/content/hash in a separate checkout or
build directory and supply its `zephyr.signed.bin`. Uploading the same image twice
does not demonstrate replacement. Keep a known working build/probe rescue path.

```sh
scripts/mmctl --port "$NRF_PORT" host image list
scripts/mmctl --port "$NRF_PORT" --yes host image update path/to/candidate/zephyr.signed.bin
scripts/mmctl --port "$NRF_PORT" host image list
```

The CLI checks image structure/hash, uploads the secondary image, marks it for
trial, resets, then reconnects for read-only image state. MCUboot enforces the
signature. The application confirms only after initialization and compatible node
discovery. Inspect active/confirmed flags; do not use manual confirmation to bypass
failed self-tests. `dfu_application.zip` is not accepted by this CLI.

## STM32 SWD rescue

SWD can restore a damaged custom loader without changing boot options. Disconnect
power before modifying wires, identify the intended ST-LINK, then program only the
matching-key loader ELF as shown above. Do not write a standalone application over
it. A loader binary does not contain private key material, but its public trust root
must match your signed packages. Preserve boot/app metadata and application slots.

## Explicit factory-ROM repair

The automated path uses the existing BOOT0 and open-drain NRST wires, USART1
PA9/PA10 at 115200 **8E1**, and ST AN3155. It is an unauthenticated rescue path.
Before authorizing it, manually verify `NBOOT_SEL=0`, `nBOOT1=1`, `BOOT_LOCK=0`,
compatible voltage, default BOOT0 low and working SWD rescue. Read options only:

```sh
STM32_Programmer_CLI -c port=SWD mode=UR reset=HWrst -ob displ
```

Factory defaults can ignore BOOT0. If the required selection is absent, stop and
review the exact board manual and reversible boot-selection change separately.
The CLI never changes options/protection. Do not change RDP, BOOT_LOCK or write
protection to make this demo work. Remove any BOOT0-to-VDD jumper before Nordic
controls BOOT0. Keep other STM UART interfaces idle during ROM selection.

Create and validate a matching release bundle as described in
[release packaging](FINAL_RELEASE.md), then explicitly repair:

```sh
scripts/mmctl --port "$NRF_PORT" --yes stm recover build/release/release.json \
  --public-key "$STM_PUBLIC_KEY" --rom-prerequisites-verified
scripts/mmctl --port "$NRF_PORT" stm recover-status
```

The host checks device ID/capabilities, erases only bootloader pages 0–31, writes
with readback, then restores BOOT0 low, UART 8N1 and reset. Application slots and
metadata are preserved. If runtime is unavailable, the CLI can chain a signed
application recovery; it never flashes Nordic as a side effect. Restoration failure
retains exclusive UART ownership until explicit recovery.

An interrupted repair can leave a partial loader. `stm recover-abort --yes` restores
pins/UART but cannot undo erased flash; perform a full explicit repair or SWD rescue.
No automatic mutating reconnect, mass erase or metadata bypass is supported.

## Verification status and security limits

Normal signed slot updates, Nordic image update, manual ROM programming and
host-driven loader repair have user-reported hardware success. Destructive update
interruption, power-loss boundaries and deliberate STM trial rollback remain
separate verification gates; see [validation status](VALIDATION.md).

CRC32 detects transmission corruption. SHA-256 hashes and ECDSA authenticate STM
images through signed metadata; payloads and runtime UART traffic are not encrypted.
Open SWD/ROM rescue, development keys and unprovisioned protections mean this is
not production-secured boot. STM CRC/SHA-256/ECDSA currently execute in software.

References: [ST AN2606](https://www.st.com/resource/en/application_note/an2606-stm32-microcontroller-system-memory-boot-mode-stmicroelectronics.pdf),
[ST AN3155](https://www.st.com/resource/en/application_note/an3155-usart-protocol-used-in-the-stm32-bootloader-stmicroelectronics.pdf),
[NUCLEO board manual](https://www.st.com/resource/en/user_manual/um3256-stm32-nucleo64-boards-mb1932-stmicroelectronics.pdf).
