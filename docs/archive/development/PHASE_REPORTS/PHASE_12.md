# Phase 12 — Metrics, acceptance and release

Date: 2026-10-25. Base Git: `2215e82c4ca41cfc790c2ff21b7f37f11b79fcc4`
plus uncommitted Phase12 implementation. Software implemented and verified.
Agent hardware access was unavailable. User subsequently passed strict runtime
acceptance:100 sensor responses,600.0004s watch,20 watch events,103 unique operations,
no retries/timeouts/protocol/UART errors/overflows/session changes. PC latency
p50=328.68ms,p95=329.41ms,max355.58ms; stack scans2596 main/3864 workqueue bytes free.
This does not close unperformed update/interruption/rollback HIL gates.

User follow-up: live metrics showed23 requests/replies, no retries/timeouts/UART
or protocol errors/overflows; stack scans reported2968 main/3864 workqueue bytes free.
Quick demo passed five sensor reads but exposed an overly strict CLI check:
other-service events sharing an actuator operation number were rejected as wrong
responses. CLI now filters asynchronous progress/completion by service while still
rejecting mismatched actual responses. Added two regressions; hardware retry pending.
User retry passed all quick checks and then the full runtime run described above.
Optional controls exposed the expected maintenance READY pulse as transient
discovering/session0; CLI now permits only that bounded maintenance-command
transition and requires the original nonzero session online before proceeding.
Three regressions cover enter/exit and rejection of a genuinely new session.
After the discovery-wait fix below, user controls retry PASS: maintenance enter/exit,
sensor, actuator, event watch, runtime metrics and remote reset/fresh-session recovery
all passed. Original session2654894508 changed to1013445503 after the explicit reset.
Fault exercise was NOT_SUPPORTED on the installed release STM firmware, as expected.
No firmware reflash was required for the CLI fixes.
Follow-up expanded that wait to the complete actual discovery sequence
(discovering0 → identity/originalsession → capabilities → services → online).
The first fix covered discovering0 only and still rejected the identity phase.
Tests now model all phases for both maintenance commands and explicitly reject a
different nonzero session at every later phase. Errors include observed state/session.

## Delivered

- Frozen plan/interfaces before parallel firmware, CLI/demo and release/resource
  tooling work. Independent read-only review and strict directory ownership.
- Separate `phase12` and `phase12-verbose` profiles; former retains warnings/errors,
  latter preserves info logging for a same-feature resource comparison. Older
  default/phase9/10/11 profiles remain selectable; no image/layout/security changes.
- Read-only SMP7/main-thread metrics snapshot: lifetime request/retry/reply/error,
  UART bytes/errors/overflow, event/history, discovery/session/restart counters,
  request latency and supported stack scans. Counters survive manager reinit;
  CBOR map capacity32 verified for the26-field response, including64-bit values.
- Job5/6 elapsed time and ACKed bytes/whole-job second, frozen on completion.
  No status heartbeat affects transfer inactivity; no wire-bandwidth claim.
- `mmctl metrics`, `mmctl demo`, root `scripts/demo.sh`. Default100 sensor requests,
  one actuator execution with25/50/75+completion, then600-second event watch.
  JSON evidence records operation uniqueness, contiguous sensor indices, event
  gaps, transport deltas and PC latency. Missing watch events are NOT_OBSERVED.
  `--require-acceptance` refuses incomplete100/600/asynchronous-event evidence.
- Optional `--exercise-controls --yes` maintenance/fault-cleanup/reset test,
  never default reset/flash/erase. Stable USB serial/VID/PID/interface matching
  for approved read-only Nordic reconnect; no uncertain mutation replay.
- Safe no-op HIL acceptance matrix; only explicit hardware/electrical flags enable
  runtime acceptance. Destructive cases remain operator procedures/evidence.
- Deterministic validated release ZIP (manifest+four artifacts only), bounded
  safe verification, no-clobber output, no keys/SDK trees. Static resource reports
  consume actual ELF and .su files. Existing manifest v2/v1 tools unchanged.
- Native framing benchmark, CI final-profile/resource checks, clean-source testing,
  final build/demo/release documentation.

