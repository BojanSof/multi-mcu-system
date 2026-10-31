# Publication readiness

Review date:2026-10-31. This is a local publication check; it does not create a remote,
push commits, rewrite history or claim new physical acceptance.

## Completed cleanup

- Current documentation describes setup, architecture, services, operation, firmware
  installation, recovery and testing by feature. Earlier engineering plans/reports
  are under [the development archive](archive/development/README.md).
- MIT covers project-owned code/documentation; dependency pins and upstream licenses
  remain in [the dependency lock](../deps/LOCK.md). Existing third-party notices are unchanged.
- Public setup uses configurable tool locations and an ignored local environment.
  Signing keys, SDK trees and generated artifacts are excluded from source publication.
- Build/profile names, firmware behavior, wire/image formats and dependency pins are preserved.

## Git audit

An independent standard-library pattern scan examined 15 reachable commits and 575 blobs
(1,006 reachable objects). It found no actual PEM private-key blocks, common provider
tokens, credential URLs or literal secret assignments. No key files, local environment,
SDK directories or generated firmware were tracked. No blob exceeded 10 MiB; the largest
was a 54,216-byte historical development plan. A deterministic EC scalar in the signing
unit test is a public test fixture, not a deployment credential.

This targeted scan is not proof that arbitrary secrets cannot exist. Review any new
artifacts before release. Historical records retain machine-specific paths as original
provenance; they are not prerequisites. Two optional developer-path discovery fallbacks
also remain in the doctor script; configured PATH/environment takes precedence, and
no source/tool behavior was changed as part of this documentation cleanup.

## Reproduction and remaining publication steps

The clean-checkout native/Python results and hardware limits are in [VALIDATION.md](VALIDATION.md).
The check reused the existing Python environment and optionally the pinned SDK through
an explicit CMake cache setting; it did not redownload dependencies or exercise boards.

No Git remote is configured. Choose the real repository URL when creating the GitHub
repository, replace `<repository-url>` in clone commands when using them, and review
`git diff`/the release manifest before publishing. No Git history rewrite is needed
based on the scan findings. Embedded CI requires the documented self-hosted SDK runner;
native CI is the ordinary hosted workflow. New private signing keys must stay outside
Git, and the default Nordic SDK development key is not a production trust configuration.


## Final documentation checks

The documentation-only working changes were overlaid onto the isolated checkout and
retested with explicitly reused Python/SDK inputs:26 root CTests,2 bootloader CTests
and331 Python tests passed. The current working tree also passed the same suite.
All145 local Markdown file links resolve across71 documentation files; current
user docs contain no numbered development headings/profile examples or personal
machine paths. Shell syntax for the local environment example and documented tool
help were checked. Firmware/build/protocol behavior and dependency pins did not change;
the Python lock only lost an obsolete comment.

Archived agent rules use `AGENT_RULES.md` so they cannot become active nested
`AGENTS.md` instructions. Only historical provenance remains there. Source cleanup,
hardware tests, remote creation and publication were not performed as side effects.
