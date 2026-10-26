# Multi-MCU Lab — Development Plan

**Version:** 4.0 (2026-10-25)
**Status:** Phases 1–12 delivered; Milestones 13–16 software implemented and native/build verified; hardware acceptance tracked separately
**Hardware:** Nordic **nRF54L15-DK** (host), ST **NUCLEO-U083RC** (node), USB leads and jumper/protoboard wires **only**  
**Firmware language:** C (C11 for project-owned libraries); Python permitted for developer tools and tests  
**Companion instructions:** [`AGENTS.md`](AGENTS.md)

> **Start here, Codex.** Read this document and `AGENTS.md` before modifying code. Execute the twelve phases in order, parallelizing independently owned packages when practical. At the beginning, run a dependency/environment audit and tell the user precisely which missing **non-Python** tools, SDKs, OS packages or manual board operations are required. You may create a **project-local Python `.venv`** for CLI, test, image-packaging and signing tools. Do not claim a board, firmware update, flash operation, or logic-analyzer capture was performed without actual evidence.

## 1. Goals, hardware limits and architectural decisions

Develop a **generic, reusable dual-MCU reference system**, not an electronics test fixture or specific end product. The Nordic host discovers/manages a subordinate STM32 node and exposes its capabilities to the user. The STM32 executes local services and handles its own state, faults and boot process.

**Core demonstrations:**

1. Interface decisions: UART data link, IRQ/READY sidebands, remote reset and optional ROM BOOT control, independent SWD/debug consoles and an exposed timing/debug GPIO.
2. Portable communication protocol: framing, CRC, request/response correlation, events, bounded buffers, timeouts, retries and duplicate-command defense.
3. Modular service architecture: node identity, version/capability discovery, generic commands and events. A detachable **virtual-I/O example** demonstrates sensor samples, actuator progress and simple LED/button behavior without tying the libraries to any application.
4. Logging: each MCU's own debug output, structured cross-MCU fault/event records, host-side collection, timestamps and reset reasons.
5. Failure injection and **logic-analyzer tutorials**: UART RX/TX plus one DEBUG/IRQ GPIO; reproduce bugs, explain waveform evidence, implement fixes and regressions.
6. Recovery: local watchdog, remote reset, boot readiness, link-supervision policies and repeated-crash escalation.
7. Firmware lifecycle: independent firmware versions, compatibility contracts, nRF MCUboot update, STM32 ROM-bootloader recovery, and eventually **custom STM32 signed A/B bootloader** with explicit active-slot and rollback semantics.
8. Independently buildable CMake libraries and firmware projects suitable for future extraction into separate repositories.

**Hard hardware constraint:** No external sensors, motors, relays, DUT, voltage/current monitors, level shifters, extra MCUs, isolation circuitry or custom PCB. Use the two boards, their built-in LEDs/buttons, jumper wires and built-in debuggers. Logic analyzer is **optional**; its probes can attach to exposed header signals but no automated test may require one. Simulated electrical/actuator behaviors **never** imply physical power-control or safety protection.

**Never compromise board safety to satisfy the no-extra-hardware constraint.** The nRF54L15-DK defaults to 1.8 V I/O; the STM32 Nucleo typically exposes 3.3 V I/O. Before any GPIO interconnection, verify the exact boards/revisions, configure Nordic's VDD as needed using Board Configurator, and physically verify the correct signal levels. If that cannot be confirmed, proceed with native/emulated tests and **stop before connecting the boards**. USB-power each board independently, connect a common ground, and never join USB 5 V or independent 3.3 V supply rails.

**Out of scope:** arbitrary multiple-node physical bus in v1, a generic scripting VM, GUI, real mechanical safety, certified secure boot, enabling irreversible readout protection, custom external circuits. Future multi-node expansion is a protocol/interface design goal, not an implementation requirement.

## 2. Architectural boundaries

```text
PC CLI / Python scripts / optional BLE
                  |
           nRF54L15-DK
     Host / BLE / Node Manager
     UART + supervisory GPIOs
                  |
           STM32U083RC
        Node runtime (FreeRTOS)
     Registered generic services
       examples/virtual-io
                  |
           on-board I/O
        + simulated peripherals

STM32 boot process (later):
  ROM recovery OR independent custom bootloader
      -> validate signed Slot 0 / Slot 1
      -> select active/trial/rollback
      -> run STM32 application
```

### Responsibilities

| Component | Owns | Must NOT own |
|---|---|---|
| `libraries/protocol` | UART-independent frame serialization, COBS, CRC32, sequence/request handling primitives | BLE, HAL, logging backends, nodes, sensors |
| `libraries/node-api` | Portable node capabilities, typed service IDs, command/event schemas and dispatch primitives | nRF/STM drivers or example-specific operations |
| `libraries/diagnostics` | Structured fault records, counters, bounded record queue, encoding helpers | Zephyr logging backend, STM32 UART registers |
| `libraries/firmware-image` | Versioned image/manifest format, bounds/endian parsing, signed-field definition | Cryptographic keys or MCU flash writes |
| `libraries/boot-protocol` | Transport-independent boot update/recovery message schema | STM ROM USART protocol, flash HAL |
| `firmware/nrf54l15-host` | BLE/CLI, node discovery, supervision, update orchestration, boot pin GPIO, staged STM image | Node application internals |
| `firmware/stm32u083/application` | Application startup/RTOS, transport adapter, node services, watchdog and fault policy | Bootloader signing or flash selection algorithm |
| `firmware/stm32u083/bootloader` | Minimal bare-metal boot decisions, signature verification, metadata, slot selection, flash programming and handoff | FreeRTOS, BLE, application service logic |
| `firmware/stm32u083/platform` | Target/board HAL adapters and narrow hardware facilities shared by boot and application **when safe** | Protocol policy or application state machines |
| `examples/virtual-io` | Deterministic virtual sensors, virtual actuator and demo scenario model | Generic protocol and node discovery mechanisms |

All `libraries/*` projects must compile natively with CMake, build via `add_subdirectory()` in the STM32 projects, and expose Zephyr module integration when consumed by the Nordic host. No cross-library source copying, no giant `common/` dumping ground and no board-specific headers in public library APIs.

## 3. Repository structure (target, incremental)

The root CMake build configures **native libraries/tests only**. Zephyr and STM32 applications are **independent builds with their own toolchains**. Subdirectories can later become separate repositories because they expose proper public headers and stable CMake targets.

