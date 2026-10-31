# STM32U083 custom bootloader

Independent bare-metal C project implementing signed A/B validation, transactional
boot metadata, trial/confirmation/rollback and bounded UART programming.
Portable journal/engine tests run without STM32 HAL or FreeRTOS:

```sh
cmake -S firmware/stm32u083/bootloader -B build/stm32-bootloader-native -G Ninja \
  -DBOOTLOADER_BUILD_NATIVE_TESTS=ON
cmake --build build/stm32-bootloader-native
ctest --test-dir build/stm32-bootloader-native --output-on-failure
```

Run from the repository root. Target build requires the STM toolchain, pinned
submodules and an external generated public-key header, never a private key:

```sh
source ./.env.local
scripts/build/build_stm32.sh --profile bootloader --public-key-header "$STM_PUBLIC_HEADER"
```

The header defines `BOOTLOADER_PUBLIC_KEY_ID` and 64 raw public-key `X || Y` bytes.
SHA-256 uses Mbed TLS and P-256 verification uses micro-ecc, currently in software.
The trusted public key is compiled into the loader; replacing it requires rebuilding
and explicitly provisioning the loader, not just signing with a different key.

See [bootloader design](../../../docs/BOOTLOADER.md) for layout and backup-register
confirmation, [first installation/update](../../../docs/FIRMWARE_UPDATES.md) for
key generation and safe provisioning, and [failure tests](../../../docs/TESTING.md)
for guarded interruption/rollback. Factory ROM and SWD remain rescue paths; open
protection and development provisioning do not establish production secure boot.
