# Dependency lock

Dependency changes are reviewed with the integration and compatibility contracts. Downloaded SDK trees and generated
artifacts are not committed.

| Dependency | Pin | License | Source / note |
|---|---|---|---|
| nRF Connect SDK | `v3.4.1` | mixed; see upstream manifest | `west.yml`; current stable 3.4 LTS patch selected, not a preview |
| Zephyr | resolved by NCS `v3.4.1` manifest | Apache-2.0 | active project revisions frozen in `deps/west-manifest-freeze.yml` |
| stm32-cmake | gitlink `ecc5acc082effc4793eb7cbed8d8b7bd4896ca6e` | MIT | pinned submodule revision with upstream STM32U0 support |
| CMSIS core | `v5.6.0`, commit `96d6da4e252b06dcfdc041e7df23e86161c33007` | Apache-2.0 | resolved from the pinned stm32-cmake U0 dependency declaration |
| STM32CubeU0 CMSIS device | `v1.0.0`, commit `056d6c5e23eb33ed6ad2fad6bd4ce6e5b0e719a4` | Apache-2.0 | ignored checkout at `.deps/cmsis-device-u0` |
| STM32CubeU0 HAL | `v1.0.0`, commit `8a3e984efa69f3d0bf240e853b1cae8f8f45b5a7` | BSD-3-Clause | ignored checkout at `.deps/stm32u0xx-hal-driver` |
| FreeRTOS-Kernel | `V11.3.0` (202604 LTS), commit `9b777ae5c5b8e9e456065a00294d1e5f5f9facf5` | MIT | Cortex-M0 `ARM_CM0` port; ignored checkout at `.deps/FreeRTOS-Kernel` |
| Python tools | see `tools/requirements.lock` | per-package | project-local `.venv` only |
| Arm GNU Toolchain | `15.2.Rel1` (`GCC 15.2.1`, binutils `2.45.1`) | GPL and component licenses | configure the external install with `STM32_TOOLCHAIN_PATH`; not vendored |
| STM32CubeCLT | `1.22.0` (bundled CubeProgrammer `2.23.0`) | ST license | configure the external install with `STM32_CUBE_CLT_PATH`; not vendored |
| nRF Util MCU Manager | `0.12.0`, commit `4c34b2064cbfdd5e545e397ee2abd840aee9368a` | see Nordic command package | install with `nrfutil install mcu-manager`; serial SMP image-management client |
| micro-ecc | commit `541b3a78026420a3e369c4c9281c396b5e531113` | BSD-2-Clause | pinned submodule; P-256 verification only in the STM32 development bootloader |
| Mbed TLS | `v3.6.7`, commit `068ff080b369adfac81509f9b57b2afabaf82dc5` | Apache-2.0 | pinned submodule; SHA-256 implementation for signed STM32 images |
| Python cryptography | `49.0.0` | Apache-2.0 OR BSD-3-Clause | project-local signing and verification only; private keys remain outside Git |
| pyserial | `3.5` | BSD-3-Clause | project-local USB serial CLI transport |
| cbor2 | `6.1.5` | MIT | project-local SMP CBOR codec; MCU uses pinned SDK zcbor |

## Reproduction

Follow [SETUP.md](../docs/SETUP.md) for the root-invoked NCS workspace and pinned
STM32 dependency checkouts. Verify exact revisions from the table rather than
substituting a newer release. Initialize all submodules with:

```sh
git submodule update --init --recursive
```

`deps/west-manifest-freeze.yml` records the application-active NCS project revisions.
Disabled manifest groups may remain uncloned and are not represented by unverified
SHAs. SDKs, toolchains and dependency source trees are external/ignored, not vendored
in this repository. Project MIT licensing does not replace their upstream licenses.
