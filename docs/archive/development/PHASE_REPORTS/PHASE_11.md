# Phase 11 — Factory-ROM repair and signed recovery

Date: 2026-10-23. Base Git `37ae58c` plus uncommitted Phase 11 changes.
Status: software implemented and verified; physical acceptance pending.
User hardware smoke test subsequently PASS: ROM device1161 (0x489), version49
(0x31), offset=total49664, statecomplete, error0, activefalse. Runtime returned
online with no timeouts; lifecycle version query returned firmware0.6.0 and
wire/management1.0, and the user confirmed sensor sample works. This demonstrates
repair of a working loader with the existing application preserved. Intentional
interruption, corrupt-loader and empty-app signed chaining remain NOT_RUN_HARDWARE.
Agent-run ROM programming and pin measurements remain NOT_RUN_HARDWARE; user-run
normal repair/runtime smoke is confirmed above. No board was flashed or option byte
changed by the agent.

## Delivered

- Frozen interfaces/ownership in `PHASE_11_PLAN.md`; parallel portable ROM,
  Nordic integration and Python release/CLI agents; independent safety review.
- Independent C11 `Stm32RomBoot::Stm32RomBoot` package, native/Zephyr shared source
  inventory. AN3155 sync/GET/GET_ID/read/write/extended erase, exact ID 0x489 and
  required capabilities before erase. Single fresh-reset sync, bounded exchanges;
  no blind retries of ambiguous writes or protection commands.
- Erase only explicit pages0..31; read/write bounded to the existing 64 KiB loader.
  Eight-byte alignment, FF final padding and full padded readback; no mass erase,
  GO, app/metadata writes, heap or target SDK in the portable library.
- Independent Nordic `phase11` profile preserves older profiles. Command6 mailbox,
  replay/boot/job guards, exclusive runtime/update/repair ownership, 8E1 ROM entry,
  streaming SHA256, ACK+readback offsets, inactivity abort and safe restoration.
- Restoration failure latches failed/active and retains UART ownership; only
  explicit abort retries restoration. No reset storm or runtime on uncertain parity.
  SMP snapshot lock is released during bounded ROM transactions.
- Offline v2 release validation and `stm recover`, `recover-status`, `recover-abort`.
  Full bundle validation before serial: paths/schema/hashes, loader bounds/vectors,
  both slot signatures/key/version/link/compatibility and Nordic signed-image structure.
  The original v1 manifest tool remains selectable and unchanged.
- Restore healthy applications unchanged; offline runtime chains signed Slot0.
  A definite protectedSlot0 refusal alone permits a fresh signed Slot1 job after
  same-boot/job checks and ownership release. No metadata erasure or guard bypass.
  Post-repair success requires a fresh nonzero STM session, not cached online state.
- Root build wrapper, actual link checks, native emulator/CBOR/CLI tests, optional
  embedded CI profile, root-invoked wiring/build/flash/test documentation.

## Environment/audit

Arch Linux x86_64; native GCC16.2.1; Arm GNU15.2.Rel1; CMake4.3.1; Ninja1.13.2;
Python3.14.8/project .venv; west1.5.0; NCS3.4.1/Zephyr4.4.2;
Zephyr SDK1.0.1; CubeProgrammer2.23.0; nrfutil8.2.0; J-Link9.24a.
All required build dependencies present. Optional cppcheck missing (manual install
`pacman -S --needed cppcheck`). No ttyACM/ttyUSB; libusb enumeration returns -99.
USB/physical gates are not inferred from successful software tests.

## Commands/results

Commands run from project root with `source ./.env.local`:

```sh
python3 scripts/doctor.py
scripts/test/run_native.sh
scripts/build/build_stm32.sh --public-key-header keys/stm32-dev-public.h
scripts/build/build_nordic.sh --profile phase11
cmake --build build/nrf54l15-host-phase11
scripts/build/build_nordic.sh --profile phase10
cmake --build build/nrf54l15-host-phase10
scripts/build/build_nordic.sh --profile phase9
scripts/build/build_nordic.sh --profile default
cmake -S libraries/stm32-rom-boot -B build/stm32-rom-native -DBUILD_TESTING=ON
cmake --build build/stm32-rom-native
ctest --test-dir build/stm32-rom-native --output-on-failure
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/stm32-rom-sanitized --output-on-failure
git diff --check
```

Sanitizer configuration used `-fsanitize=address,undefined -fno-omit-frame-pointer`;
leak detection disabled because the sandbox's ptrace prevents LeakSanitizer. No heap
allocation exists in the ROM library. Added tests verify range constants match
canonical `flash_layout.yaml` (no layout/link-address changes).

