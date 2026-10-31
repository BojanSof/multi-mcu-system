# Release and compatibility matrix

Versions describe independent contracts. Firmware semantic versions do not replace wire, service,
image or boot-protocol negotiation.

| Component or contract | Current version | Compatibility rule |
|---|---:|---|
| Repository/native project | 0.1.0 | Build metadata only |
| Nordic host application | 0.6.0 | Informational; MCUboot independently governs image state |
| STM node application | 0.6.0 | Informational; lifecycle reports wire/management versions separately |
| Protocol C API | 1.0 | Source API; no ABI stability is promised across repositories |
| Application wire framing | major 1 | Unknown major is rejected before state mutation |
| Node API C API | 1.0 | Source API |
| Node management | 1.0 | Major must match; offered minor must be at least required minor |
| Lifecycle service | 1.0; reliability node 1.1 | Host requires 1.0; new history/event commands require 1.1 |
| Diagnostics API/service | 1.0 | Same major/minor rule |
| Virtual-I/O example | 1.0 | Optional example service; not part of the generic protocol |
| Firmware image | MMFW v1.0 | Legacy integrity-only package format; not the signed A/B format |
| Signed STM image | MMFW v2.0 | Exact supported format; target, slot, key and link address are checked |
| STM boot protocol | 1.0 | Exact major/minor decoder match in the current implementation |
| STM flash layout | 64/88/88/16 KiB | Change-controlled; slot vectors are `0x08010200`/`0x08026200` |

## Tested dependency baseline

| Dependency | Tested pin |
|---|---|
| nRF Connect SDK | v3.4.1 LTS |
| Zephyr | NCS v3.4.1 manifest (reported v4.4.2) |
| Arm GNU Toolchain | 15.2.Rel1 |
| STM32CubeCLT / CubeProgrammer | 1.22.0 / 2.23.0 |
| STM32CubeU0 CMSIS device / HAL | v1.0.0 / v1.0.0 |
| FreeRTOS-Kernel | V11.3.0, 202604 LTS |
| west | 1.5.0 |
| micro-ecc / Mbed TLS | pinned commits in `deps/LOCK.md` |
| Python cryptography | 49.0.0 |

`deps/LOCK.md` and `deps/west-manifest-freeze.yml` are authoritative for exact revisions and
licenses.

## Supported combinations

- The 0.6.0 host accepts a node only when application wire major 1 and management major 1 match.
- Optional services are evaluated independently. An incompatible service is unavailable without
  taking other compatible services or the link offline.
- Service compatibility requires equal major and `offered_minor >= required_minor`.
- A node reboot/READY transition restarts discovery; pending host work is discarded rather than
  replayed. Session IDs and duplicate caches are not durable global identities across power loss.
- Signed STM images must match the expected STM target/hardware IDs, Slot 0 or Slot 1, corresponding
  vector/link address, maximum size, signature algorithm, key ID, and required protocol versions.
- There is no anti-rollback version policy. A cryptographically valid older image is not rejected
  solely because its semantic version is lower.

## Security and validation labels

The STM signed boot flow authenticates images, but SWD and protections remain open and development
key replacement is not protected. The Nordic default MCUboot key is public. Neither path is a
production secure-boot claim.

Use these result labels in reports: `PASS`, `FAIL`, `PASS_BUILD`, `PASS_HARDWARE_USER`,
`BLOCKED_TOOL`, `BLOCKED_ELECTRICAL`, and `NOT_RUN_HARDWARE`. A native test, simulated trace or
successful build is not hardware evidence. See [VALIDATION.md](VALIDATION.md) for current observed results and outstanding checks.
