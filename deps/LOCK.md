# Dependency lock

Phase 1 dependency decisions are owned by the integrator. Downloaded SDK trees and generated
artifacts are not committed.

| Dependency | Pin | License | Source / note |
|---|---|---|---|
| nRF Connect SDK | `v3.4.1` | mixed; see upstream manifest | `west.yml`; current stable 3.4 LTS patch selected, not a preview |
| Zephyr | resolved by NCS `v3.4.1` manifest | Apache-2.0 | record exact revisions with `west manifest --freeze` after update |
| stm32-cmake | gitlink `ecc5acc082effc4793eb7cbed8d8b7bd4896ca6e` | MIT | pinned submodule revision with upstream STM32U0 support |
| CMSIS core | `v5.6.0`, commit `96d6da4e252b06dcfdc041e7df23e86161c33007` | Apache-2.0 | resolved from the pinned stm32-cmake U0 dependency declaration |
| STM32CubeU0 CMSIS device | `v1.0.0`, commit `056d6c5e23eb33ed6ad2fad6bd4ce6e5b0e719a4` | Apache-2.0 | ignored checkout at `.deps/cmsis-device-u0` |
| STM32CubeU0 HAL | `v1.0.0`, commit `8a3e984efa69f3d0bf240e853b1cae8f8f45b5a7` | BSD-3-Clause | ignored checkout at `.deps/stm32u0xx-hal-driver` |
| FreeRTOS-Kernel | `V11.3.0` (202604 LTS), commit `9b777ae5c5b8e9e456065a00294d1e5f5f9facf5` | MIT | Cortex-M0 `ARM_CM0` port; ignored checkout at `.deps/FreeRTOS-Kernel` |
| Python tools | see `tools/requirements.lock` | per-package | project-local `.venv` only |
| Arm GNU Toolchain | `15.2.Rel1` (`GCC 15.2.1`, binutils `2.45.1`) | GPL and component licenses | local `/home/bojan/toolchains/arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi`; not vendored |
| STM32CubeCLT | `1.22.0` (bundled CubeProgrammer `2.23.0`) | ST license | local `/home/bojan/st/stm32cubeclt_1.22.0`; not vendored |
| nRF Util MCU Manager | `0.12.0`, commit `4c34b2064cbfdd5e545e397ee2abd840aee9368a` | see Nordic command package | install with `nrfutil install mcu-manager`; serial SMP client for Phase 6 |

## Reproduction

```sh
git submodule update --init --recursive
west init -l .
west update
west manifest --validate
west manifest --freeze > deps/west-manifest-freeze.yml
```

The STM dependency revisions above were resolved from their signed/annotated release tags and
verified by a successful application cross-build. Local source trees remain ignored and are never
vendored into the repository.
