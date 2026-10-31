# Phase 9 — USB operator control

Date: 2026-10-20. Base Git revision: `5899760` (uncommitted implementation changes).
Status: software implemented; user confirmed runtime CLI smoke test works.
Nordic OTA through the new CLI and extended HIL remain `NOT_RUN_HARDWARE`.

Follow-up correction (2026-10-20): the first reported Nordic Phase 9 build silently
omitted the control group because application-specific CMake options were not
imported from sysbuild. The earlier 77,264-byte image was therefore not a working
CLI image. The wrapper now scopes the option to `nrf54l15-host`; the child imports
sysbuild values with `zephyr_get`, and the build fails unless both the child cache
and link map prove control is included. Three wrapper regressions exercise correct
forwarding plus missing-option/missing-handler failures. Enabling the previously
excluded source also exposed and fixed CBOR header ordering and missing entropy
driver selection for the host boot ID. The CLI explains unsupported group errors.
Phases 10–12 remain planned in `docs/POST_BASELINE_PLAN.md`.

## Delivered

- Updated development plan/agent ownership to cover Phases 9–12; froze SMP v2 group
  64/API v1 in `docs/CONTROL_API.md` before parallel Nordic and CLI implementation.
- Opt-in `phase9` Nordic MCUboot profile, fixed command mailbox, 8-request replay
  cache, host boot-ID guard and 16-record history with four-record pagination.
- Main-thread ownership of node manager; typed result/event/error callbacks and
  cancellation on node session loss. Existing demonstration profiles remain selectable.
- Root CLI `scripts/mmctl`: discovery, status/services, sensor/actuator, lifecycle,
  diagnostics, generic calls, event watching and standard Nordic image management.
- Firmware hash preflight, inactive Nordic image verification, single reset and
  read-only reconnect; no mutating request replay across a host reboot.
- Phase 9 uses RTT logs and SMP-only VCOM to avoid character-wise log corruption.
- Pinned pyserial 3.5/cbor2 6.1.5; native control-state and fake-serial CLI regressions;
  self-hosted CI control-profile build; operator instructions in `docs/CLI.md`.

## Dependency audit

Arch Linux x86_64, Python 3.14.8, CMake 4.3.1, Ninja 1.13.2, native GCC 16.2.1,
Arm GNU 15.2.1, Zephyr SDK 1.0.1/GCC 14.3.0, west 1.5.0, NCS 3.4.1/Zephyr 4.4.2,
J-Link 9.24a, nrfutil 8.2.0, MCU Manager 0.12.0 and CubeProgrammer 2.23.0 available.
No required non-Python build tool is missing. Optional cppcheck is absent; user
install command on Arch: `sudo pacman -S --needed cppcheck`. No ttyACM/ttyUSB is
visible; libusb enumeration returns -99. No flash, wire change or option-byte write
was performed. Connect boards and verify ports with `scripts/mmctl device list`.

## Verification

| Check | Result |
|---|---|
| Native libraries/control | PASS: 8/8 CTest (includes optional pinned-SDK CBOR integration) |
| Bootloader state machines | PASS: 2/2 CTest |
| Python tools/CLI/HIL-runner | PASS: 87/87 pytest, including 33 CLI and 3 build-wrapper cases |
| Venv consistency | PASS: pip check |
| CLI help/device discovery | PASS: device list returns empty devices, accurately |
| STM normal application | PASS_BUILD: 23,280 text + 104 data; 9,440 bss |
| STM Slot 0 and Slot 1 | PASS_BUILD: each 23,456 text + 104 data; 9,440 bss |
| STM bootloader | PASS_BUILD: 49,528 text + 116 data; 2,104 bss |
| Nordic default | PASS_BUILD: 50,264 flash; 12,992 RAM |
| Nordic Phase 7 query | PASS_BUILD: 43,184 flash; 12,216 RAM |
| Nordic Phase 9 (corrected) | PASS_BUILD: 78,640 flash; 30,272 RAM; MCUboot 62,412 flash/27,984 RAM; `HostControl_Register`, `HostControl_StatusRead`, `HostControl_CallWrite` verified in final ELF |
| Runtime CLI smoke on boards | PASS_HARDWARE_USER: user confirmed it works after corrected Nordic build |
| Nordic OTA through new CLI | NOT_RUN_HARDWARE |
| Runtime stack high-water marks | NOT_RUN_HARDWARE; configured main 3072 and system workqueue 4096 bytes |
| Physical STM reset/automatic updates/ROM repair | NOT_IMPLEMENTED: Phases 10–11 |

Control-state tests exercise production admission/replay/conflict/boot-ID logic,
offline/unsupported/busy cases, malformed/oversized requests, cancellation, cache
rollover, history overflow/pagination and ID wrap with narrow SDK boundary doubles.
Python tests cover SMP golden header/CRC, fragmentation, partial reads, log lines,
bounded buffers/timeouts, exact retry bytes, errors, command gates, service schemas,
negative sensor values, node reboot guards and MCUboot image/hash preflight.
The optional `host_control_real_cbor` target compiles the pinned SDK zcbor and
MCUmgr bulk decoder against production handlers. It verifies nested status/services
and four maximum payload events (687 CBOR bytes within 1016 usable bytes), and rejects
missing/duplicate/wrong-type/truncated/oversized inputs. Without the NCS workspace,
the seven portable/state tests still run. Actual generated Nordic `.signed.bin`
preflight passed, including NCS's fixed-ROM flag/address and SHA-512 TLV; both
SHA-256 and SHA-512 image IDs are supported. These are native/build results, not
serial or board evidence.

## Commands run

```sh
source ./.env.local
python3 scripts/doctor.py
scripts/test/run_native.sh
scripts/build/build_stm32.sh --profile application
scripts/build/build_stm32.sh --profile slots
scripts/build/build_stm32.sh --profile bootloader --public-key-header keys/stm32-dev-public.h
scripts/build/build_nordic.sh --profile default
scripts/build/build_nordic.sh --profile phase9
.venv/bin/west build -d build/nrf54l15-host-phase9
scripts/build/build_nordic.sh --profile phase7-query
scripts/mmctl --help
scripts/mmctl device list --json
git diff --check
```

The existing ignored public-key header was supplied to the bootloader; private
keys were not read or modified. An incremental Phase 7 query build initially failed
on malformed generated Kconfig strings in its old cache; the canonical pristine
build command above fixed it and passed. Known SDK deprecated-symbol and STM
newlib/RWX linker warnings remain.

## Hardware acceptance (user)

User reported runtime commands working after the build fix. The reported injection
3 rejection (service 2, command 2, status 6) matches the intentional Release build
exclusion of development faults; it is not a CLI transport failure. Fault injection
requires a compatible Debug STM image and was not validated on the Release image.
No user evidence was supplied for new-CLI OTA or the extended reset-during-operation
exercise; those remain pending.

Follow `docs/CLI.md` from the root: build/flash Phase 9 Nordic, leave STM firmware
and current five wires, close the Nordic log terminal, choose its J-Link VCOM.
Verify online status, services, node versions, signed sensor value/index, actuator
acceptance then 25/50/75/completion in event watch, lifecycle enter/exit and diagnostics
injection/clear/drain. Test the CLI Nordic update with a distinct signed candidate,
then inspect active/confirmed flags. Press STM reset during an operation and verify
terminal cancellation/new discovery without repeat execution. Record actual results
before marking the phase hardware-verified. Do not connect planned BOOT0/NRST wires
until the Phase 10 exact-board electrical gate is completed.
