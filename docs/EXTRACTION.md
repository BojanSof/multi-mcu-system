# Component extraction guide

The repository is intentionally a set of portable libraries, removable examples and target
adapters. Extraction should preserve those dependency directions instead of copying sources into a
new common directory.

## Package boundaries

| Package | Portable dependencies | Target-specific dependencies | Extraction note |
|---|---|---|---|
| `libraries/protocol` | C11 only | None | Move with `cmake/Sources.cmake`, public headers, tests and Zephyr module |
| `libraries/node-api` | C11 only | None | Generic management/service registry; do not add board or example IDs here |
| `libraries/diagnostics` | C11 only | None | Bounded records/injection state; logging backends stay in firmware/tools |
| `libraries/firmware-image` | C11 only | None | Format parsing/compatibility; crypto and flash writes remain adapters |
| `libraries/boot-protocol` | C11 only | None | Message encoding only; not a UART or flash implementation |
| `libraries/stm32-rom-boot` | C11 only | None | Bounded restricted ROM transactions; adapter owns pins/parity |
| `examples/virtual-io` | `NodeApi::NodeApi` | None | Optional demonstration; may be removed without changing management/wire APIs |
| Nordic host firmware | Zephyr/NCS plus portable modules | nRF54L15-DK | Owns discovery, supervision and update orchestration, not node internals |
| STM application | portable modules, FreeRTOS | STM32U0 HAL/platform | Owns runtime services; no boot signature or slot-selection policy |
| STM bootloader | firmware-image, boot-protocol, crypto | STM32U0 flash/UART/startup | Independent build; no FreeRTOS or application services |

Each portable package exports its namespaced CMake alias (`Protocol::Protocol`,
`NodeApi::NodeApi`, `Diagnostics::Diagnostics`, `FirmwareImage::FirmwareImage`, or
`BootProtocol::BootProtocol`, or `Stm32RomBoot::Stm32RomBoot`) and keeps its source inventory in `cmake/Sources.cmake`. Keep that
single inventory shared by native and Zephyr builds.

## Extraction procedure

1. Copy one package directory intact, including its license information, public headers, tests,
   `cmake/Sources.cmake`, and `zephyr/` integration where present.
2. Preserve C11, bounded caller-owned storage and the published names/types. Do not introduce an
   SDK dependency into a portable package.
3. Run the package natively with warnings as errors before integrating a target adapter.
4. For Zephyr, add the package directory to `EXTRA_ZEPHYR_MODULES` before
   `find_package(Zephyr ...)`; never fork a second source copy.
5. For STM, add the package with `add_subdirectory()` from its independent cross-build. Apply CPU
   flags to that target in the firmware project, not in the portable package.
6. Treat wire constants, service IDs, signed canonical bytes, `flash_layout.yaml`, pin assignments
   and persistent metadata as frozen migration contracts. Version deliberate changes.
7. Recreate external dependencies from `deps/LOCK.md`; do not transfer `.deps/`, `.ncs-workspace/`,
   `.venv/`, build trees, captures, generated images or keys.

## Adapter seams

The protocol encoder/stream decoder accepts byte buffers and callbacks; UART ownership belongs to
`HostTransport` or `NodeTransport`. Node services use a registry callback rather than HAL calls.
Diagnostics stores structured records but does not print them. Firmware-image parsing validates
metadata but delegates hashing/signature verification. Boot-protocol parses messages while the STM
boot engine owns flash safety and replay state.

These seams make replacement straightforward: a different RTOS, UART driver, log sink or crypto
backend should require a target adapter, not changes to public portable headers.

## What must remain project-specific

- Board overlays, pinctrl, READY/IRQ polarity and voltage validation.
- STM startup, linker scripts, vector relocation and flash driver.
- Nordic MCUboot partitions and MCUmgr transport.
- Public-key provisioning, private-key custody and release signing policy.
- Application composition and optional virtual-I/O personality.

Do not extract the development fault controls into a production build by accident. Generated
artifacts remain untracked, and hardware claims remain tied to the exact board revision and validation evidence that produced them. See [VALIDATION.md](VALIDATION.md).
