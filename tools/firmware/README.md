# Phase 6 firmware image tools

These standard-library-only tools build and inspect the unsigned `MMFW` container. Phase 6 provides
SHA-256 integrity, not signature-based authenticity. Never treat an `MMFW` package as trusted solely
because its hash is internally consistent.

```sh
.venv/bin/python tools/firmware/pack.py app.bin app.mmfw \
  --target-id 2 --hardware-id 0x00830001 --version 1.0.0 --build-id 1 \
  --load-address 0x08000000 --wire 1.0 --management 1.0
.venv/bin/python tools/firmware/inspect_image.py app.mmfw
.venv/bin/python tools/firmware/verify.py app.mmfw
.venv/bin/python tools/firmware/release_manifest.py release.json app.mmfw
```

The packer is deterministic: identical payloads and arguments produce byte-identical output. The
release manifest contains no wall-clock timestamp and sorts image records by stable identifiers.
The Phase 6 tools reject unknown target IDs and mismatched target/hardware/load-address combinations,
and enforce the frozen 696 KiB Nordic or 256 KiB STM32 payload ceiling. They do not replace MCUboot
signature verification.