```text
multi-mcu-lab/
├── README.md
├── DEVELOPMENT_PLAN.md
├── AGENTS.md
├── CMakeLists.txt                 # Native-only tests and libs
├── CMakePresets.json
├── west.yml                       # Valid pinned nRF/west manifest
├── .gitignore
├── .clang-format
│
├── libraries/
│   ├── protocol/
│   │   ├── CMakeLists.txt
│   │   ├── cmake/Sources.cmake
│   │   ├── include/protocol/{Protocol,ProtocolFrame,ProtocolTransport,ProtocolTypes}.h
│   │   ├── src/{Protocol,ProtocolFrame,ProtocolCobs,ProtocolCrc}.c
│   │   ├── tests/
│   │   └── zephyr/{module.yml,CMakeLists.txt,Kconfig}
│   ├── node-api/
│   │   ├── CMakeLists.txt
│   │   ├── cmake/Sources.cmake
│   │   ├── include/node_api/{NodeApi,NodeCommands,NodeCapabilities,NodeServices}.h
│   │   ├── src/
│   │   ├── tests/
│   │   └── zephyr/{module.yml,CMakeLists.txt,Kconfig}
│   ├── diagnostics/
│   │   ├── CMakeLists.txt
│   │   ├── cmake/Sources.cmake
│   │   ├── include/diagnostics/{Diagnostics,DiagnosticsFault,DiagnosticsBuffer}.h
│   │   ├── src/
│   │   ├── tests/
│   │   └── zephyr/{module.yml,CMakeLists.txt,Kconfig}
│   ├── firmware-image/            # Reserve in Phase 1; implement in Phase 6/7
│   │   ├── CMakeLists.txt
│   │   ├── include/firmware_image/
│   │   ├── src/
│   │   ├── tests/
│   │   └── zephyr/{module.yml,CMakeLists.txt,Kconfig}
│   └── boot-protocol/             # Reserve in Phase 1; implement in Phase 7
│       ├── CMakeLists.txt
│       ├── include/boot_protocol/
│       ├── src/
│       ├── tests/
│       └── zephyr/{module.yml,CMakeLists.txt,Kconfig}
│
├── firmware/
│   ├── nrf54l15-host/
│   │   ├── CMakeLists.txt
│   │   ├── prj.conf
│   │   ├── sysbuild.conf           # MCUboot later
│   │   ├── boards/                 # pinctrl/overlay/config if necessary
│   │   ├── include/
│   │   └── src/
│   │       ├── main.c
│   │       ├── app/
│   │       ├── communication/
│   │       ├── node_manager/
│   │       ├── firmware_update/
│   │       ├── platform/
│   │       └── fault_injection/
│   │
│   └── stm32u083/
│       ├── cmake/{Dependencies,Stm32Targets}.cmake
│       ├── platform/{CMakeLists.txt,include/,src/,config/}
│       ├── memory/
│       │   ├── flash_layout.yaml   # Authoritative layout, change-controlled
│       │   ├── linker/{bootloader,slot0,slot1}.ld.in
│       │   └── generated/          # generated OUTSIDE source or ignored
│       ├── application/
│       │   ├── CMakeLists.txt
│       │   ├── CMakePresets.json
│       │   ├── config/{FreeRTOSConfig.h,stm32u0xx_hal_conf.h}
│       │   ├── include/
│       │   └── src/
│       │       ├── main.c
│       │       ├── app/
│       │       ├── communication/
│       │       ├── services/
│       │       ├── platform/
│       │       └── fault_injection/
│       └── bootloader/             # Separate bare-metal CMake project
│           ├── CMakeLists.txt
│           ├── CMakePresets.json
│           ├── config/
│           ├── include/
│           └── src/
│               ├── main.c
│               ├── Bootloader.c
│               ├── BootControl.c
│               ├── BootVerify.c
│               ├── BootCrypto.c
│               ├── BootUpdate.c
│               └── BootJump.c
│
├── examples/
│   └── virtual-io/
│       ├── CMakeLists.txt
│       ├── include/virtual_io/
│       ├── src/{VirtualSensor,VirtualActuator,VirtualIo}.c
│       └── tests/
│
├── tools/
│   ├── requirements.in            # Human-edited Python requirements
│   ├── requirements.lock          # Tested exact Python pins
│   ├── cli/
│   ├── log-collector/
│   ├── fault-runner/
│   └── firmware/{pack,sign,inspect,upload}/
├── scripts/
│   ├── doctor.py                   # Stdlib-only environment detector
│   ├── bootstrap-venv.sh
│   ├── build/
│   ├── flash/
│   └── test/
├── tests/{integration,simulation,bootloader,hil}/
├── hardware/{PINOUT.md,WIRING.md,LOGIC_ANALYZER.md}
├── docs/
│   ├── ARCHITECTURE.md
│   ├── PROTOCOL.md
│   ├── COMPATIBILITY.md
│   ├── FAULT_INJECTION.md
│   ├── BOOTLOADER.md
│   ├── FIRMWARE_UPDATES.md
│   ├── USER_DEMO.md
│   └── PHASE_REPORTS/
├── third_party/
│   └── stm32-cmake/               # Git submodule at exact tested commit
├── deps/LOCK.md                   # Versions, pins, provenance, licenses
└── .github/workflows/ci.yml
```

**Directory rules:**
- Public `.h` files under `include/<package>/`, implementation under `src/`; CMake imported target names `Protocol::Protocol`, `NodeApi::NodeApi`, `Diagnostics::Diagnostics`, `FirmwareImage::FirmwareImage`, `BootProtocol::BootProtocol`, `VirtualIo::VirtualIo`.
- `libraries/` packages may depend only on standard C and other declared portable libraries; no upward dependency on firmware or examples.
- `examples/virtual-io` is an independently buildable demonstration component, linked by the STM application. Its simulation has no physical peripherals or flash HAL dependencies.
- `platform` separates STM HAL/board-specific code from boot algorithms and application logic. Avoid forcing a common platform HAL implementation where their flash ownership or interrupt needs differ.
- Do not create unused empty source files merely to match this illustration. Reserve directories and implement files when reached; keep the final public component boundaries.
- No generated binaries or downloaded SDK trees committed into repository. `build/`, `.venv/`, west dependencies outside Git or ignored, keys and captures appropriately ignored.

## 4. Tool and environment bootstrap: Codex must inform user

### 4.1 First action: dependency audit

