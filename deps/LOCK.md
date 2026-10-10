# Dependency lock

Phase 1 dependency decisions are owned by the integrator. Downloaded SDK trees and generated
artifacts are not committed.

| Dependency | Pin | License | Source / note |
|---|---|---|---|
| nRF Connect SDK | `v3.4.1` | mixed; see upstream manifest | `west.yml`; current stable 3.4 LTS patch selected, not a preview |
| Zephyr | resolved by NCS `v3.4.1` manifest | Apache-2.0 | record exact revisions with `west manifest --freeze` after update |
| stm32-cmake | gitlink `ecc5acc082effc4793eb7cbed8d8b7bd4896ca6e` | MIT | pinned submodule revision with upstream STM32U0 support |
| STM32CubeU0 CMSIS/HAL | `UNRESOLVED_BLOCKED_NETWORK` | BSD-3-Clause/ST licenses | must be pinned and frozen after the stm32-cmake checkout can be inspected and a build succeeds |
| FreeRTOS-Kernel | `V11.3.0` (202604 LTS) | MIT | Cortex-M0 `ARM_CM0` port; exact resolved commit to be recorded after fetch |
| Python tools | see `tools/requirements.lock` | per-package | project-local `.venv` only |
| Arm GNU Toolchain | `15.2.Rel1` (`GCC 15.2.1`, binutils `2.45.1`) | GPL and component licenses | local `/home/bojan/toolchains/arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi`; not vendored |
| STM32CubeCLT | `1.22.0` (bundled CubeProgrammer `2.23.0`) | ST license | local `/home/bojan/st/stm32cubeclt_1.22.0`; not vendored |

## Reproduction

```sh
git submodule update --init --recursive
west init -l .
west update
west manifest --validate
west manifest --freeze > deps/west-manifest-freeze.yml
```

The stm32-cmake gitlink is pinned above. CubeU0's transitive repositories must be frozen after the
submodule is populated and before an STM32 firmware build or Phase 1 dependency lock is accepted.
