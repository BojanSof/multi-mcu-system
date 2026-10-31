# Phase 8 plan — regression, CI, documentation, extraction readiness

Date: 2026-10-18
Base Git revision: `4cbdbc9`

## Frozen interfaces and ownership

- CI invokes repository-owned scripts from the project root; workflows contain orchestration only.
- `scripts/test/run_native.sh` is the canonical native/Python regression entry point.
- `scripts/build/build_stm32.sh` builds the normal STM application, both A/B slot variants, and the
  bootloader without flashing hardware.
- `scripts/build/build_nordic.sh` builds the documented default Nordic profile; optional profiles
  remain explicit arguments and are never silently selected.
- HIL tooling under `tests/hil/` is opt-in, requires explicit serial-device arguments, never changes
  option bytes, and never flashes or resets hardware unless its command name states that action.
- HIL result records use `PASS`, `FAIL`, `BLOCKED_TOOL`, `BLOCKED_ELECTRICAL`, or
  `NOT_RUN_HARDWARE`; simulated results cannot be labeled as hardware captures.
- Release/version metadata is descriptive and derived from existing wire, service, image, host,
  node, and bootloader versions. Phase 8 does not change published protocol/image headers,
  `flash_layout.yaml`, pin assignments, signing bytes, or persistent boot state.
- No private keys, generated firmware, downloaded SDKs, serial captures, or build trees are tracked.

## Parallel work

1. CI agent: root-invocable build/test scripts and GitHub Actions workflow.
2. HIL/tooling agent: optional two-board runner, matrix, and deterministic parser tests.
3. Documentation agent: user demo, release/version matrix, extraction and API documentation audit.
4. Integrator: frozen contracts, build integration, full validation, size report, Phase 8 report.

## Completion checks

- Clean-style native configure/build/CTest and Python tests.
- STM normal application, Slot 0, Slot 1, and bootloader cross-builds.
- Nordic default build and feasible update/demo profiles.
- Deterministic simulated scenarios and HIL-tool unit tests.
- Workflow syntax/configuration review, formatting, whitespace, dependency/submodule audit.
- Actual board workflow is either user-observed or explicitly `NOT_RUN_HARDWARE` with exact steps.