Create `scripts/doctor.py` using **Python standard library only**; invoke it at the beginning and at each milestone needing new tools. Print a compact machine-readable/Markdown report with: tool name, detected version/path, required-for phase, status `READY`/`MISSING`/`VERSION_MISMATCH`, concrete official download URL or platform-specific install instructions, and **actions the user must perform**. Do not pretend required software is installed. Detect host OS (likely Arch Linux), architecture, access to USB serial/debug devices, udev rules, device permissions, toolchain versions, and SDK environment.

| Tool / dependency | Needed when | Setup rule |
|---|---|---|
| `git`, `cmake`, `ninja`, `cc`, `ctest`, `python3` | Phase 1 | Detect locally; show exact user install command if missing |
| `clang-format`, compiler warnings, optional `cppcheck`/`clang-tidy` | Phase 1+ | Pin formatter config; report missing static analyzers, don't silently skip |
| `arm-none-eabi-gcc`, `binutils`, newlib | Phase 1+ | Verify correct Cortex-M0+ flags and linker support; propose Arch packages if absent |
| `west`, Nordic nRF Connect SDK/toolchain, required Zephyr Python environment | Phase 1+ | Use official compatible NCS setup; SDK-managed Python may be separate from `tools/.venv` |
| Segger J-Link / `nrfutil` / Nordic flash/debug tooling | Phase 1+ | Check connected DK, debug permissions and firmware; no automatic destructive flashing |
| STM32CubeProgrammer CLI or working ST-LINK OpenOCD alternative | Phase 1+ | Check ST-LINK target and permissions; prefer vendor tool for ROM recovery/option-byte inspection |
| `ObKo/stm32-cmake`, CubeU0 CMSIS/HAL and FreeRTOS kernel | Phase 1+ | Pinned submodule + controlled CMake fetch; no manually vendored copies |
| `pyserial`, `bleak`, `pytest`, `cryptography` as needed | Phases 2–7 | **Project Python virtual environment**, exact versions in `requirements.lock` |
| `sigrok-cli`/PulseView or other logic-analyzer decoder | Phase 4 (optional) | Report manual install instructions; no analyzer hardware required |
| BLE adapter/interface usable by PC | Phase 6 | Nordic DK is available; host PC BLE capability is not guaranteed. Provide USB/VCOM alternative and explain limits |

**Virtual environment:**

```bash
python3 -m venv .venv
.venv/bin/python -m pip install --upgrade pip
.venv/bin/python -m pip install -r tools/requirements.lock
```

Create `scripts/bootstrap-venv.sh` with equivalent idempotent behavior, a version check and helpful failure output. `requirements.in` contains direct project requirements; exact pinned lock is created with an appropriate resolver after testing. Python tooling uses `.venv/bin/python` (or the explicitly activated venv), never global `pip install` or unrequested `sudo`. NCS's own Python tooling has distinct constraints; don't force it into this venv if incompatible. Commands for both configurations belong in README.

**Automation permission:** Codex may inspect environment, create/reuse local `.venv`, install Python requirements into `.venv` if network available, fetch publicly available pinned dependencies, build, run host tests and generate scripts. It **must request user action** for unsafe electrical steps, changing board power supply settings, OS-level package installation requiring privileges, changing STM option bytes, enabling flash protection, or any unverified destructive flashing procedure. Missing tools are **not** an excuse to stop independent work; label unperformed checks and continue where safe.

### 4.2 SDK and version selection (verify and pin)

- **Nordic:** start with **nRF Connect SDK v3.4.1** (v3.4 LTS patch, based on Zephyr 4.4.2). Confirm whether a newer **stable, board-compatible** patch exists before locking; no preview/mainline without documented reason. Nordic build target expected `nrf54l15dk/nrf54l15/cpuapp`, verified against pinned SDK. Use modern devicetree-defined flash partitions for Nordic MCUboot; avoid obsolete Partition Manager recipes.
- **STM32:** `https://github.com/ObKo/stm32-cmake`, pinned exact Git submodule commit; its support for `STM32U083RC`, `stm32_fetch_cmsis(U0)` and `stm32_fetch_hal(U0)` is confirmed in upstream sources. Its default U0 package versions may lag current releases; explicitly choose compatible pinned CubeU0 CMSIS/HAL revisions and document how those are supplied to the CMake module. Toolchain selected before `project()`.
- **FreeRTOS:** Cortex-M0+ compatible port with STM32U0; native FreeRTOS C API, static task/queue storage where practical. Pin kernel revision and validate `ObKo`'s `FindFreeRTOS` port or import the pinned kernel directly. Do not assume a CMSIS-RTOS wrapper is necessary.
- **Crypto:** choose a mature, tested P-256 ECDSA/SHA-256 verifier or equivalent signature scheme suitable for STM32U083 resources; don't implement primitives from scratch. Sign/verify on host before selecting on-device library. Benchmark `.text`, `.rodata`, stack and verification time.
- Exact commits/tags/hashes, setup steps and licenses go into `deps/LOCK.md`. `west manifest --freeze` records the resolved workspace.

### 4.3 CMake integration requirements

The independent STM32 application and bootloader use `third_party/stm32-cmake/cmake/stm32_gcc.cmake` as the toolchain and `ObKo`'s U0 CMSIS/HAL targets. Its upstream examples use:

```cmake
stm32_fetch_cmsis(U0)
stm32_fetch_hal(U0)
find_package(CMSIS COMPONENTS STM32U083RC REQUIRED)
find_package(HAL COMPONENTS STM32U0 REQUIRED)
```

In `firmware/stm32u083/cmake/Dependencies.cmake`, implement pinning/cache reuse and compatibility checks; use `CMSIS::STM32::U083RC`, the required `HAL::STM32::U0::<DRIVER>` targets and appropriate `FreeRTOS` port target if validated. The bootloader is **bare-metal**, without FreeRTOS. Provide independent `CMakePresets.json` or reproducible command lines for STM app, both slot variants, and bootloader. Do not use an STM-target toolchain when configuring the root native CMake project.

Every portable library has an actual `CMakeLists.txt`, `cmake/Sources.cmake`, `include/`, `src/`, `tests/`, plus when used in nRF `zephyr/module.yml`, `zephyr/CMakeLists.txt`, `zephyr/Kconfig`. Define source inventory once and consume it from both normal CMake and Zephyr CMake. For example, `libraries/protocol/zephyr/module.yml`:

```yaml
name: multi_mcu_protocol
build:
  cmake: zephyr
  kconfig: zephyr/Kconfig
```

