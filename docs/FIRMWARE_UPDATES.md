# Firmware updates and recovery

This document separates the two Phase 6 update paths. The Nordic host uses MCUboot/MCUmgr. The
STM32 application has no in-application updater in Phase 6; its supported recovery path is the
on-board ST-LINK over SWD. The STM32 factory ROM procedure below is a manual recovery exercise,
not an nRF-controlled update path and not an atomic or signed update.

## Nordic MCUboot/MCUmgr update

Phase 6 uses SMP over the Nordic DK USB serial port; BLE is not required. The normal host build is
unchanged. Build the explicit MCUboot profile from the repository root:

```sh
source ./.env.local
cd .ncs-workspace
../.venv/bin/west build -b nrf54l15dk/nrf54l15/cpuapp ../firmware/nrf54l15-host \
  --build-dir ../build/nrf54l15-host-phase6 --pristine=always -- \
  -DEXTRA_CONF_FILE=phase6_mcumgr.conf \
  -DEXTRA_DTC_OVERLAY_FILE=boards/nrf54l15dk_nrf54l15_cpuapp_phase6_mcumgr.overlay \
  -DSB_EXTRA_CONF_FILE="$PWD/../firmware/nrf54l15-host/sysbuild_phase6_mcumgr.conf"
cd ..
```

The frozen RRAM map is MCUboot 68 KiB, primary slot 696 KiB, secondary slot 696 KiB, and storage
64 KiB. Both child builds consume the same overlay. The generated artifact uses the NCS default
development Ed25519 key; that key is public and **not secure for production**. Phase 7 will address
project key handling. Do not add private keys to Git.

Initial installation writes MCUboot and the primary application over the DK debug probe:

```sh
source ./.env.local
cd .ncs-workspace
../.venv/bin/west flash -d ../build/nrf54l15-host-phase6
cd ..
```

Build a distinct OTA candidate before testing. This preserves the flashed baseline image at version
`0.6.0` and creates a version `0.6.1` candidate with a different signed hash:

```sh
source ./.env.local
.venv/bin/west build \
  --source-dir firmware/nrf54l15-host \
  --build-dir build/nrf54l15-host-phase6-update \
  --board nrf54l15dk/nrf54l15/cpuapp \
  --pristine=always -- \
  -DEXTRA_CONF_FILE='phase6_mcumgr.conf;phase6_update_test.conf' \
  -DEXTRA_DTC_OVERLAY_FILE=boards/nrf54l15dk_nrf54l15_cpuapp_phase6_mcumgr.overlay \
  -DSB_EXTRA_CONF_FILE="$PWD/firmware/nrf54l15-host/sysbuild_phase6_mcumgr.conf"
```

Do not flash this candidate through SWD for the OTA test. Close every terminal using the Nordic
VCOM, identify its `/dev/ttyACM*` path, and substitute it below. Upload targets the inactive slot;
it does not activate the image until `image-test` and reset. Copy the hash printed by `image-list`
for the secondary version `0.6.1` image:

```sh
NRF_PORT=/dev/ttyACM0
nrfutil --log-output=stdout mcu-manager serial image-list --serial-port "$NRF_PORT"
nrfutil --log-output=stdout mcu-manager serial image-upload \
  --firmware build/nrf54l15-host-phase6-update/nrf54l15-host/zephyr/zephyr.signed.bin \
  --image-number 0 --serial-port "$NRF_PORT"
nrfutil --log-output=stdout mcu-manager serial image-list --serial-port "$NRF_PORT"
nrfutil --log-output=stdout mcu-manager serial image-test --hash <SECONDARY_HASH> \
  --serial-port "$NRF_PORT"
nrfutil --log-output=stdout mcu-manager serial reset --serial-port "$NRF_PORT"
```

The normal image confirms itself only after GPIO and UART initialization plus complete compatible
node discovery reach `online`. `image-list` must then show version `0.6.1` active and confirmed.
Manual
`image-confirm` is available for diagnosis, but bypasses that application self-test and is not the
normal procedure.

For a controlled rollback exercise, build a separate version `0.6.2` candidate using
`phase6_rollback_test.conf` and add `-DMULTI_MCU_PHASE6_UNCONFIRMED_TRIAL_DEMO=ON`, using build
directory `build/nrf54l15-host-phase6-unconfirmed`. Upload and test its `zephyr.signed.bin`; after
its first boot the log says it deliberately remains unconfirmed. Issue another MCUmgr reset (or
press RESET) and MCUboot must revert to the last confirmed image. Record both `image-list` results.
Never use the unconfirmed profile as a release image.

