# Integrated two-board demonstration

This is the shortest end-to-end demonstration of the reusable host/node platform. It uses only the
nRF54L15-DK, NUCLEO-U083RC, their USB connections and the frozen jumper wiring. Virtual sensor and
actuator values are software simulations; they do not represent physical measurements or outputs.

## Safety and preparation

Use the exact boards and wiring in `hardware/PINOUT.md` and `hardware/WIRING.md`. The recorded test
configuration is PCA10156 1.0.0 and MB1932-U083RC-B01, with both I/O domains measured at 3.3 V.
USB-power the boards independently, connect only a common ground and the listed signals, and never
join their 3.3 V or 5 V rails. Keep RESET and BOOT0 disconnected during the normal demo.

From the repository root:

```sh
source ./.env.local
python3 scripts/doctor.py
cmake --preset native-debug
cmake --build --preset native-debug
ctest --preset native-debug --output-on-failure
.venv/bin/python -m pytest tools/tests -q
```

Build the normal personality-B pair:

```sh
cmake --preset debug-personality-b -S firmware/stm32u083/application
cmake --build firmware/stm32u083/application/build/debug-personality-b --parallel

cd .ncs-workspace
../.venv/bin/west build -b nrf54l15dk/nrf54l15/cpuapp \
  ../firmware/nrf54l15-host --build-dir ../build/nrf54l15-host --pristine=always
cd ..
```

Flash only after confirming the target probe:

```sh
STM32_Programmer_CLI -l st-link
STM32_Programmer_CLI -c port=SWD mode=UR reset=HWrst \
  -w firmware/stm32u083/application/build/debug-personality-b/multi_mcu_stm32_application.elf \
  -v -rst

cd .ncs-workspace
../.venv/bin/west flash -d ../build/nrf54l15-host
cd ..
```

## Normal workflow

Open the two independent USB log ports. Power or reset STM first, then reset Nordic. Expect Nordic
to progress through `discovering`, `identity`, `capabilities`, `services`, and `online`, identifying
`stm32u083rc-node`. It should request a deterministic virtual sensor sample every five seconds.

- Short-press the Nordic button: personality B starts one two-second virtual actuator operation.
  Expect one acceptance followed by 25%, 50%, 75%, and completion events with the same operation ID.
- Press and release the STM button: expect distinct node button pressed/released events.
- Reset STM: READY drops, Nordic leaves online state, then rediscovers a new session after STM boots.
  An unconfirmed application command is not automatically replayed across that session change.

## Lost IRQ and duplicate operation

These are bounded development exercises; all injections start disabled and reset clears them.

For lost/short IRQ behavior, hold the Nordic button for at least 1.5 seconds until injection 2 is
selected. Generate an STM button event. The deliberately short IRQ pulse may be missed, but UART
polling must still recover the event. Disable injections by continuing the long-press cycle to OFF
or reset STM. Optional probe placement and waveform expectations are in
`hardware/LOGIC_ANALYZER.md`.

For duplicate-operation behavior, select injection 4 (`DROP_ACCEPTANCE`) and short-press Nordic.
The first successful actuator acceptance is discarded, causing the bounded host retry. The node
must execute the operation once: progress/completion use one operation ID and the execution count
advances only once. This verifies replay of the cached response rather than re-execution.

A deterministic simulation is available without boards:

```sh
.venv/bin/python tools/fault-runner/fault_runner.py \
  scenario duplicate_operation --mode fixed --seed 7 --duration-ms 1000 \
  --csv /tmp/duplicate.csv --vcd /tmp/duplicate.vcd
```

The generated CSV/VCD is `SIMULATED`, never a hardware capture.

## Recovery and updates

The lifecycle paths are deliberately separate:

- Nordic: MCUboot/MCUmgr over the Nordic USB serial interface. Build, upload, test, reset and
  confirmation commands are in `docs/FIRMWARE_UPDATES.md`. Always build a candidate with a distinct
  version/hash; `image-upload` requires `--firmware` before the path.
- STM factory recovery: ST-LINK/SWD is primary; the factory ROM USART2 route is the manual fallback.
  Both are documented in `docs/FIRMWARE_UPDATES.md`. ROM recovery requires the documented BOOT0
  procedure and reversible `NBOOT_SEL=0`; do not alter RDP, BOOT_LOCK or write protection.
- STM signed A/B demonstration: independently linked Slot 0/Slot 1 images are delivered using the
  opt-in Nordic recovery client. Signing, bootloader provisioning and delivery are in
  `docs/BOOTLOADER.md`. Private keys stay outside Git.

## Hardware evidence status

User-observed hardware passes include normal two-board services, diagnostics, supervision,
Nordic OTA and rollback, STM ROM programming, and signed STM Slot 0/Slot 1 delivery with persistent
trial confirmation. Phase 7 trial-crash rollback, corrupt signed-image rejection, reset during STM
erase/program, corrupt metadata recovery, both-slot button checks, and post-bootloader ROM/SWD
rescue remain `NOT_RUN_HARDWARE`. Consult phase reports before treating an exercise as verified.
