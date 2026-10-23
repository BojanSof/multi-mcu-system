# Host CLI

Run from the project root after building/flashing the `phase9` Nordic profile and
installing `tools/requirements.lock` into `.venv`. Close the terminal log viewer
before opening the CLI. Phase 9 sends logs over RTT and reserves J-Link VCOM for SMP.

```sh
./scripts/mmctl device list
./scripts/mmctl --port /dev/serial/by-id/YOUR_JLINK status
./scripts/mmctl services list
./scripts/mmctl sensor sample
./scripts/mmctl actuator start --duration-ms 2000
./scripts/mmctl events watch
./scripts/mmctl node versions
./scripts/mmctl diagnostics records
./scripts/mmctl node maintenance enter --yes
./scripts/mmctl node maintenance exit --yes
```

If there is exactly one J-Link VCOM it is selected automatically. Otherwise pass
`--port`; stable `/dev/serial/by-id/` paths are preferred. Common options work
before or after subcommands. Add `--json` for one JSON object per line, `--logs`
for host log lines on stderr, or `--timeout` / `--wait-timeout` to adjust bounded
transport / operation-result waits.

Service commands print admission and then the asynchronous response. Use
`events watch` to observe actuator progress and completion. `diagnostics records`
drains up to 32 retained node records and then checks that the queue is empty.
`diagnostics clear --yes` disables injections; it does not erase history.

```sh
./scripts/mmctl diagnostics inject 3 --yes
./scripts/mmctl diagnostics clear --yes
./scripts/mmctl service call 0x1001 1 --data '' --yes
./scripts/mmctl host image list
./scripts/mmctl host image update path/to/zephyr.signed.bin --yes
```

Nordic image operations use a signed MCUboot `.bin`, not `dfu_application.zip`.
`update` checks the image SHA-256/SHA-512 TLV and confirms that its hash differs from the
primary image. It uploads, checks that the secondary slot matches, marks it for
trial, resets once, and reconnects using read-only image-list requests. It reports
the active image and confirmation flags. The Phase 9 firmware automatically
confirms after its node-discovery self-test; query `host image list` again if
confirmation has not completed yet. `host image confirm --yes` is an explicit
diagnostic bypass of the normal self-test policy.

An interrupted service call is never reconnected/replayed automatically. Retried
requests retain the same request ID and expected host boot ID; a Nordic restart
invalidates pending operations.

The independently selectable `phase10` profile adds remote STM reset and signed
custom-bootloader updates. Verify the documented supervision wiring first.

```sh
./scripts/mmctl node reset --yes
./scripts/mmctl stm status
./scripts/mmctl stm image inspect path/to/slot1.mmfw --public-key /external/public.pem
./scripts/mmctl stm update path/to/slot1.mmfw --public-key /external/public.pem --yes
./scripts/mmctl stm abort --yes
```

Image inspection is offline. Updates require an external trusted P-256 public key
and validate the complete signed MMFW image before opening the serial port. The
default trusted key ID is 1; use `--key-id` to match a deliberately configured
bootloader key ID. Choose the image linked opposite the previously confirmed slot;
the host checks the STM journal before erasing. Initial idle `stm status` has
unknown slots; its confirmed slot is populated after the first update query.

The CLI streams bounded chunks and waits for STM acknowledgement before advancing.
It reports completion only after confirmation and rediscovery; rollback is a
failure. After a timeout or USB disconnect, use `stm status` to inspect the job;
the CLI never reconnects and replays an update. Ctrl-C attempts abort before the
commit starts. Once `finish` has been sent, verification proceeds on the host.
Factory-ROM recovery uses the independently selectable `phase11` firmware and the
same supervision wires. It is an **unauthenticated rescue path**, not a secure
update mechanism. Before authorizing erase, manually verify `NBOOT_SEL=0`,
`nBOOT1=1`, `BOOT_LOCK=0`, compatible 3.3 V levels, BOOT0/reset wiring and usable SWD
recovery. The CLI never changes option bytes or protections.

Prepare a release directory containing four artifacts: Nordic MCUboot signed bin,
raw STM bootloader bin (not ELF/HEX), and two signed MMFW slot packages. Both slot
package versions must match `--system-version`; Nordic firmware version is independent.
Reuse the signing key matching the bootloader. Paths in the manifest are relative
to its directory; staging all artifacts below that directory is required.

```sh
.venv/bin/python tools/firmware/release_bundle.py create build/phase11-release/release.json \
  --nordic build/phase11-release/nordic.signed.bin \
  --bootloader build/phase11-release/stm32-bootloader.bin \
  --slot0 build/phase11-release/slot0.mmfw --slot1 build/phase11-release/slot1.mmfw \
  --system-version 0.11.0 --public-key keys/stm32-dev-public.pem
.venv/bin/python tools/firmware/release_bundle.py verify build/phase11-release/release.json \
  --public-key keys/stm32-dev-public.pem
scripts/mmctl stm recover build/phase11-release/release.json \
  --public-key keys/stm32-dev-public.pem --yes --rom-prerequisites-verified
scripts/mmctl stm recover-status
scripts/mmctl stm recover-abort --yes
```

Every artifact is validated offline before serial is opened: schema/path/bounds,
SHA-256, raw loader vectors, slot signatures/key IDs/versions and Nordic MCUboot
hash/signature-TLV structure. A manifest hash is integrity, not authenticity: obtain
the raw loader from a trusted matching-key build. Nordic signature authenticity is
ultimately enforced by its MCUboot; ROM repair does not flash the Nordic artifact.

Recovery enters ROM with 8E1, verifies device ID/capabilities, erases only the 64 KiB
bootloader region and streams chunks with readback. Completion means the loader
bytes/hash were verified; application recovery is reported separately. Healthy
applications are preserved. If runtime stays offline, CLI chains the signed Slot 0
update. Runtime success requires a nonzero session different from the pre-repair
session; an old cached online snapshot is not accepted. A persistently stale online
snapshot stops without signed mutations. Only a verified protected-Slot-0 failure
permits a fresh Slot 1 job. The CLI never
clears metadata or bypasses slot protection. Other errors stop without replay.

Interrupted ROM writes may leave a partial loader. `recover-abort` restores BOOT0
low, 8N1 and reset, but cannot undo an erased/programmed loader. Retry a full explicit
recovery or use SWD; do not assume a working application can boot through a partial
loader. Neither cable loss nor host restart triggers an automatic mutating reconnect.
