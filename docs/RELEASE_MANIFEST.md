# Release bundle v2

Phase 11 adds a separate `tools/firmware/release_bundle.py`; the Phase 6 v1
manifest creator remains unchanged. One bundle directory contains a JSON manifest
and referenced files; private keys are never bundled.

The frozen v2 object is:

```json
{
  "format": "multi-mcu-release",
  "format_version": 2,
  "board": "nrf54l15dk+nucleo-u083rc",
  "system_version": "0.11.0",
  "api": 1,
  "wire": "1.0",
  "management": "1.0",
  "image_format": 2,
  "nordic": {"file": "host.signed.bin", "sha256": "<64 hex digits>"},
  "bootloader": {
    "file": "stm-loader.bin", "sha256": "<64 hex digits>",
    "address": 134217728, "size": 49660, "key_id": 1
  },
  "slots": [
    {"file": "slot0.mmfw", "sha256": "<64 hex digits>", "slot": 0},
    {"file": "slot1.mmfw", "sha256": "<64 hex digits>", "slot": 1}
  ]
}
```

Numbers above are illustrative; generation records the actual loader size/hash.
System version is strict semantic version text matching both signed slot manifests;
Nordic MCUboot version remains independent. The loader is a raw `.bin`, not ELF/HEX.
Its initial stack/vector must be valid for 40 KiB RAM and the 64 KiB loader region.
Exactly one slot0 and one slot1 are required; both signatures, key IDs, targets,
link addresses, payload lengths and compatibility versions are checked.

Every referenced path is relative to the manifest directory. Absolute paths, `..`,
missing files, escapes through symlinks, unknown/missing schema fields and digest
mismatches are rejected before opening serial. Nordic's MCUboot artifact is parsed
and its internal digest checked; this does not replace MCUboot's device-side signature
verification. `stm recover` does not install Nordic firmware.

The JSON SHA256 entries are integrity checks, **not** signatures authenticating the
manifest or raw loader. The operator must trust the release's provenance and ensure
the loader was built with the matching public key; key_id alone does not prove its
embedded key. ROM repair intentionally allows replacement of the trust root, so it
is an unauthenticated development rescue facility, not production secure boot.

Recovery safety range remains the canonical STM flash layout: loader pages0..31;
all application slots and metadata are preserved by ROM repair. Native regressions
check these bounds against `flash_layout.yaml`. Recovery after interrupted transfer
starts explicitly from a fresh ROM reset and erase; never blindly resumes writes.