In Nordic application `CMakeLists.txt` **before** `find_package(Zephyr ...)`:

```cmake
get_filename_component(MML_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
list(APPEND EXTRA_ZEPHYR_MODULES
    "${MML_ROOT}/libraries/protocol"
    "${MML_ROOT}/libraries/node-api"
    "${MML_ROOT}/libraries/diagnostics"
)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(multi_mcu_host C)
```

Append the `firmware-image`/`boot-protocol` modules only when the Nordic firmware actually needs them. Each Zephyr module's `CMakeLists.txt` must compile **the same source files** as native CMake, using Zephyr's `zephyr_library*` APIs and explicit include paths; no duplicate library implementation. Check the exact module Kconfig/target ordering with the chosen NCS. If creating an umbrella west manifest, verify `west manifest --validate`, `west list`, and `west update` from a documented workspace layout. No untested manifest syntax.

## 5. Boards, pins and wire contract

### Safety and verification gate

Before wiring, Codex must produce `hardware/PINOUT.md` with **physical header/pin numbers**, nRF pinctrl/Zephyr overlay names, STM port/pin and AF settings, any solder bridge/board debugger conflict, polarity and **safe reset defaults**. Ground it in the exact Nordic DK and NUCLEO-U083RC hardware revisions and user guides. Provide a physical user verification checklist for 1.8 V vs 3.3 V; never treat code inspection as voltage measurement.

**Proposed signal functions (not verified physical pins):**

| Signal | Direction | Purpose |
|---|---|---|
| `HOST_TX` / `NODE_RX` | nRF → STM | UART frames; STM USART1 RX candidate PA10 (verify) |
| `NODE_TX` / `HOST_RX` | STM → nRF | UART frames; STM USART1 TX candidate PA9 (verify) |
| `GND` | Shared | Common reference, not shared power rail |
| `NODE_IRQ_N` | STM → nRF | **Level asserted** while unserviced events are pending; missed-IRQ demo |
| `NODE_READY` | STM → nRF | Indicates initialized and ready application; low during reset/boot |
| `HOST_RESET_N` | nRF → STM NRST | Open-drain pull-low/high-Z release, avoid ST-LINK contention |
| `HOST_BOOT0` | nRF → STM BOOT0 | Optional ROM-loader entry; verify board routing/option bytes before use |
| `NODE_DEBUG` | STM → test point only | **One reusable GPIO marker** accessible to analyzer; not a host interconnect |
| `HOST_DEBUG` | nRF → test point only | Optional extra marker; don't consume scarce required pins |

UART app baseline `115200 8N1`. STM ROM bootloader typically uses `0x7F` sync with USART **8E1** framing; confirm against target AN2606/AN3155 and change configuration accordingly. STM debug logging should use separate ST-LINK VCOM UART where board routing allows (candidate USART2 PA2/PA3); nRF uses its own DK debug VCOM/RTT. Never feed ASCII logs into binary protocol UART. Do not claim ROM boot works until boot pins, option bytes and ST-LINK/user header connections are checked.

**Minimal analyzer setup:** CH0 host UART TX, CH1 node UART TX, CH2 either `NODE_DEBUG` or `NODE_IRQ_N` (plus ground). Store decoder settings, expected event markers, traces where available and explicit instructions for PulseView/sigrok. The `NODE_DEBUG` mark is a GPIO event, **not a measurement of real power voltage**.

**Boot control caution:** STM BOOT0 behavior can depend on option bytes, and careless changes can block recovery. Default BOOT0 low, reset release high-Z, event IRQ high/deasserted, READY low. Verify pull resistors; no driving unpowered MCU through GPIO. In no-wire mode, all protocol development still proceeds with native tests.

## 6. Generic protocol and node API

### 6.1 Protocol frame v1

- UART byte stream, COBS encoding and `0x00` delimiter, CRC32 over decoded header+payload, little-endian manually encoded fields (never cast structs onto the wire).
- Proposed decoded fields: `magic:u16`, `wire_version:u8`, `kind:u8`, `flags:u8`, `sequence:u16`, `length:u16`, `payload[length]`, `crc32:u32`. Maximum decoded packet initially **256 bytes**; compute exact maximum payload and compile-time/host tests.
- Frame kinds: `REQUEST`, `RESPONSE`, `EVENT`. Define status/error codes and a version negotiation path. Code rejects impossible lengths before any copy, bad CRC, unsupported major version, invalid enum/flags and overlapping fragments.
- Request acceptance is not the same as operation completion. `sequence` correlates a response to a request; **operation ID** uniquely identifies a long-running action and persists across retries. Include session/boot generation so stale packets after reset cannot be mistaken for valid completions.
- Link layer is **asynchronous**, fixed-capacity RX/TX buffers, bounded timeouts, error counters and explicit queue ownership. No heap allocation or blocking software logging in ISR. Corrupted streams always resynchronize at the next delimiter.
- CRC detects transmission corruption; it does not authenticate either MCU. Firmware signing belongs to the boot/update trust domain.

### 6.2 Node API (application neutral)

| Category | Illustrative commands/events |
|---|---|
| Basic management | `HELLO`, `GET_IDENTITY`, `GET_VERSIONS`, `GET_CAPABILITIES`, `GET_HEALTH` |
| Service model | `LIST_SERVICES`, `INVOKE_SERVICE`, `SERVICE_ACCEPTED`, `SERVICE_RESULT`, `SERVICE_EVENT` |
| Node lifecycle | `ENTER_MAINTENANCE`, `GET_RESET_REASON`, `SYNC_TIME`, `HEARTBEAT` |
| Diagnostics | `GET_FAULTS`, `READ_LOG_RECORDS`, `CLEAR_RECOVERABLE_FAULT`, `GET_COUNTERS` |
| Update negotiation | `GET_BOOT_CAPABILITIES`, `REQUEST_UPDATE_MODE`, `QUERY_UPDATE_STATE` |

Identify services by stable numeric IDs and versioned typed payloads. `node-api` must not define a product-specific `START_TEST`, power relay, pump, or fixture. A service description includes service ID, compatible API major/minor, supported operations, limits/units and maximum message size.

**Public C naming contract:** `Protocol_EncodeFrame()`, `Protocol_ConsumeBytes()`, `NodeApi_DispatchRequest()`, `NodeApi_RegisterService()`, `Diagnostics_RecordFault()`, `VirtualSensor_Read()`, `VirtualActuator_Start()`, `BootControl_SelectSlot()`. Typedef style `Protocol_Frame`, `NodeApi_Service`, `Diagnostics_Fault`; constants `PROTOCOL_MAX_FRAME`, `NODE_API_SERVICE_SENSOR`, `BOOT_SLOT_0`.

