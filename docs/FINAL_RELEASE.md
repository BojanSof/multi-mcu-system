# Reproducible builds and release packaging

Start with [setup](SETUP.md), then [firmware installation](FIRMWARE_UPDATES.md).
Pinned SDK/toolchain/dependency versions are recorded in [the lock](../deps/LOCK.md).
Use [testing](TESTING.md) and [validation status](VALIDATION.md) to distinguish
implemented software from measured hardware behavior.

## Build and verify

All commands run from the project root. Keep SDKs, keys and build artifacts ignored.
Supply the public-key header matching the installed STM bootloader, never its
private key, to the build:

```sh
source ./.env.local
scripts/test/run_native.sh
scripts/build/build_nordic.sh --profile reliability
scripts/build/build_stm32.sh --profile reliability
scripts/build/build_stm32.sh --profile bootloader --public-key-header "$STM_PUBLIC_HEADER"
```

For a fresh checkout, initialize pinned submodules and SDKs as described in setup.
Native portable tests do not need the Nordic SDK; optional real-CBOR integration
checks require its pinned sources. Do not assume an absent SDK ran those tests.
The historical build interfaces remain available, but the complete operator system
uses `reliability`. Build scripts do not flash hardware.

## Stage a bundle

Create signed slot packages using the [image commands](FIRMWARE_UPDATES.md#keys-and-stm32-packages).
The private key must match the loader's public key. The sample below assumes
`build/images/slot0.mmfw` and `slot1.mmfw`, both signed with package version 0.6.0:

```sh
mkdir -p build/release
cp build/nrf54l15-host-reliability/nrf54l15-host/zephyr/zephyr.signed.bin build/release/nordic.signed.bin
cp firmware/stm32u083/bootloader/build/release/multi_mcu_stm32_bootloader.bin build/release/stm32-bootloader.bin
cp build/images/slot0.mmfw build/release/slot0.mmfw
cp build/images/slot1.mmfw build/release/slot1.mmfw
.venv/bin/python tools/firmware/release_bundle.py create build/release/release.json \
  --nordic build/release/nordic.signed.bin \
  --bootloader build/release/stm32-bootloader.bin \
  --slot0 build/release/slot0.mmfw --slot1 build/release/slot1.mmfw \
  --system-version 0.6.0 --public-key "$STM_PUBLIC_KEY"
.venv/bin/python tools/firmware/release_bundle.py verify \
  build/release/release.json --public-key "$STM_PUBLIC_KEY"
.venv/bin/python tools/firmware/release_archive.py create \
  build/release/release.json build/multi-mcu-release.zip --public-key "$STM_PUBLIC_KEY"
.venv/bin/python tools/firmware/release_archive.py verify \
  build/multi-mcu-release.zip --public-key "$STM_PUBLIC_KEY"
```

Change both package versions and `--system-version` together for another release.
Nordic and runtime application versions are independent; package metadata alone does
not prove which application ran. The raw loader must be BIN, not ELF/HEX. Manifest
paths are relative to its directory; all four artifacts must reside below it.

Identical input bytes produce identical archives: entries are sorted, uncompressed,
and use fixed metadata. That is archive determinism, not a blanket claim that SDK
firmware builds are bit-reproducible across paths/toolchains. Each archive contains
exactly the manifest plus four artifacts. Resource/test reports are separate evidence.

The manifest verifies artifact integrity but is not a signature authority for the
raw loader. Obtain release bundles from a trusted source. Nordic signature authenticity
is enforced by MCUboot; STM applications are independently P-256 verified. ROM repair
does not install Nordic firmware and is not an atomic multi-MCU upgrade.

## Publication checks

Do not include signing keys, local environments, SDK trees or private captures in
source/release archives. Include test provenance and unresolved hardware gates in
release notes. Never mark skipped destructive tests PASS, use a fixture as physical
evidence, or claim production secure boot with the current open SWD/ROM configuration.
