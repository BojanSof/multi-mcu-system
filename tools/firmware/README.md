# Firmware image and release tools

Run through the project `.venv` from the repository root. Current STM application
updates use signed MMFW v2 packages (`sign.py`, `verify_signed.py`,
`public_key_header.py`). `generate_key.py` is only for a new development trust root;
reuse existing keys when updating an installed bootloader.

See [installation and signing](../../docs/FIRMWARE_UPDATES.md),
[bootloader format](../../docs/BOOTLOADER.md) and
[release packaging](../../docs/FINAL_RELEASE.md) for complete commands.

`release_bundle.py` validates the manifest and four staged artifacts before device
access. `release_archive.py` creates/verifies deterministic ZIPs.
`resource_report.py` reports static ELF/stack-frame data, not measured runtime bounds.

Legacy `pack.py`, `inspect_image.py`, `verify.py` and `release_manifest.py` support
an unsigned integrity-only container. Its hash is not proof of authenticity and it
must not be substituted for the signed A/B format. Those tools do not replace
MCUboot or custom-loader signature verification. No private key belongs in a bundle.
