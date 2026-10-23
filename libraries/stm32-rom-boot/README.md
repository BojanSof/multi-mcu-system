# STM32 ROM bootloader transport

Independent C11 AN3155 host transactions for STM32U083RC (`0x489`), deliberately
restricted to repairing its 64 KiB custom bootloader region. No SDK, GPIO, heap,
option-byte, protection, GO or mass-erase commands. Native and Zephyr builds share
`cmake/Sources.cmake`.

```sh
cmake -S libraries/stm32-rom-boot -B build/stm32-rom-native -DBUILD_TESTING=ON
cmake --build build/stm32-rom-native
ctest --test-dir build/stm32-rom-native --output-on-failure
```

Target: `Stm32RomBoot::Stm32RomBoot`. Adapter callbacks return zero only after
transmitting or receiving the **entire** requested buffer; negative errno on error.
The read callback must enforce the supplied whole-read timeout, including fragment
handling. The adapter owns external reset, BOOT0, 115200 8E1 setup/restoration and
UART flushing. Call Connect only following fresh ROM entry; sync is not replayed
after an ambiguous ACK loss. Connect requires GET_ID, READ, WRITE and EXTENDED_ERASE
capabilities and the exact device ID before enabling erase.

Erase sends an explicit 32-page list (0–31). Writes require a successful erase,
offset aligned to eight bytes, size 1–256, and bounds within `[0x08000000,0x08010000)`.
Final short writes pad with FF to eight bytes and read back all programmed bytes,
including padding. The caller enforces stream continuity, final-chunk-only padding,
complete image validation/hash and duplicate-admission handling. No transaction is
blindly retried; after a transport/protocol/readback failure the context is poisoned
and cannot write again without fresh reset, connection and erase. Invalid arguments
are rejected before wire access and do not poison an otherwise connected context.

Normal response timeout is 1 second per exchange; explicit-page erase completion
has a 10-second limit. Functions are blocking but bounded. Do not call while holding
a mutex needed by the management/status transport. Read-only identification is not
proof of safe electrical connections or boot option configuration.

Return codes: `-EINVAL` argument/alignment/length, `-ERANGE` out of bootloader bounds,
`-ENOTCONN` poisoned/disconnected, `-EPERM` write before erase, `-ENOTSUP` missing
capability, `-ENODEV` wrong ID, `-EACCES` ROM NACK, `-EPROTO` malformed ACK/ID length,
`-EBADMSG` readback mismatch, or callback error such as `-ETIMEDOUT`/`-EIO`.

Protocol reference: [ST AN3155 Rev 22, sections 3.1/3.3/3.4/3.6/3.8](https://www.st.com/resource/en/application_note/an3155-usart-protocol-used-in-the-stm32-bootloader-stmicroelectronics.pdf).
STM32U0 eight-byte flash alignment is specified in [AN2606 Rev 70, table 7](https://www.st.com/resource/en/application_note/an2606-stm32-microcontroller-system-memory-boot-mode-stmicroelectronics.pdf).
Target boot-option/electrical prerequisites belong to the hardware adapter
and operator checklist. This is an unauthenticated factory-ROM rescue path.