### 6.3 Generic virtual-I/O demonstration

STM registers services from `examples/virtual-io`:

- **Virtual sensor:** deterministic integer readings in millivolts, milli-degrees C or arbitrary unit-scaled samples; configurable sampling period, event threshold, sample count, rate limits and `ADC_STUCK` simulation.
- **Virtual actuator:** asynchronous `START`, `STOP`, `GET_STATE`; simulated duration, progress, completion and `JAM`/`TIMEOUT`, finite maximum runtime; never claims an actual motor exists.
- **Onboard GPIO demonstration:** remote toggle of STM LED, STM button publishes an event. Nordic buttons may request sensor read, actuator start/stop or cycle fault profile; map buttons after inspecting actual aliases.
- **Simulated power/enclosure sequencing:** an abstract virtual output such as `SIM_OUTPUT_ENABLE`, enabled only after modeled preconditions. This is for ordering/fault timing demos **only**, not real power control.
- Two **service personalities** (sensor-only vs sensor+actuator) selected via node config/build option. The same host firmware must discover both via capabilities.

Simulation must use a deterministic seed and monotonic clock adapter, support accelerated native tests, never sleep in virtual model logic, and enforce bounded numeric/queue limits.

## 7. Logging, fault injection and logic-analyzer lessons

### 7.1 Observability

- Nordic: Zephyr logging over its configured VCOM/RTT; STM: independent debug VCOM and nonblocking bounded log queue. Each identifies MCU, firmware version, boot/session ID and local uptime.
- Cross-MCU diagnostic records use a small typed format: source, severity, domain, code, monotonic timestamp, optional correlation/operation ID and details. Preserve local records on disconnect until buffer full, report overflow count. Nonvolatile persistence only if explicitly implemented and wear-managed.
- Python `tools/log-collector/` correlates both serial streams. Document timestamp synchronization uncertainty (boot clocks not globally synchronized) and include a correlation marker/event to compare traces.
- In normal operation, no logs on the binary UART except **structured diagnostic messages** inside valid frames.

### 7.2 Controlled injection architecture

Development-only module in **both firmware apps**, with compile-time exclusion/restriction from production-like builds. Runtime selection via local CLI or documented onboard buttons. Always restore default behavior after reboot unless a dedicated test requires otherwise; never let injected behavior defeat actual board protections. Provide `fault list`, `fault enable <id>`, `fault disable`, and `fault status` in PC/host tooling (names may be finalized once).

**At minimum implement and document:**

| Injection | Evidence to collect | Required correct behavior |
|---|---|---|
| Startup READY asserted too early / startup race | READY/DEBUG versus first UART request | Host waits for valid HELLO/capabilities and rejects premature traffic |
| Deliberately short IRQ pulse / masked interrupt | IRQ plus UART event sequence | Normal design uses **level-held IRQ** plus queue draining/poll fallback |
| Delay node response past host timeout | UART TX/RX, DEBUG marker | Timeout/retry with bounded backoff, no duplicate operation |
| Drop acceptance response to long operation | Two same-operation requests and actuator DEBUG pulses | Deduplicate operation ID; execute only once |
| Bad CRC or malformed COBS | Raw UART bytes + parser counters | Reject and resynchronize; no state mutation |
| Truncated frame from simulated node reset | UART and restart HELLO | Reset session and recover cleanly |
| Virtual output enabled before prerequisite | `NODE_DEBUG` marker vs host request | Fixed model enforces prerequisite before enable |
| Node communication task stall | Heartbeat/READY/DEBUG timing | Watchdog or supervised reset with reported reason |
| Host reboot during virtual actuator | Node log/progress/state | Node safely completes or aborts per documented policy, no automatic re-run |
| Incompatible capability/API version | HELLO/GET_CAPABILITIES | Unsupported requests rejected; compatible ones continue |
| Bad signed image / update interruption (later) | Update messages, slot metadata, boot logs | Reject new image, recover last confirmed slot or ROM path |

Three **mandatory analyzer tutorials**: (A) lost IRQ, (B) duplicate operation after missing response, (C) virtual output sequencing bug. Each tutorial includes an injected buggy implementation, how it manifests in logs, exactly which physical pins to probe, illustrative/real expected waveform, measurement/time markers, corrected code, host regression test and HIL procedure. Do not label an illustrative capture as a real capture. If user has no logic analyzer, provide a timing-event trace export (CSV/VCD optionally) that makes the same logical sequence inspectable, clearly labeled as **simulated**.

### 7.3 Test and reporting rules

- Use native CTest unit tests, deterministic simulation scenarios, COBS/parser fuzz/property-style tests, `pytest` for Python/serial integration and optional real-board HIL.
- All fault-injection cases must work from scripts with deterministic seeds and explicit maximum durations.
- Store expected traces and explain analysis limitations: UART decoding shows **wire bytes**; one GPIO marker indicates a chosen firmware point, not everything that happened inside MCU.
- No fake `PASS` for tests not run on boards. Use `PASS`, `FAIL`, `NOT_RUN_HARDWARE`, `BLOCKED_TOOL`, `BLOCKED_ELECTRICAL` with exact instructions to resolve.

## 8. Failure handling, sequencing and version compatibility

**Lifecycle:** `POWER_ON -> NODE_INIT -> READY -> NEGOTIATING -> ONLINE`; alternate states `DEGRADED`, `MAINTENANCE`, `UPDATING`, `RECOVERY`, `INCOMPATIBLE`. A timeout or reset starts a **new session generation**. If the host reboots, the node continues with finite, documented local virtual-actuator limits; never repeats commands just because of reconnection.

**Watchdog/heartbeat:** Have a local STM watchdog observing meaningful progress (not unconditional timer kicks), host heartbeat/ready tracking, reset-reason recording, and escalation after repeated node crashes. Distinguish link loss, application task stall and MCU reset. BOOT0/NRST are not used impulsively on every transient UART timeout. Define watchdog and host retry intervals using measured scheduling and protocol latency; start with conservative test defaults and tune based on HIL.

**Firmware versions:** Independently version Nordic firmware, STM application, STM custom bootloader, protocol wire major/minor, service/API major/minor and image format. Every image embeds Git ID and build provenance; `GET_VERSIONS` exposes relevant information. Compatibility policy: unknown wire major -> reject; known major with unsupported operation -> explicit error; new optional capability -> ignore safely; firmware update must check hardware ID, supported protocol, image target/slot and rollback policy before activation.

