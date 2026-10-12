# Phase 2 report

Date: 2026-10-12 (Europe/Skopje)
Baseline Git commit: `d99c830e4c3e71e3e2f3243b77de89615405e794`
Result: software implementation `PASS`; initial two-board discovery `PASS`; reset recovery pending

## Delivered

- Frozen and documented a fixed-capacity 256-byte decoded frame with explicit little-endian
  fields, COBS plus zero delimiter, IEEE CRC32, sequence IDs, frame kinds and strict flags.
- Added streaming recovery for partial, corrupt and overlong UART input without heap allocation.
- Added HELLO, identity, capability and heartbeat management messages with node session checking.
- Added Nordic interrupt-driven UART transport, bounded rings, READY-gated discovery, timeout/retry
  handling, sideband inputs and an independent debug output.
- Added STM32 USART1 interrupt RX transport, fixed ring, management responder, READY output,
  open-drain active-low IRQ and independent debug output. USART2 VCP logging remains separate.
- Froze the application signal mapping and documented the mandatory 3.3 V voltage gate, wiring and
  optional analyzer probes. RESET and BOOT0 remain intentionally excluded.
- Preserved independent native, Zephyr and STM32 CMake builds. STM32 still exports
  `compile_commands.json`.

## Dependency audit

Command: `python3 scripts/doctor.py`

- `PASS`: Linux x86_64, CMake 4.3.1, Ninja 1.13.2, native GCC, Python 3.14.7, clang-format
  23.1.3, clang-tidy, Arm GNU, west 1.5.0, J-Link 9.24a, STM32CubeProgrammer 2.23.0,
  OpenOCD, NCS 3.4.1 and pinned stm32-cmake.
- `PASS`: `.venv/bin/python -m pip check` reports no broken requirements.
- `MISSING_OPTIONAL`: cppcheck.
- `BLOCKED_TOOL`: nrfutil cannot create `/home/bojan/.nrfutil/logs/nrfutil.log` in the restricted
  execution environment. This does not affect compilation; verify `nrfutil --version` in a normal
  user shell before Nordic flashing.
- `NOT_RUN_HARDWARE`: the restricted environment cannot initialize libusb and exposes no ACM/USB
  devices to the audit.

The STM32 cross build used the requested Arm GNU 15.2.Rel1 toolchain through
`STM32_TOOLCHAIN_PATH`. The Nordic build used Zephyr SDK 1.0.1 / GCC 14.3.0 selected by NCS.

## Verification

### Native

Commands:

```sh
cmake --build --preset native-debug
ctest --preset native-debug --output-on-failure
git diff --check
```

Result: `PASS`, 3/3 CTest targets. Protocol coverage includes a canonical golden frame, maximum
payload, deterministic payload/chunk variations, byte-at-a-time input, concatenated frames,
malformed COBS, magic/version/kind/flag/length failures, CRC corruption, overlong-frame dropping
and recovery to the following valid frame. `git diff --check` passed.

### Nordic nRF54L15 host

Command:

```sh
source ./.env.local
.venv/bin/west build -b nrf54l15dk/nrf54l15/cpuapp firmware/nrf54l15-host \
  --build-dir build/nrf54l15-host-phase2 --pristine=always
```

Result: `PASS`, NCS v3.4.1 / Zephyr 4.4.2. Final image uses 44,112 bytes of flash (2.83% of the
reported region) and 11,192 bytes of RAM (4.27%). NCS emits its existing deprecated
`NRF_PLATFORM_LUMOS` warning.

### STM32U083 application

Commands:

```sh
source ./.env.local
cmake --preset debug -S firmware/stm32u083/application
cmake --build firmware/stm32u083/application/build/debug --parallel
arm-none-eabi-size firmware/stm32u083/application/build/debug/multi_mcu_stm32_application.elf
```

Result: `PASS`. After the hardware fault corrections, size is text 22,444 bytes, data 24 bytes,
BSS 6,880 bytes, total 29,348 bytes.
`firmware/stm32u083/application/build/debug/compile_commands.json` exists. Linker warnings for
unused newlib syscall stubs and an RWX load segment remain visible and are not hidden; linker
hardening/layout refinement is outstanding beyond this phase's communication bring-up.

## Hardware gate and outstanding verification

Status: voltage gate `PASS`. The user identified PCA10156 1.0.0 (date code 2025.30), configured
Nordic `VDD:IO` to 3.3 V, and measured both Nordic VDD:IO and STM VDD at 3.3 V while disconnected.
Never connect board power rails. Follow `hardware/WIRING.md`, flash both images and verify:

1. READY remains low until STM transport initialization completes.
2. Nordic progresses to `online`, reports identity `stm32u083rc-node`, a nonzero session and
   capabilities `0x00000007`.
3. Separate Nordic and STM logging transports remain usable; no ASCII appears on the application
   UART.
4. Resetting or power-cycling STM takes Nordic out of online and causes clean rediscovery after
   READY returns, with no Nordic reset.
5. Optional analyzer capture shows COBS-delimited traffic in both directions and active-low IRQ
   idles deasserted. Any real capture must record equipment, threshold, sample rate and Git commit.

Phase 2 should be marked fully complete only after those results are recorded. No hardware flash,
option-byte change or claimed real capture was performed by this report.

## Hardware finding after initial flash

The first two-board run reached READY but not HELLO. Review found that the application link had
incorrectly selected Nordic `uart30` for P1.11/P1.12, and the board's default console pinctrl still
claimed P1.06/P1.07 as RTS/CTS. The corrected image uses `uart21` for P1.11/P1.12 and overrides the
`uart20` console pinctrl to TX/RX only, freeing P1.06/P1.07 for READY/IRQ. A pristine Nordic rebuild
passes; the corrected image still requires HIL confirmation.

The same run showed an STM LD4 period slower than the intended 500 ms transition interval. The
application now explicitly selects calibrated HSI16 as SYSCLK before starting FreeRTOS instead of
depending on reset-default MSI state. The button edge remains logged but no longer toggles LD4, so
the heartbeat timing is unambiguous. A subsequent flash exposed the underlying factor-of-eight
error: defining `configSYSTICK_CLOCK_HZ` makes the Cortex-M0 FreeRTOS port select the external
AHB/8 SysTick source, but the configuration had supplied the full core frequency. Removing that
override selects the core-clock source and matches the reload calculation. The rebuilt STM image
requires HIL timing confirmation.

Holding Nordic in reset made the STM task operate normally, while the first valid HELLO caused a
complete apparent freeze. ELF-level review found the receive path nested roughly 920 bytes or more
of decoder frame, response frame, encoder scratch and call frames on a 1,024-byte task stack.
FreeRTOS then entered the configured stack-overflow hook, which disabled interrupts forever. The
static task stack is now 3 KiB. The overflow hook also deasserts READY and forces LD4 on so a future
overflow cannot masquerade as an online node. Longer term, protocol scratch storage should move to
an explicit caller-owned workspace to reduce peak stack use.

## Two-board HIL result

User-reported hardware result on 2026-10-12: `PASS` for initial discovery after flashing the
corrected images. Nordic observed READY asserted, completed HELLO, identity and capability
negotiation, and reported identity `stm32u083rc-node`, session `605646`, capabilities `0x00000007`,
and link state `online`; IRQ was deasserted. The report treats the supplied console text as
user-observed hardware evidence, not as a capture made by the automation environment.

Reset recovery and post-reset session behavior remain to be confirmed before final Phase 2 closure.
