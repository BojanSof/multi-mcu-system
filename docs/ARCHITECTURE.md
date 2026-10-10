# Architecture and frozen Phase 1 boundaries

The nRF54L15-DK is the host and the STM32U083RC is the managed node. Portable libraries contain no
board or SDK dependencies. The Nordic Zephyr and STM32 HAL/FreeRTOS applications are independently
configured and never participate in the root native build.

Frozen reusable target names are `Protocol::Protocol`, `NodeApi::NodeApi`,
`Diagnostics::Diagnostics`, `FirmwareImage::FirmwareImage`, and `BootProtocol::BootProtocol`.
Firmware-image and boot-protocol are interface-only reservations until their schemas are frozen in
Phases 6 and 7. The Phase 2 wire frame and service IDs are deliberately not guessed in Phase 1.