**Fault ownership:** protocol parser handles framing; node-api rejects invalid service calls; application enforces simulation constraints; local watchdog resets broken task/app; bootloader ensures executable image integrity; Nordic can remotely supervise but cannot override the STM bootloader's signature decision.

## 9. Firmware updates and the custom STM32 bootloader

### 9.1 Stage 1: establish recovery before any custom bootloader

- **Nordic:** use SDK's supported MCUboot + MCUmgr update mechanism, BLE when available and a documented USB/debug alternative when possible. Check external-flash staging and partition layout on the *actual* DK/NCS revision, verify signature and confirmation/revert policy, update instructions, and flash footprint. Do not claim automatic rollback until tested.
- **STM32 ROM:** verify NUCLEO-U083RC BOOT0 option bytes and factory system-memory bootloader UART availability against ST AN2606/AN3155, and prove a **manual recovery route** with STM32CubeProgrammer/ST-LINK first. Only then implement nRF-host-controlled ROM programming on USART, if direct BOOT0/NRST wiring and board behavior support it. Use separate ROM 8E1 framing. If automated entry is unavailable on the board, document verified manual entry rather than inventing GPIO magic.
- In the **ROM-recovery stage**, STM32 application may occupy the factory default flash region; a future custom bootloader **changes the STM32 memory map**. Preserve build presets/profiles so ROM-only development and custom-bootloader variants are explicit and don't accidentally flash the wrong address.
- Nordic stages the **complete** STM firmware artifact into verified persistent storage *before* resetting the STM for programming, if sufficient storage is exposed/configured; verify storage/image bounds and hashes before erasing STM.

### 9.2 Stage 2: separate STM32 custom bootloader (Phase 7)

The custom bootloader is a standalone bare-metal CMake target built through pinned `ObKo/stm32-cmake`; it must run even when **both** applications are invalid. It uses only a small platform flash/UART abstraction and portable `firmware-image` / `boot-protocol` formats, plus a vetted signature verification implementation. It must not rely on an STM application task, FreeRTOS or the normal node service protocol.

**Proposed features:**

1. Identify/validate Slot 0 and Slot 1. Validate magic, schema, board/hardware ID, linked address, slot ID, image size, vector stack pointer bounds, reset vector address/Thumb bit, CRC/cryptographic hash and signature.
2. **ECDSA-P256 + SHA-256** is the initial candidate, subject to code-size/stack benchmarking. Trust a fixed public key or strictly controlled key-ID map in the protected bootloader; keep private keys outside source control. Never implement homemade crypto.
3. Explicit `CONFIRMED`, `PENDING`, `TRIAL`, `INVALID`, `RECOVERY` logic. Boot a newly programmed *inactive* slot for a limited number of attempts, require application confirmation only after its self-checks; otherwise return to previous confirmed slot.
4. Transactional boot metadata with at least two **distinct erase pages**, generation/sequence, CRC, validation and safe rollover. Metadata writes must survive a reset at every erase/program boundary. Journal updates cannot destroy the last valid record.
5. UART boot update protocol for `QUERY_SLOTS`, `ERASE_INACTIVE`, `WRITE_CHUNK`, `FINALIZE`, `SET_PENDING`, `GET_BOOT_STATE`, `REBOOT`, with bounded packet size, ack, offset checking, resume/restart policy and image verification before `PENDING`.
6. Application-to-bootloader confirmation interface and shared boot state definitions. Define whether confirmation is via protected flash metadata or a documented boot service; do not require changing bootloader internals from app.
7. Reliable jump: disable relevant interrupts/peripherals, set stack pointer, set vector relocation (if supported), and branch to the selected image after validation. Test actual interrupt behavior in both slots.
8. Recovery when neither slot is valid, when both metadata copies are corrupt, after interrupted programming or when host update communication fails. Keep ST ROM/SWD recovery documented as last resort.
9. Secure-boot **limitations**: demo signing only authenticates images against a stored public key. With open SWD, no production key provisioning, and unlocked readout/write protection, it is **not production-grade secure boot**. Never enable RDP Level 2 or irreversible protection for this project.

**Flash constraint:** STM32U083RC: **256 KiB single-bank flash**, **2 KiB erase pages**, **40 KiB SRAM**. Proposed *provisional* layout:

| Region | Offset from `0x08000000` | Capacity | Physical span |
|---|---|---|---|
| Bootloader | `0x00000` | 64 KiB | `0x08000000..0x0800FFFF` |
| Slot 0 | `0x10000` | 88 KiB | `0x08010000..0x08025FFF` |
| Slot 1 | `0x26000` | 88 KiB | `0x08026000..0x0803BFFF` |
| Metadata/reserved | `0x3C000` | 16 KiB | `0x0803C000..0x0803FFFF` |

These are **starting allocations, not guaranteed fits**. Reserve an explicitly defined image metadata/trailer area inside each slot or header area as required by the signed-image format. Generate slot/linker constants from `firmware/stm32u083/memory/flash_layout.yaml`; never manually synchronize addresses across bootloader, apps and Python packager. Build the same application for **two link addresses** (slot0 and slot1). A slot0-linked Cortex-M binary is not automatically executable in slot1. Validate the specific STM32U083 vector-table relocation capability and boot jump architecture; if direct dual-slot XIP needs extra remapping or exceeds resources, document and implement a staging/swap alternative instead of pretending directly bootable A/B works.

**Size gates:** CI must fail if bootloader doesn't fit 64 KiB, either slot binary+manifest exceeds its budget or RAM/stack cannot support signature verification. If tight, reduce features or benchmark another supported implementation and document trade-offs—never silently omit signature checks. Build map/size reports for bootloader and both slots.

**Image signing/packaging:** `tools/firmware/` Python code produces deterministic metadata, SHA-256, signature and per-slot package. The exact canonical signed bytes, algorithm ID, key ID, version and anti-rollback policy must be specified and covered by golden vectors. Distinguish **CRC32 for transmission** from **SHA-256/signatures for image integrity/authenticity**. Key material is supplied via a user-provided local file/environment; `.gitignore` keys and example development keys must be explicitly labeled insecure. Anti-rollback is a separately analyzed policy, not automatic just because signed firmware exists.