## Audit and environment

All required tools available. Optional cppcheck absent (manual install:
`pacman -S --needed cppcheck`); no ttyACM/ttyUSB, libusb enumeration unavailable.
Arch Linux x86_64, native GCC16.2.1, Arm GNU15.2.Rel1, CMake4.3.1, Ninja1.13.2,
Python3.14.8/project .venv, west1.5.0, NCS3.4.1/Zephyr4.4.2/Zephyr SDK1.0.1,
CubeProgrammer2.23.0, J-Link9.24a, nrfutil8.2.0. No Python dependencies installed
globally, no private key read/generated, no hardware or option-byte writes.

## Verification commands

From root (`source ./.env.local`):

```sh
python3 scripts/doctor.py
scripts/test/run_native.sh
scripts/build/build_stm32.sh --public-key-header keys/stm32-dev-public.h
scripts/build/build_nordic.sh --profile phase12-verbose
cmake --build build/nrf54l15-host-phase12-verbose
scripts/build/build_nordic.sh --profile phase12
cmake --build build/nrf54l15-host-phase12
scripts/build/build_nordic.sh --profile phase11
scripts/build/build_nordic.sh --profile default
.venv/bin/python tests/hil/acceptance_matrix.py
cmake -S . -B build/native-benchmark -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build/native-benchmark --target protocol_benchmark
build/native-benchmark/protocol_benchmark
.venv/bin/python tools/firmware/resource_report.py \
  --elf verbose=build/nrf54l15-host-phase12-verbose/nrf54l15-host/zephyr/zephyr.elf \
  --elf optimized=build/nrf54l15-host-phase12/nrf54l15-host/zephyr/zephyr.elf \
  --size-tool arm-none-eabi-size --output build/phase12-comparison.json
git diff --check
```

| Check | Result |
|---|---|
| Root native libraries/framing/host/ROM/metrics tests | PASS:16 CTests |
| Bootloader journal/engine/real crypto | PASS:2 CTests |
| Python CLI/demo/reconnect/archive/resources/HIL tests | PASS:251 tests (including follow-up fixes) |
| Python dependency consistency | PASS |
| Fresh source snapshot, no SDK/build caches | PASS:11 native+2 bootloader+242 Python |
| Nordic verbose, optimized, previous Phase11, default | PASS |
| STM normal, both linked slots and loader | PASS |
| Real-artifact release archive double-create/compare/verify | PASS |
| Independent native/release safety review | PASS: no outstanding concrete findings |
| Live100requests/600seconds, latency/stack scan | USER-REPORTED PASS; update throughput not yet measured |
| Both updates, ROM repair, interruption, rollback, reconnect matrix | NOT_RUN_HARDWARE this phase |

Historical user evidence for Phase10/11 normal update/ROM repair remains in its
reports; it is not invented Phase12 extended acceptance. New fake-device tests
exercise successful/failing acceptance, event-ID zero-skipping wrap, skipped/duplicate
sensor indices, missing progress/completion, malformed/unadmitted events, host/session
change, USB identity ambiguity, rejected controls, report aliases, interruption,
HIL subprocess timeout/failure and strict incomplete acceptance.

## Fresh-source reproduction evidence

Exported tracked and non-ignored new source, excluding recursive submodules/build
caches, to a fresh temporary directory; copied only pinned crypto source dependencies
(no .git/build), then ran the canonical script with the installed project Python:

```sh
task_clean_dir=$(mktemp -d /tmp/multi-mcu-phase12-clean.XXXXXX)
git ls-files -z --cached --others --exclude-standard |
  tar --null --no-recursion -T - -cf - | tar -xf - -C "$task_clean_dir"
tar --exclude=.git --exclude=build -cf - third_party/micro-ecc third_party/mbedtls |
  tar -xf - -C "$task_clean_dir"
PYTHON="$PWD/.venv/bin/python" "$task_clean_dir/scripts/test/run_native.sh"
```

