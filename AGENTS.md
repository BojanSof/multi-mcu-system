# Repository contribution rules

Read [DEVELOPMENT_PLAN.md](DEVELOPMENT_PLAN.md) first, then the relevant public
architecture, protocol and testing guides. Multi-MCU Lab is a generic reference
system using nRF54L15-DK and NUCLEO-U083RC. The virtual-I/O example is removable;
product-specific behavior must not enter reusable libraries.

## Work and verification

- Begin development work with `scripts/doctor.py`. Report missing required tools,
  SDKs, permissions and hardware facts with exact verification/install actions.
  Do not assume root, network access or connected boards. Python packages belong
  in the project `.venv`, never a global pip environment.
- Establish scope and shared contracts before changes. Use parallel agents where
  available for independent libraries, Nordic, STM32, tools and validation, with
  explicit exclusive ownership. The integrator owns shared headers, schemas,
  root CMake/manifest, flash layout, pinout, recovery policy and merge review.
- Preserve working profiles and public contracts. Experimental firmware variants
  need independently selectable builds. Never replace a known recovery route merely
  to make a new demonstration work.
- Run relevant native/Python tests and firmware builds. Record commands, revisions,
  tool versions, memory/stack evidence and outstanding issues in current validation
  documentation. Use PASS/FAIL/NOT_RUN_HARDWARE/BLOCKED_TOOL/BLOCKED_ELECTRICAL honestly.
  Simulation and native flash models are not proof of board power-loss behavior.
- Native-only tests must remain usable without SDKs; SDK-backed integration tests
  may be explicitly unavailable. Keep clean-checkout reproduction instructions current.

## Language and package boundaries

- Project firmware and portable libraries are C, with C11 for new portable code.
  Python is for CLI, tests, signing/packaging and development tools. CMake, Kconfig,
  DTS and YAML handle integration. No C++, project heap allocation in MCU control
  paths, exceptions or SDK dependencies in portable APIs.
- Functions use `Module_Function`, types `Module_Type`, constants `MODULE_NAME_VALUE`.
  Prefer bounded buffers, explicit lengths/ownership, clear return values and small
  functions. Use clang-format. ISRs do minimal work; processing belongs to owners.
- Portable packages live in `libraries/`, each with public `include/<package>/`,
  source inventory, reusable CMake target and native tests. Zephyr modules compile
  the same source inventory; set EXTRA_ZEPHYR_MODULES before find_package(Zephyr).
- Root CMake builds native libraries/tests only. Nordic Zephyr application,
  STM32 HAL/FreeRTOS application and bare-metal STM32 bootloader build independently.
  Board adapters live below the STM32 platform boundary; bootloader policy must
  not depend on FreeRTOS or application services.
- Pin dependencies/submodules and keep licenses in `deps/LOCK.md`. Do not vendor
  downloaded SDK trees or commit private keys, generated images or sensitive captures.
  Public docs use current feature names; older engineering records are archival.

## Hardware constraints

- Only the two documented boards, onboard I/O, USB and jumper wires are required.
  Sensors, actuators and power behavior are simulated. A logic analyzer is optional;
  DEBUG is an observable firmware event, not a voltage/power sensor.
- Before physical wiring, verify exact board revisions, pins/solder bridges and
  compatible I/O levels against `hardware/PINOUT.md`. Nordic defaults can be1.8V;
  the documented wiring requires measured3.3V compatibility. Never connect power
  rails. Preserve the verified Nordic-first power order and unpowered-board limits.
- NRST must sink low or release high-Z; BOOT0 defaults low. Preserve independent
  logging UARTs and SWD. Port2 IRQ setup is unsupported by the pinned driver;
  optional interrupt failures use UART wake/poll fallback, never abort host startup.
- Stop unsafe physical steps when facts are unverified, while continuing unaffected
  native/build work. Do not fabricate pin mappings or measured safety protections.
  Critical option-byte/protection changes need verified human authorization;
  never enable irreversible RDP Level2 or erase without a validated rescue path.

## Runtime contracts

- Preserve COBS framing, zero delimiter, explicit LE fields, CRC32, fixed capacities,
  sequence/operation/session identities, bounded timeouts/retries, version/service
  negotiation and duplicate suppression. Never cast C structs onto wire buffers.
- Host main owns runtime/update/recovery state; SMP uses mailboxes and snapshots.
  Serialize UART ownership, GPIO reset and flash operations. No concurrent mutation
  from control handlers or automatic replay after uncertain reconnect/update outcomes.
- Reliable events require negotiation. Keep legacy behavior compatible, IRQ held
  while queued, bounded retry/exhaustion counters, producer backpressure and cursor
  preservation across same-session rediscovery. ACK is not proof of PC consumption.
- Both watchdogs depend on meaningful local progress. Node offline must not starve
  the host watchdog. Automatic reset attempts are bounded, suppressed in intentional
  modes/update/repair, and never automatically enter ROM or rewrite firmware.
- Keep integer virtual services, fault injections, reset causes and diagnostics
  separate from protocol policy. Release builds exclude development injection.
  Fault journal is bounded and wear-managed; volatile counters are not persistent history.

## Firmware update gates

1. Prove ROM/SWD rescue and exact boot selection before destructive firmware tests.
2. Preserve one authoritative YAML flash layout and generated link constants.
   STM32 has256KiB flash,40KiB RAM and2KiB pages; retain measured budget gates.
3. Canonical signed bytes, slot/link address, key ID, target and compatibility are
   explicit. Use vetted hash/signature libraries and keep private keys out of Git.
4. Build each Cortex-M0+ slot for its own address and validate vector handoff on
   hardware; do not assume position independence.
5. Normal updates write only an inactive permitted slot; protect the confirmed image,
   bootloader and boot metadata. Test interrupted erase/program/metadata boundaries
   natively before authorized hardware fault injection.
6. Confirm only after application self-test; failed trials select a valid confirmed
   image under documented policy. Never infer anti-rollback or production-secure boot
   from signing while debug/recovery paths and development keys remain open.