**Core HIL update cases:** blank Slot 1, valid Slot 0; valid signed Slot 1 pending then confirmed; trial crash causing revert; interrupt erase/program at different offsets; corrupted image/hash/signature; tampered target address; both metadata pages stale/corrupted; selecting slot explicitly (development CLI restricted to safe transitions); recovery with no valid apps. Hardware erase stress tests must be bounded to protect flash endurance.

## 10. Implementation phases

The original eight phases below retain their historical reports. The approved
continuation, Phases 9–12, is specified in [POST_BASELINE_PLAN.md](docs/POST_BASELINE_PLAN.md).
Execute all twelve phases in sequence; the current frozen interface is
[CONTROL_API.md](docs/CONTROL_API.md). This continuation supersedes earlier whole-image
Nordic staging requirements for STM updates: PC streaming is now the chosen architecture.

For **every phase**, Codex produces a concise plan and contracts first; delegates non-overlapping code to parallel agents where supported; runs feasible tests; writes `docs/PHASE_REPORTS/PHASE_N.md` containing exact commands, tool versions, Git revision, PASS/FAIL/BLOCKED/NOT_RUN_HARDWARE and next steps. **Never rely on hardware being attached to Codex's environment.** If unavailable, finish native/tests/builds and output step-by-step human HIL instructions.

### Phase 1 — Repository foundation, tools, independent builds

**Implement:** final package boundaries and CMake target names; pinned `ObKo/stm32-cmake` submodule; NCS/west manifest/bootstrap; `.gitignore`, `.clang-format`, lockfile; `scripts/doctor.py`; `.venv` bootstrap and Python packaging; native CMake/CTest; stub Zephyr modules (only for real compiled code); both boards' LED/button/logging bring-up; STM HAL + FreeRTOS app; reserve the STM custom bootloader layout/project directories and authoritative flash-layout schema but don't impose experimental A/B partitions on normal ROM-only bring-up.

**Parallel:** integration/CI agent sets root/CMake/manifest; STM agent brings up STM; Nordic agent brings up nRF; library agent builds native targets; tool agent creates doctor/venv. Integrator owns dependency versions and final shared CMake contract.

**Exit tests:** native tests execute; each MCU firmware compiles and flashes (or hardware test instructions produced); each onboard LED changes as documented; separated debug logs visible; no missing dependency silently hidden; venv reproducible; exact board voltage/wiring gate documented. No inter-board wires necessary yet.

### Phase 2 — Electrical interface, UART framing and discovery

**Implement:** verified header pin map; safe GPIO defaults; app UART and distinct debug log UART; READY + IRQ_N (and reset/BOOT pins only after verified); UART drivers/ring buffers; portable protocol encode/decode and golden vectors; HELLO, GET_IDENTITY/GET_CAPABILITIES, heartbeat and session generation; host link state machine. UART console commands to introspect status.

**Parallel:** protocol agent owns framing/parser tests, Nordic agent link adapter, STM agent UART/GPIO, validator owns end-to-end tests. Freeze wire header and service IDs before parallel coding.

**Exit tests:** native corruption/overflow/partial-stream tests and actual two-board HELLO/identity exchange or documented `NOT_RUN_HARDWARE`; no dynamic buffers; both UART directions observable at testable headers. Logic-analyzer wiring documentation ready.

### Phase 3 — Generic services and virtual I/O example

**Implement:** portable `node-api` service registry/schema; `examples/virtual-io` deterministic sensor and async actuator; STM onboard button publishes events; nRF onboard buttons and console invoke capabilities; two STM node personalities using same Nordic firmware; correct separate request acceptance/completion and deduplication.

**Parallel:** node-api library agent and virtual-io agent own different projects, STM service adapter agent and Nordic client agent integrate after frozen service contract, tests agent builds native simulations.

**Exit tests:** sensor samples/events and actuator progress/completion visible in both logs; duplicate START requests execute once; two personalities discovered and unsupported commands rejected; tests use deterministic clock and seed; simulated output never described as actual electrical power.

### Phase 4 — Diagnostics, logs, deliberate bugs and logic-analyzer tutorials

**Implement:** portable diagnostics library; nonblocking logging backends, correlation and Python log collector; fault injection controls and `tools/fault-runner`; DBG GPIO mode switching; three mandatory lessons (missed IRQ, duplicate operation and bad virtual output ordering) plus UART corrupt/truncate/delay cases; simulated trace export when no analyzer exists.

**Parallel:** diagnostics agent/library tests, Nordic/STM fault adapters in their own directories, Python tools agent, independent HIL/test agent.

**Exit tests:** each bug can be enabled/disabled, its failure reproduced, then fix validated automatically; structured counters and captures documented; CH0/CH1/CH2 probe guides specify physical pins, decode settings and markers; never fake captures.

### Phase 5 — Supervision, safety policies, compatibility

**Implement:** link timeout vs app hang distinction; watchdog progress monitoring; reset-cause diagnostics; remote STM reset escalation once approved; startup/shutdown/maintenance state machines; resynchronization on reboot; per-service/firmware version compatibility, session reset, bounded retries; host reset and node reset tests.

**Parallel:** STM recovery state machine, Nordic supervision, compatibility library tests and independent HIL orchestration after agreeing on state transitions.

**Exit tests:** simulated stalls and actual board reset where permitted recover without phantom operation; repeated crashes become latched fault; unsupported service versions are rejected without breaking other services; two boards can independently reboot and re-discover.

### Phase 6 — Standard firmware management: Nordic MCUboot and STM ROM recovery

**Implement:** `firmware-image` manifest/parser and Python image tooling; Nordic MCUboot/MCUmgr with verified partitions and BLE or supported serial management; STM factory ROM bootloader procedure, then optional nRF-controlled ROM programming via UART+BOOT0+NRST if board permits; persistent staging and SHA-256 validation; firmware compatibility check, build IDs and release/package manifest.

**Parallel:** Nordic OTA agent, STM ROM-recovery agent, firmware-image/tools agent, HIL/test agent. Pin update metadata format and target addresses before agents work; one integrator owns release/compatibility manifest.

**Exit tests:** nRF update/confirm/recovery demonstrated, STM recoverable from erased/corrupt app via verified ROM/SWD route, bad image rejected, interrupted programming has a documented verified recovery, and no claim of atomic multi-MCU upgrade or STM custom A/B rollback yet. If BLE adapter or BOOT0 automation unavailable, provide a supported alternative and report test status.

### Phase 7 — Custom STM32 bootloader: signed direct A/B (advanced)

