# Phase 10 hardware acceptance

All commands run from the project root. Use the existing signed A/B STM bootloader
and a working confirmed slot from Phase 7/8; do **not** flash the standalone
application over the bootloader. Keep SWD recovery available. No option-byte changes.

If the standalone application overwrote the bootloader, restore only the loader
with its original matching public key (no mass erase):

```sh
source ./.env.local
scripts/build/build_stm32.sh --profile bootloader --public-key-header keys/stm32-dev-public.h
STM32_Programmer_CLI -c port=SWD \
  -d firmware/stm32u083/bootloader/build/release/multi_mcu_stm32_bootloader.elf -v -rst
```

Query timeout `-116` before any bytes can indicate a missing loader. Error `-18`
with protocol 6 means slot-state protection rejected the target: inspect
`scripts/mmctl stm status` and choose the inactive slot, not the confirmed one.

## Wire and flash Nordic

Follow [the supervision wiring checklist](../hardware/WIRING.md#phase-10-supervision-addition)
with both boards powered off. Add only:

- Nordic P1 pin 11 (P1.08) → STM CN7 pin 14 (NRST).
- Nordic P1 pin 17 (P1.14) → STM CN7 pin 7 (BOOT0).

Remove any BOOT0–VDD jumper. Keep existing five wires. Power Nordic first.
Verify BOOT0 low and NRST released near 3.3 V before reset/update.

```sh
source ./.env.local
scripts/build/build_nordic.sh --profile phase10
.venv/bin/west flash -d build/nrf54l15-host-phase10
scripts/mmctl device list
scripts/mmctl status
scripts/mmctl node reset --yes
scripts/mmctl sensor sample
```

Expected: supervision/update capabilities true; reset completes, STM session changes,
node returns online, sensor works. Close other programs using Nordic VCOM; use
`--port /dev/serial/by-id/YOUR_JLINK_PORT` if discovery is ambiguous. Logs remain RTT.

## Build and sign candidates

Reuse the **existing** private/public key matching the installed STM bootloader.
Do not generate replacement keys. These ignored `keys/` paths are examples; substitute
your actual external paths. Build both slot-linked applications:

```sh
scripts/build/build_stm32.sh --profile slots
for slot in 0 1; do
  .venv/bin/python tools/firmware/sign.py \
    --private-key keys/stm32-dev-private.pem --slot "$slot" --version 0.10.0 \
    --build-id 0xa00 --wire 1.0 --management 1.0 \
    "firmware/stm32u083/application/build/release-slot$slot/multi_mcu_stm32_application.bin" \
    "build/phase10-slot$slot.mmfw"
done
scripts/mmctl stm image inspect build/phase10-slot1.mmfw --public-key keys/stm32-dev-public.pem
```

## Update and failure checks

If Slot 0 is currently confirmed, update Slot 1 first:

```sh
scripts/mmctl stm update build/phase10-slot1.mmfw --public-key keys/stm32-dev-public.pem --yes
scripts/mmctl stm status
scripts/mmctl sensor sample
scripts/mmctl stm update build/phase10-slot0.mmfw --public-key keys/stm32-dev-public.pem --yes
scripts/mmctl stm status
```

Expected: ACKed bytes reach total, final state `complete`, confirmed slot 1 then 0,
runtime rediscovered. Allow verification resets; do not press reset manually.
Initial idle status has unknown slots: choose opposite your previously confirmed
slot. A wrong-slot request is rejected before erase and returns to the old app.

Remaining checks (record outcome, do not claim them from native tests):

1. Repeat an update targeting the **currently confirmed** slot: fail safely; old app
   remains online. Job can briefly reset STM before its journal is checked.
2. Supply a damaged package/wrong public key: CLI rejects offline, no reset/erase.
3. Interrupt a transfer before finish (Ctrl-C): abort and old app returns. For cable
   interruption, disconnect **PC serial only**, not board power; after 30 seconds
   reconnect and run `scripts/mmctl stm status`. Do not restart/replay automatically.
4. After finish, close the CLI: Nordic must finish verification autonomously; query
   status when reconnected. No abort once pending commit starts.
5. Use the previously established Phase 7 signed trial-crash candidate for the
   inactive slot: expect `rolled_back` and previous confirmed app online. Do not
   substitute a normal candidate and call that a rollback test.

Hardware acceptance remains pending until both slot updates, refusal, interruption
and rollback results are recorded. Factory-ROM repair is Phase 11, not this phase.