| Check | Result |
|---|---|
| Root native libraries/host/ROM emulator/real-SDK CBOR adapters | PASS: 12 CTests |
| STM boot journal/engine/real crypto | PASS: 2 CTests |
| Python tools/CLI/bundle/wrapper/HIL-runner tests | PASS: 160 tests |
| Dependency consistency (`pip check`) | PASS |
| Standalone ROM package and ASan/UBSan emulator | PASS |
| STM normal/Slot0/Slot1/bootloader builds | PASS |
| Nordic Phase11 initial clean + final incremental link | PASS |
| Nordic Phase10 and Phase9 regression builds | PASS |
| Nordic default regression | PASS: 50264 flash / 12992 RAM bytes |
| Actual reset/ROM erase/write/readback/repair/chained install | NOT_RUN_HARDWARE |

ROM emulator includes full64KiB repair with application/metadata sentinels preserved,
corrupted transmitted checksums, malformed ACK/NACK/count/ID/capabilities, timeouts
and send failure at every exchange, odd final chunk, readback mismatch, out-of-range
addresses and poisoned context after uncertainty. Adapter tests cover arbitration,
replay/boot guard, hash mismatch, abort/inactivity, restoration latch/no reset flood,
and rescue while normal discovery is outstanding. CLI tests cover offline rejection,
uncertain outcomes, guarded inactive-slot fallback and stale session rejection.

An additional offline smoke created and verified a v2 bundle from the actual SDK
`zephyr.signed.bin`, built raw STM loader and existing signed Phase10 Slot0/Slot1
packages with system version0.10.0, using only the existing public PEM. Output
`verified:true`. Artifacts stayed in an ignored unique `build/phase11-validation.*`
directory; no private key was read or generated.

Integration caught an unused handler warning (guarded for Phase11) and test-wrapper
string assembly error (fixed). Final suites, rather than intermediate failures,
are the acceptance evidence. Existing SDK NRF_PLATFORM_LUMOS deprecation and STM
newlib syscall-stub/RWX loader warnings remain; no new warnings are accepted.

## Memory/stack

STM sizes unchanged (text/data/bss bytes): normal23280/104/9440,
Slot0 and Slot1 each23456/104/9440, loader49544/116/2104. Loader fits64KiB,
signed slot packages fit88KiB, RAM within40KiB.

Nordic final Phase11 app: **90804 flash / 38776 RAM bytes**, within696176-byte app
partition and256KiB RAM. MCUboot62412/27984, within68KiB loader partition.
Current Phase10 regression84820/38240: ROM profile adds5984 flash and536 RAM bytes.

Main/workqueue stacks4096 each; ISR2048. Compiler individual frames include
HostUpdateAdapter_Process632, ROM SMP write808, regular update SMP write936 and
ROM read-exact24 bytes. These are individual static frames, not whole-call bounds.
Runtime stack high-water, throughput and physical verification timing remain
NOT_RUN_HARDWARE; measured optimization is Phase12.

## Hardware/source/security gates

AN3155 Rev22 sections3.1/3.3/3.4/3.6/3.8; AN2606 Rev70 section91/table206 confirms
USART1 PA10 RX/PA9 TX and8E1; table7 requires STM32U0 eight-byte programming alignment.
References: portable package README and `PHASE_11_TESTING.md`.
Use existing verified Phase10 reset/BOOT0 wires; no new wiring/power rails.
Manual prerequisite check: NBOOT_SEL0, nBOOT1=1, BOOT_LOCK0, unprotected loader
read/program access and working SWD. Software never changes these settings.

Manifest hashes and key_id do not authenticate the raw loader or its embedded key.
Operator must trust the bundle and matching-key build. Nordic artifact validation
checks signature TLV structure/digest, not Nordic signature authenticity against
a PC-provisioned key; device MCUboot remains authoritative. ROM repair deliberately
permits replacement of the trust root. No production secure-boot/anti-rollback claim.

HIL checklist/commands: [PHASE_11_TESTING.md](../PHASE_11_TESTING.md). First repair a
working loader, then explicitly choose bounded interruption/corrupt-loader/empty-app
tests with saved recovery artifacts. Do not call emulator writes real captures or
declare physical restoration/slot confirmation proven by compilation.

## Final collection

Final canonical suite PASS: 12 root CTests, 2 bootloader CTests and160 Python tests;
dependency consistency and formatting/whitespace PASS. Default/Phase9/Phase10/Phase11
Nordic builds and all four STM builds PASS. Actual ELF symbols confirm ROM Connect,
EraseBootloader, WriteVerified and SMP handler are linked; image cache enables
MULTI_MCU_PHASE11_ROM. No hardware writes or production security claims.