**Implement:** independent bootloader CMake target and `boot-protocol`/`firmware-image` APIs; flash-layout generation and two linked app variants; boot validation, vetted signature verification, boot metadata journal, pending/trial/confirmed/rollback, inactive-slot programmer, app confirmation and recovery. Integrate host firmware delivery and boot control. Preserve ROM rescue path.

**Internal milestones (not extra phases):**
1. Compile/link bootloader, boot Slot 0, verify interrupts; test boot jump.
2. Build both link variants, boot Slot 1, add slot selection and boundary tests.
3. Add canonical manifest, hashing/signing, key management and negative tests.
4. Add redundant metadata, pending/trial confirmation, rollback/power-loss fault tests.
5. Add UART update protocol, host delivery and real-board recovery exercises.

**Parallel:** bootloader core agent owns STM bootloader state/jump, image-format/crypto/test agent owns portable formats and test vectors, host update agent handles Nordic, tooling agent builds Python signing, validation agent runs native state-machine tests. **No concurrent edits to flash map or image schemas**; architect/integrator first freezes them and drives integration. Bare-metal bootloader work can start alongside final Phase 6 tooling once contracts freeze, but phase sign-off remains sequential.

**Exit tests:** both correctly linked slots can execute (if hardware supports it), no wrong-slot binary activation, signatures checked, corrupt images rejected, interrupted update restores confirmed image or enters recovery, metadata withstands reset injection, verified ROM recovery remains accessible. If signed two-slot boot cannot fit *without weakening verification*, report exact measured limitations and implement documented feasible fallback; **do not claim completion of direct A/B**.

### Phase 8 — Full regression, CI, documentation and extraction readiness

**Implement:** CI pipelines for native libraries, both embedded firmware builds and bootloader slot variants; Python tests; optional HIL runner and test-matrix scripts; comprehensive docs, tutorials, C API contracts, pin map, SDK/lock file, external dependency audit, release/version matrix; demonstration script and selected logic-analyzer traces.

**Parallel:** separate agents for CI, docs/tutorials, Python HIL scripts and firmware size/report review; validation agent checks clean-checkout reproducibility.

**Exit tests:** clean checkout reproduces native tests and available firmware builds; full simulated scenarios pass; available on-board hardware demonstrably runs integrated workflow; demo explains lost IRQ, duplicate operations, recovery and update path; all incomplete hardware checks prominently labeled; library boundaries permit future repository split.

### Milestones 13–16 — Reliability and demonstration extension

The approved continuation is specified in [RELIABILITY_PLAN.md](docs/RELIABILITY_PLAN.md).
Preserve Phases 1–12 profiles, signing formats, application slot addresses and recovery
paths. Implement sequentially: (13) host watchdog and bounded automatic recovery;
(14) persistent node fault history; (15) negotiated reliable events and IRQ wake;
(16) reproducible failure evidence and guarded interruption acceptance.
Freeze additive interfaces before delegating and write PHASE_13 through PHASE_16
reports with software results and hardware acceptance separately identified.

## 11. Coding standards, agent orchestration and phase gates

**C:** C11 only; `Module_Function` for public and local functions where meaningful, `Module_Type` structs/enums; `MODULE_CONSTANT` values; descriptive file/module names. Use small single-purpose functions, fixed-size buffers, explicit ownership/lengths, `stdint.h`/`stdbool.h`/`stddef.h`, named return codes, no hidden global coupling. No C++ and no `malloc/free` in project firmware/service/protocol hot paths; SDK internals may allocate as required. ISR must do minimal work; driver/timing/RTOS adapters are distinct from portable algorithms. Avoid packed struct wire aliasing. Verify compile warnings, stack sizes, firmware flash size, bounds checks and timeout paths.

**Parallel Codex model:** lead integrator specifies API/schema contracts, assigns **exclusive directory ownership**, spins independent subagents if tool supports it, asks validation/review agent to test independently, then integrates. Agents must not race-edit `west.yml`, `libraries/*/include`, `flash_layout.yaml`, STM option-byte configuration or root package manifest. Use worktrees/branches if supported; otherwise disjoint paths. No long-running unsupervised work. Report missing tool/permissions promptly, but proceed with unaffected phases. Detailed agent behavior in `AGENTS.md`.

**Every phase report includes:** goals delivered, changed modules, exact build/flash/test commands, tooling versions, test results with `PASS`/`FAIL`/`BLOCKED_TOOL`/`BLOCKED_ELECTRICAL`/`NOT_RUN_HARDWARE`, proof of actual hardware vs simulated tests, new warnings, memory usage and next-step tasks. Do not call the phase fully verified when required HIL remains untested.

**Release requirements:** independent host/node/bootloader semver; service/wire/image format versions; signed image metadata check; Git ID; pinned SDK and dependency versions; reproducible Python tooling; all persistent boot state documented; fail-safe simulated states without fictitious safety guarantees.

## 12. Trusted starting references and unresolved checks

Codex should inspect and pin the **version-matched** original sources, not copy random forum examples:

- Nordic nRF Connect SDK releases: <https://github.com/nrfconnect/sdk-nrf/releases>; nRF54L15 DK power: <https://docs.nordicsemi.com/r/bundle/ug_nrf54l15_dk/page/ug/nrf54l15_dk/hw_desription/power_supply.html>.
- Zephyr modules documentation: <https://docs.zephyrproject.org/latest/develop/modules.html>.
- ObKo STM32 CMake: <https://github.com/ObKo/stm32-cmake>; verify U0 support and fetching functions against pinned commit.
- ST MCU: <https://www.st.com/en/microcontrollers-microprocessors/stm32u083rc.html>; ST datasheet and STM32U0 Flash training, ST NUCLEO-U083RC user manual/schematics, AN2606 (ROM boot configurations), AN3155 (USART protocol).
- Upstream STM32CubeU0 HAL/CMSIS repositories and FreeRTOS-Kernel/port used in the build; record exact revisions.
- MCUboot/MCUmgr and Mbed TLS/other chosen vetted signature-verification references from the pinned SDK.

**Open checks before physical wiring/bootloader work:** actual board revisions, Nordic voltage configuration, exact nRF pin assignments, STM USART1/USART2 routing and solder-bridge conflicts, BOOT0 selection/option bytes, ROM recovery UART support, Cortex-M0+ vector relocation behavior on STM32U083, custom A/B slot feasibility with the chosen app and signature library, and fully tested flash programming behavior. Preserve these as explicit gates, never quietly assume they're proven.