An interrupted upload leaves the active slot intact. Reopen the serial port, run `image-list`, and
repeat the complete upload. A corrupted or unsigned binary must be rejected during boot validation;
do not mark it confirmed. These update, interruption, and rollback observations remain
`NOT_RUN_HARDWARE` until performed on the DK.

## STM32 safety boundary

These instructions apply to `NUCLEO-U083RC`, board `MB1932-U083RC-B01`, with its factory
STLINK-V2EC and STM32U083RCT6. They do not change option bytes or protection. In particular, do not
use CubeProgrammer `-ob`, `-rdu`, or protection commands. Keep readout protection disabled as the
board manual requires.

Before changing jumpers or signal wires:

1. Disconnect both boards from USB power.
2. Remove all five nRF-to-STM application wires. This prevents PA10 traffic from accidentally
   selecting ROM USART1 and leaves recovery independent of the Nordic firmware.
3. Do not connect either board's 3.3 V or 5 V rail to the other board.

The commands assume the repository root and the local environment file:

```sh
source ./.env.local
STM32_IMAGE=firmware/stm32u083/application/build/debug-personality-b/multi_mcu_stm32_application.elf
test -r "$STM32_IMAGE"
STM32_Programmer_CLI --version
```

Build the image first if necessary:

```sh
cmake --preset debug-personality-b -S firmware/stm32u083/application
cmake --build firmware/stm32u083/application/build/debug-personality-b --parallel
```

## Primary STM32 recovery: ST-LINK/SWD

SWD is the first recovery choice because it needs no BOOT0 or UART routing change. Connect only the
STM board's ST-LINK USB connector to the PC. List probes and make a read-only connection:

```sh
STM32_Programmer_CLI -l st-link
STM32_Programmer_CLI -c port=SWD mode=UR reset=HWrst
```

`mode=UR` connects under hardware reset, which is useful when damaged application code immediately
faults, sleeps, or reconfigures debug pins. Program, byte-verify, and reset in one operation:

```sh
STM32_Programmer_CLI -c port=SWD mode=UR reset=HWrst \
  -w "$STM32_IMAGE" -v -rst
```

The operation passes only if CubeProgrammer reports both download and verification successful. Open
the STM VCP afterward and confirm the expected application banner. If power or USB is interrupted,
reconnect and repeat the same command; no resume state is assumed.

### Optional backup and explicit full erase

Normal ELF programming erases the required flash pages, so a full erase is not normally necessary.
Before an intentional full erase, save all 256 KiB of main flash to a location outside the Git tree:

```sh
STM32_Programmer_CLI -c port=SWD mode=UR reset=HWrst \
  -u 0x08000000 0x40000 /tmp/stm32u083-main-flash-backup.bin
```

Only when losing the existing application is acceptable:

```sh
STM32_Programmer_CLI -c port=SWD mode=UR reset=HWrst -e all
STM32_Programmer_CLI -c port=SWD mode=UR reset=HWrst \
  -w "$STM32_IMAGE" -v -rst
```

Do not erase merely to test interruption. Phase 6 has no STM A/B rollback; an interrupted erase or
program leaves the application unavailable until this recovery command succeeds.

## Manual STM32 factory-ROM recovery over ST-LINK VCP

ST AN2606 Rev 70 groups STM32U073xx/83xx together. Their factory bootloader uses activation
Pattern 11 and supports USART2 RX/TX on PA3/PA2. USART initialization is 8 data bits, even parity,
one stop bit, with `0x7f` synchronization. UM3256 Rev 2 shows that the board's default VCP routing is
USART2 on PA2/PA3. This keeps ROM recovery separate from the PA9/PA10 application link.

The STM32U083 factory defaults observed on the test board were `NBOOT_SEL=1`, `nBOOT0=1`,
`nBOOT1=1`, and `BOOT_LOCK=0`. That configuration ignores the physical BOOT0 pin and boots main
flash. The hardware recovery test therefore changed only the reversible `NBOOT_SEL` option byte to
`0`, verified the readback, and exercised ROM USART programming. `NBOOT_SEL=0` is retained as the
development configuration so the physical recovery jumper remains usable; the board's default
BOOT0 pull-down selects main flash when the jumper is absent.
Never change RDP, `BOOT_LOCK`, write protection, `nBOOT0`, or `nBOOT1` for this test.

