# Phase 1 implementation plan and interface freeze

1. Audit host tools, SDK environments, USB/debug access, and board presence.
2. Freeze package names, target names, native-only root build, independent firmware build roots, and
   the ROM-only STM bring-up profile.
3. Implement portable C11 boundary libraries and native smoke tests.
4. Implement independent Nordic and STM LED/button/log bring-up applications.
5. Add pinned manifests, local Python bootstrap, dependency lock, and electrical safety gate.
6. Run all feasible native/static/configuration tests and report blocked cross/HIL checks exactly.

No UART wire schema, physical pin map, custom bootloader behavior, or option-byte operation is in
scope for this phase.
