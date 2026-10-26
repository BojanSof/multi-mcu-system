# Reproduce and package the final reference system

Software coverage is not completed physical acceptance. See PHASE_REPORTS/PHASE_12.md
for exact evidence and remaining HIL; no production secure-boot claim is made.

## Clean checkout

```sh
git clone --recurse-submodules YOUR_REPOSITORY_URL multi-mcu-system
cd multi-mcu-system
scripts/bootstrap-venv.sh
.venv/bin/python scripts/doctor.py
scripts/test/run_native.sh
```

No SDK is required for portable/native tests; real-CBOR integration tests are
enabled only when the pinned local NCS workspace exists. Bootloader native crypto
tests require the pinned submodules. No global pip, private key or captured logs
belong in the checkout. The placeholder repository URL must be your actual remote.

For cross builds follow README's ignored `.env.local` and `.ncs-workspace` setup;
install the audited NCS3.4.1/toolchains and matching SDK Python requirements. Keep
CMSIS/HAL/FreeRTOS at deps/LOCK.md pins. Supply the existing external public-key
header; never generate replacement keys just to complete a build.

```sh
source ./.env.local
scripts/build/build_stm32.sh --public-key-header /external/stm32-public.h
scripts/build/build_nordic.sh --profile phase12
```

`phase12-verbose` selects identical metrics/features but retains informational
logs for the measured flash comparison. `phase12` retains warnings/errors and all
typed SMP events; phase9/10/11/default remain independently selectable. Builds never
flash. Never overwrite the custom STM loader with the standalone application.

## Create the release

Use the Phase11 bundle creation commands with a fresh directory, e.g.
`build/release-0.12.0/`, changing:

- Nordic source to `build/nrf54l15-host-phase12/nrf54l15-host/zephyr/zephyr.signed.bin`.
- Both signed slot package versions and `--system-version` to0.12.0.
- Build ID to a chosen deterministic release identifier (e.g.0xc00).

Full staging/signing commands are in PHASE_11_TESTING.md; raw STM loader, both
slot-linked binaries and the original matching private/public keys are required.
Do not replace the loader's trust root implicitly. Independent runtime firmware
version can remain0.6.0; package version describes the signed candidate and does
not prove the running app was upgraded.

```sh
.venv/bin/python tools/firmware/release_bundle.py verify \
  build/release-0.12.0/release.json --public-key /external/stm32-public.pem
.venv/bin/python tools/firmware/release_archive.py create \
  build/release-0.12.0/release.json build/multi-mcu-0.12.0.zip \
  --public-key /external/stm32-public.pem
.venv/bin/python tools/firmware/release_archive.py verify \
  build/multi-mcu-0.12.0.zip --public-key /external/stm32-public.pem
```

Identical bundle input bytes produce identical ZIP bytes (fixed metadata, sorted
uncompressed entries). Rebuilding firmware itself can depend on Git/build IDs,
compiler and SDK paths; archive determinism is not a blanket bit-reproducible
firmware build claim. Each archive has exactly release.json plus four artifacts;
resources/test reports remain separate evidence, never private keys or SDK trees.

The manifest is integrity metadata, not a signed authority for the raw loader.
Trusted release provenance remains required; MCUboot/STM signed-image verification
and open SWD/ROM recovery security limitations remain unchanged.

## Final acceptance

Run PHASE_12_TESTING.md's strict100/600 acceptance and explicit update/recovery
matrix on actual boards. Keep JSON evidence and SDK/Git/tool versions with the
release outside source control when captures may contain sensitive data. Do not
mark skipped destructive checks PASS, and do not treat synthetic fixtures as HIL.


## Reliability extension status

The approved Milestones13–16 continuation is in [RELIABILITY_PLAN.md](RELIABILITY_PLAN.md).
Its independently selectable reliability builds and acceptance runner are documented
in [RELIABILITY_TESTING.md](RELIABILITY_TESTING.md). Preserve baseline release bundles;
new watchdog, journal, acknowledged-event and failure scenarios require their own
hardware acceptance. Native/build PASS does not promote that acceptance automatically.
