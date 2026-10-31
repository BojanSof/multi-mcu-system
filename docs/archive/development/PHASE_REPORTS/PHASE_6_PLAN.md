# Phase 6 plan and frozen interfaces

## Scope

Phase 6 establishes standard, recoverable firmware management: Nordic MCUboot with MCUmgr over the
DK USB serial path, a portable firmware-image manifest plus deterministic host tooling, and a
documented STM32 factory-ROM/SWD recovery route. It does not install the Phase 7 STM32 custom
bootloader, claim atomic two-MCU updates, or automate BOOT0/NRST before electrical approval.

## Frozen firmware-image v1 contract

All integers are unsigned little-endian. The fixed 80-byte manifest precedes the payload:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | magic bytes `MMFW` |
| 4 | 1 | format major (`1`) |
| 5 | 1 | format minor (`0`) |
| 6 | 2 | header size (`80`) |
| 8 | 4 | target ID |
| 12 | 4 | hardware ID |
| 16 | 2 | firmware major |
| 18 | 2 | firmware minor |
| 20 | 2 | firmware patch |
| 22 | 2 | flags (must be zero in Phase 6) |
| 24 | 4 | non-secret build ID |
| 28 | 4 | required load address |
| 32 | 4 | payload size |
| 36 | 1 | required wire major |
| 37 | 1 | required wire minor |
| 38 | 2 | required management major |
| 40 | 2 | required management minor |
| 42 | 6 | reserved, must be zero |
| 48 | 32 | SHA-256 of payload |

Target `1` is the Nordic host application and target `2` is the STM32U083 application. Hardware ID
`0x54150001` identifies nRF54L15-DK/cpuapp and `0x00830001` identifies NUCLEO-U083RC. The Phase 6
STM ROM application load address remains `0x08000000`; the provisional Phase 7 slot addresses are
not accepted by this profile. Payload size is bounded by the selected target region and the package
must contain exactly `header_size + payload_size` bytes.

Compatibility requires exact target, hardware ID, load address, wire major and management major;
an image requiring a newer wire or management minor than the running peer is rejected. Hash
comparison is constant-time at the portable API boundary. This format provides integrity, not
authenticity: flags are zero and no signature is claimed in Phase 6. Phase 7 will add a separately
versioned signed envelope/trailer without changing these fields silently.

Nordic self-update continues to use the NCS/MCUboot image format and generated signed artifact;
the `MMFW` package is the generic release/ST staging contract, not a replacement for MCUboot.

## Nordic update contract

- Use NCS 3.4.1 sysbuild with MCUboot and DTS fixed partitions validated from build output.
- Required management transport is MCUmgr SMP over the DK USB serial/VCOM interface. BLE is
  optional and must not be required for acceptance.
- Upload enters the secondary slot, test boot is explicit, and the application confirms itself only
  after normal initialization. Revert/confirmation claims require board evidence.
- Existing non-MCUboot build remains independently selectable; Phase 6 adds a dedicated update
  profile and does not overwrite the prior bring-up profile.

## STM recovery contract

- First supported recovery remains ST-LINK/SWD using STM32CubeProgrammer, with read/verify steps.
- Factory ROM entry and USART behavior must be verified against STM32U083/NUCLEO-U083RC source
  documentation and then on hardware before claiming PASS.
- No option bytes are changed automatically. No nRF-controlled BOOT0/NRST is implemented while the
  pins, open-drain reset behavior and board routing remain unapproved.
- An interrupted ROM programming attempt is recovered by repeating the documented manual ROM or
  SWD procedure; Phase 6 does not claim STM atomicity or A/B rollback.

## Parallel ownership

1. Integrator: this contract, public `firmware-image` header, release manifest and final builds.
2. Firmware-image/tools agent: library implementation/tests and `tools/firmware/`, without changing
   the frozen public layout.
3. Nordic agent: `firmware/nrf54l15-host/` MCUboot/MCUmgr profile and build evidence.
4. STM recovery agent: ROM/SWD documentation and read-only board/source research; no option bytes.
5. Validation agent: malformed-image, compatibility, build-artifact and HIL review.

## Exit verification

- Native malformed/truncated/overflow/wrong-target/wrong-address/hash tests and deterministic Python
  pack/inspect tests.
- Nordic normal profile and pristine MCUboot/MCUmgr profile builds, partition/size report and signed
  update artifact inspection.
- STM normal application builds unchanged; recovery commands documented.
- Hardware update/revert/recovery tests clearly marked `NOT_RUN_HARDWARE` until actually performed.
