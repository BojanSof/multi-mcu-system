# Phase 8 report — regression, CI, documentation and extraction readiness

Date: 2026-10-19
Base Git revision: `4cbdbc9`
Status: `COMPLETE`; integrated hardware smoke test passed with user-observed evidence.

## Delivered

- Canonical root-level native/Python, STM32, and Nordic build scripts that never flash hardware.
- Committed `west manifest --freeze --active-only` output for the exact active NCS workspace pins.
- GitHub Actions native regression plus an explicit self-hosted embedded-build workflow.
- Observation-only HIL runner with an electrical gate, explicit distinct serial ports, structured
  result labels, deterministic simulated fixtures, and no reset/flash/option-byte operations.
- Integrated user demo, release/version matrix, public C API contracts, and extraction guide.
- Phase 8 interface freeze and prominent preservation of deferred Phase 7 hardware checks.

## Dependency audit

`scripts/doctor.py` found CMake 4.3.1, Ninja 1.13.2, native GCC 16.2.1, Arm GNU 14.3.1 through
CubeCLT and the project Arm GNU 15.2.1 path, west 1.5.0, NCS 3.4.1/Zephyr 4.4.2,
STM32CubeProgrammer 2.23.0, J-Link 9.24a, and nrfutil 8.2.0 ready. Optional `cppcheck` is missing.
No USB serial devices are exposed to this execution environment and libusb reports `-99`.

## Verification

| Check | Result | Evidence |
|---|---|---|
| Canonical regression script | PASS | `scripts/test/run_native.sh` |
| Clean source-tree regression | PASS | exported tracked/candidate sources into `/tmp`; fresh configure/build and all 51 Python tests passed |
| Portable native C tests | PASS | 6/6 CTest |
| Bootloader state-machine tests | PASS | 2/2 CTest |
| Python tool and HIL-runner tests | PASS | 51/51 pytest |
| STM normal release application | PASS_BUILD | 23,280 text + 104 data; 9,440 bss |
| STM signed Slot 0 application | PASS_BUILD | 23,456 text + 104 data; 9,440 bss |
| STM signed Slot 1 application | PASS_BUILD | 23,456 text + 104 data; 9,440 bss |
| STM bootloader | PASS_BUILD | 49,528 text + 116 data; 2,104 bss |
| Nordic default host | PASS_BUILD | 50,008 flash; 12,984 RAM |
| Nordic MCUboot/MCUmgr profile | PASS_BUILD | application 77,592 flash/20,784 RAM; MCUboot 62,412 flash/27,984 RAM |
| Nordic STM bootloader query profile | PASS_BUILD | 43,184 flash; 12,216 RAM |
| Nordic embedded STM update profile | PASS_BUILD | 68,024 flash; 12,216 RAM; temporary signed Slot 1 image |
| HIL simulated fixture | PASS | deterministic fixture, explicitly labeled simulated |
| Phase 8 integrated board rerun | PASS_HARDWARE_USER | user confirmed the documented build, flash, discovery and service smoke test works |
| Deferred Phase 7 destructive/fault cases | NOT_RUN_HARDWARE | rollback, interruption, corrupt image/metadata and post-bootloader rescue remain deferred by user |

## Exact commands

```sh
.venv/bin/python scripts/doctor.py
scripts/test/run_native.sh
source ./.env.local
scripts/build/build_stm32.sh --public-key-header /tmp/phase7_public_key.h
scripts/build/build_nordic.sh --profile default
scripts/build/build_nordic.sh --profile phase6
scripts/build/build_nordic.sh --profile phase7-query
scripts/build/build_nordic.sh --profile phase7-update --stm-image /tmp/phase8-sign.XXXXXX/signed-slot1.bin
.venv/bin/python tests/hil/run_hil.py --scenario phase5_smoke \
  --fixture-host tests/hil/fixtures/phase5_host.log \
  --fixture-node tests/hil/fixtures/phase5_node.log
```

The `/tmp` public-key header was generated from an external development key; neither is tracked.
Real hardware observation requires the electrical checks and explicit command in `tests/hil/README.md`.

## Warnings and remaining work

- The pinned STM HAL emits known conversion/unused-parameter warnings; new project code builds.
- The bootloader linker reports an RWX load segment and unused newlib syscall warnings.
- The GitHub embedded job intentionally targets a separately administered `multi-mcu-lab`
  self-hosted runner containing the pinned SDK/toolchains, ignored `.env.local`, `.venv`, `.deps`,
  and external public-key header. Hosted native CI requires no board or embedded SDK.
- Optional `cppcheck` was not run. Install it on Arch with
  `sudo pacman -S --needed cppcheck` if that additional analyzer is desired.
- The user confirmed the documented integrated smoke workflow on attached boards. This does not
  change the explicit status of deferred destructive/fault-injection bootloader tests.