With the BOOT0 jumper removed, enable the physical pin and verify the readback over SWD:

```sh
STM32_Programmer_CLI -c port=SWD mode=UR reset=HWrst -ob NBOOT_SEL=0
STM32_Programmer_CLI -c port=SWD mode=UR reset=HWrst -ob displ
```

Continue only when `NBOOT_SEL=0`, `nBOOT1=1`, and `BOOT_LOCK=0` are reported.

1. With USB disconnected and the application wires removed, place a removable 2.54 mm jumper
   between CN7 pin 5 (`VDD`) and CN7 pin 7 (`BOOT0`). UM3256 states BOOT0 defaults low and this
   jumper sets it high. Do not use CN7 pin 6 (`E5V`) or CN7 pin 8 (`GND`).
2. Connect the STM ST-LINK USB cable. Press and release B2 RESET once so BOOT0 is sampled high.
3. Find the ST-LINK VCP. On Linux, identify it by device metadata rather than assuming `ACM0`:

   ```sh
   STM32_Programmer_CLI -l uart
   udevadm info --query=property --name=/dev/ttyACM0 | \
     grep -E 'ID_VENDOR|ID_MODEL|ID_SERIAL'
   ```

4. Substitute the identified port below. Do not have a terminal emulator open on that port:

   ```sh
   STM32_ROM_PORT=/dev/ttyACM0
   STM32_Programmer_CLI -c port="$STM32_ROM_PORT" br=115200 P=EVEN db=8 sb=1 fc=OFF
   STM32_Programmer_CLI -c port="$STM32_ROM_PORT" br=115200 P=EVEN db=8 sb=1 fc=OFF \
     -w "$STM32_IMAGE" -v
   ```

5. Disconnect STM USB power, remove the CN7 5-to-7 BOOT0 jumper, reconnect USB, and press B2. Confirm
   the normal application banner and Nordic discovery only after restoring the verified application
   wiring with both boards unpowered.

6. Verify the development boot policy before reconnecting application wiring:

   ```sh
   STM32_Programmer_CLI -c port=SWD mode=UR reset=HWrst -ob displ
   ```

   The final display must report `NBOOT_SEL=0`, `nBOOT1=1`, and `BOOT_LOCK=0`. Keep the BOOT0
   jumper removed for normal main-flash boot. `NBOOT_SEL=1` may be restored later over SWD if a
   finalized deployment should ignore the physical BOOT0 pin.

If the UART operation is interrupted, keep BOOT0 high, power-cycle, press B2, and repeat the complete
connect/write/verify sequence. If UART recovery cannot reconnect, remove the BOOT0 jumper and use
the SWD procedure. Do not mass-erase or change option bytes as a troubleshooting shortcut.

### ROM procedure acceptance status

The user verified ROM identification, programming, and byte verification on the
NUCLEO-U083RC. Interrupted ROM programming recovery remains a separate optional exercise.

No automated nRF-driven BOOT0/NRST path is included. RESET remains subject to an open-drain-only
electrical design and explicit approval; Phase 6 does not add those wires.

## Authoritative STM references

- [AN2606 Rev 70, system-memory boot mode](https://www.st.com/resource/en/application_note/an2606-stm32-microcontroller-system-memory-boot-mode-stmicroelectronics.pdf), section 91 for STM32U073xx/83xx and activation Pattern 11 in Table 2.
- [AN3155, USART protocol used in the STM32 bootloader](https://www.st.com/resource/en/application_note/an3155-usart-protocol-used-in-the-stm32-bootloader-stmicroelectronics.pdf).
- [UM3256 Rev 2, MB1932 Nucleo-64 board](https://www.st.com/resource/en/user_manual/um3256-stm32-nucleo64-boards-mb1932-stmicroelectronics.pdf), sections 7.3, 7.7, 7.11 and 8.2.
- [RM0503, STM32U0 reference manual](https://www.st.com/resource/en/reference_manual/rm0503-stm32u0-series-advanced-armbased-32bit-mcus-stmicroelectronics.pdf), boot configuration and option-byte loading.