PASS. Five real-SDK integration tests were unavailable in that SDK-free snapshot
and were separately included in the full16-test run. This demonstrates clean
source/build independence, not an unperformed fresh SDK/network installation or
bit-identical cross-build. Full new-machine instructions: `FINAL_RELEASE.md`.

## Measured resources and optimization

Same feature set/toolchain/source; GNU size text+data and data+bss (bytes):

| Nordic app profile | text | data | bss | flash | RAM |
|---|---:|---:|---:|---:|---:|
| Phase12 verbose |91564|1256|37746|92820|39002|
| Phase12 warning/error |89232|1256|37746|90488|39002|

Measured flash saving **2332 bytes (2.51%)**, unchanged RAM. No buffer/polling/chunk
size optimization was made without target measurements; information logs alone
are compiled out in the final operator profile. SMP typed events remain present.
Zephyr region totals include alignment and can differ slightly from GNU size.
Both fit the696176-byte application partition/256KiB RAM. Default build uses
50616 flash/13072 RAM bytes (Zephyr region report); lifetime counters add modest
state even when the metrics command is not exposed.

STM text/data/bss unchanged: normal23280/104/9440; Slot0/Slot1 each23456/104/9440;
loader49544/116/2104. All fit existing64/88/88/16KiB flash and40KiB RAM bounds.
Existing SDK deprecation, newlib stub and loader RWX-segment warnings are retained,
not hidden; no newly introduced compiler warning remains.

Main/workqueue stacks4096 each, ISR2048. Individual compiler frames include
HostControl_Process776, HostUpdateAdapter_Process632, manager Process96 and
metrics encoder24 bytes. These are not whole-call-chain bounds or measured live
high-water evidence. Live scan is exposed only with supported firmware API.
manager_restarts counts reinitializations (including supervision); observed
unexpected node_reboots is not a physical reset-pulse total. Retained-history
overwrites are not automatically lost events. UART counters may wrap; acceptance
conservatively rejects decreasing counters instead of assuming a clean delta.

Host-only framing benchmark:20,000 valid207-byte frames/mode, native Release GCC.
Observed byte-at-a-time0.065348 CPU s (~306054 frames/CPU s), batched64-byte reads
0.042295 CPU s (~472869). Both decoded all frames with no rejects. These are
desktop CPU observations, not MCU/UART throughput or latency; runtime already
uses batched reads, so this did not justify changing target buffering.

## Release evidence

Created a new ignored `build/phase12-archive-validation.*` bundle using the final
Phase12 SDK signed Nordic image, existing trusted raw loader and signed Slot0/1
packages at system version0.11.0. Only the existing public PEM was read. Both
archive creations compared byte-identical and validated successfully:
SHA256 `9e386cc3a20207b346e58f2e1b6c5b370ee5cf36a4d783d49dc88074d732d361`.
No new signed application version was manufactured or private key bundled.

Equivalent reproducible commands for any staged bundle:

```sh
.venv/bin/python tools/firmware/release_archive.py create PATH/release.json PATH/one.zip --public-key keys/stm32-dev-public.pem
.venv/bin/python tools/firmware/release_archive.py create PATH/release.json PATH/two.zip --public-key keys/stm32-dev-public.pem
cmp PATH/one.zip PATH/two.zip
.venv/bin/python tools/firmware/release_archive.py verify PATH/one.zip --public-key keys/stm32-dev-public.pem
```

Deterministic packaging does not assert reproducible compiler output across Git
IDs/toolchains/paths. Manifest/raw-loader trust, MCUboot/STM signature verification,
open SWD/ROM and anti-rollback limitations remain explicitly documented.

## Handoff and remaining acceptance

Use `PHASE_12_TESTING.md` for root commands, press STM button during the watch,
retain JSON evidence and require acceptance_complete:true. `FINAL_RELEASE.md`
covers checkout/build/package reproduction. Matrix no-op reports all hardware
scenarios NOT_RUN_HARDWARE and does not automatically run destructive tests.
Extended interruption/corrupt-loader/empty-app/rollback checks require the existing
saved-artifact/recovery-path procedures. All twelve software phases are implemented;
the overall project is not fully hardware-accepted until those gates are recorded.
