# Phase 11 testing — run from project root

This explicitly replaces the STM **custom bootloader** via factory ROM. It is not
a routine application update. Keep the original matching signing keys and working
SWD recovery available. Do not change protection or option bytes with these tools.
Boards must have the Phase 10 supervision wires and verified 3.3 V supplies.

## 1. Manual gate (before any ROM command)

Remove BOOT0–VDD jumper. Nordic P1.08 → STM NRST, P1.14 → STM BOOT0;
application UART and GND unchanged. Power Nordic first. Verify BOOT0 low and NRST
released near STM VDD. Read option bytes, without writing:

```sh
source ./.env.local
STM32_Programmer_CLI -c port=SWD -ob displ
```

Require NBOOT_SEL=0, nBOOT1=1, BOOT_LOCK=0; readout/write protection must permit
bootloader read/program/erase. If anything differs, stop and review rather than
using unprotect/mass erase. The prerequisites CLI flag is your confirmation of
these checks, not automatic option-byte detection. No SWD sessions or transmitters
on either STM USART while the ROM transfer runs; do not type into STM log terminal.

## 2. Build and flash Nordic; build STM loader and slot candidates

```sh
scripts/build/build_nordic.sh --profile phase11
.venv/bin/west flash -d build/nrf54l15-host-phase11
scripts/build/build_stm32.sh --public-key-header keys/stm32-dev-public.h
scripts/mmctl status
```

Expected `rom_recovery:true`. Do **not** flash the standalone STM application.
Use your actual matching external key paths if different; never generate a new
key just to make a recovery package.

## 3. Build a validated release directory

```sh
mkdir -p build/phase11-release
cp build/nrf54l15-host-phase11/nrf54l15-host/zephyr/zephyr.signed.bin build/phase11-release/host.signed.bin
cp firmware/stm32u083/bootloader/build/release/multi_mcu_stm32_bootloader.bin build/phase11-release/stm-loader.bin
for slot in 0 1; do
  .venv/bin/python tools/firmware/sign.py \
    --private-key keys/stm32-dev-private.pem --slot "$slot" --version 0.11.0 \
    --build-id 0xb00 --wire 1.0 --management 1.0 \
    "firmware/stm32u083/application/build/release-slot$slot/multi_mcu_stm32_application.bin" \
    "build/phase11-release/slot$slot.mmfw"
done
.venv/bin/python tools/firmware/release_bundle.py create build/phase11-release/release.json \
  --system-version 0.11.0 --public-key keys/stm32-dev-public.pem \
  --nordic build/phase11-release/host.signed.bin \
  --bootloader build/phase11-release/stm-loader.bin \
  --slot0 build/phase11-release/slot0.mmfw --slot1 build/phase11-release/slot1.mmfw
.venv/bin/python tools/firmware/release_bundle.py verify build/phase11-release/release.json \
  --public-key keys/stm32-dev-public.pem
```

Expected `verified:true`; this is offline. A manifest with damaged/missing/escaping
paths must fail here before any hardware reset. Raw loader provenance is trusted
by the operator; its manifest digest is integrity, not a signature.

## 4. Repair a working loader first

```sh
scripts/mmctl stm recover build/phase11-release/release.json \
  --public-key keys/stm32-dev-public.pem --yes --rom-prerequisites-verified
scripts/mmctl stm recover-status
scripts/mmctl sensor sample
```

Expected ROM device ID 0x489, ACK+readback offsets reaching total, `complete`,
BOOT0 low, runtime back online and sensor works. Application/metadata regions are
not ROM-erased; Nordic firmware is not updated. Existing healthy app is preserved.
If no compatible app starts, CLI chains a signed custom-protocol application
install. Slot0 protection may cause a new inactive Slot1 job; guards are retained.

For ambiguous serial discovery add `--port /dev/serial/by-id/YOUR_JLINK_PORT`.
Close all other users of Nordic VCOM. Logs stay on RTT.

If status is `failed` **and** `active:true`, pin/UART restoration failed and runtime
traffic is deliberately blocked. Retry restoration explicitly:

```sh
scripts/mmctl stm recover-abort --yes
scripts/mmctl stm recover-status
```

If it stays active, stop and inspect hardware/use SWD; do not force runtime traffic.

## 5. Remaining acceptance (explicitly destructive, optional until ready)

First save the matching loader/bundle and prove SWD works. Confirm a normal repair
above before intentionally damaging anything. Do not erase any app/metadata pages.

- Interrupt transfer before finish with Ctrl-C: report partial-loader/abort,
  BOOT0 returns low. Explicitly rerun full `stm recover` to repair; do not blindly
  replay/resume the interrupted offset.
- Disconnect PC serial while retaining both board supplies: after 30 s, ROM job
  aborts/restores BOOT0/8N1. Reconnect and query recover-status before retrying.
- Corrupt-loader HIL: with **your explicit acceptance of temporarily losing normal
  STM boot**, erase **page0 only** using SWD, then close programmer and perform ROM
  repair. Never use `-e all`; ROM erase remains limited to loader pages0..31.
- Empty-app chaining is destructive to app contents and is not required for the
  first smoke test. Validate separately with saved signed candidates/SWD rescue;
  record journal state, chosen slot and confirmation. Do not erase metadata to
  conceal a protection failure or claim rollback from a normal image.

These cases are NOT_RUN_HARDWARE until actual observations are recorded. No option
bytes, readout protections or irreversible settings are changed by recovery.
