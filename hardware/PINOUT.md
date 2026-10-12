# Phase 2 pinout and electrical gate

Software mapping is frozen for NUCLEO-U083RC `MB1932-U083RC-B01` and nRF54L15-DK
`PCA10156 1.0.0` (date code `2025.30`). On 2026-10-12 the user configured Nordic VDD:IO to
3.3 V and measured both Nordic VDD:IO and STM VDD at 3.3 V with the boards disconnected. The
Phase 2 electrical voltage gate is `PASS`.

## Frozen application signals

| Signal | nRF54L15-DK | NUCLEO-U083RC | Direction | Safe reset state |
|---|---|---|---|---|
| `HOST_TX` / `NODE_RX` | P1.11, header P1 pin 14 | PA10, CN10 pin 33 | nRF to STM | inputs until UART init |
| `NODE_TX` / `HOST_RX` | P1.12, header P1 pin 15 | PA9, CN10 pin 21 | STM to nRF | inputs until UART init |
| `NODE_IRQ_N` | P1.07, header P1 pin 10 | PB4, CN10 pin 27 | STM to nRF | STM open-drain/high-Z; nRF pull-up |
| `NODE_READY` | P1.06, P1 header position labeled `P1.06` | PB5, CN10 pin 29 | STM to nRF | STM drives low until ready; nRF pull-down |
| `NODE_DEBUG` | not connected | PB3, CN10 pin 31 | analyzer only | low |
| `HOST_DEBUG` | P2.10, header P2 pin 13 | not connected | analyzer only | low |
| `GND` | P30 pin 2 | CN10 pin 20 | common reference | connected only after voltage gate |

USART application framing is 115200 baud, 8 data bits, no parity, one stop bit. STM USART2
PA2/PA3 remains dedicated to ST-LINK VCP logging. The Nordic Zephyr console remains on the DK debug
transport; no ASCII log bytes use the application UART.

## Mandatory voltage gate

1. Record the exact nRF54L15-DK PCA10156 board revision from its sticker.
2. With no inter-board wires attached, use nRF Connect for Desktop **Board Configurator** to set
   `nRF:VDD` to **3.3 V**. Nordic documents a 1.8 V default and a programmable 1.8–3.3 V range.
3. USB-power both boards independently. Measure Nordic header P1 pin 1 (`VDD:IO`) relative to P30
   pin 2 (`GND`) and record the value. Measure STM VDD relative to CN10 pin 20.
4. Continue only when both measurements are compatible near 3.3 V. A 3.3 V STM output must never
   be connected while the Nordic SoC is at 1.8 V; the SoC limit is VDD + 0.3 V.
5. Power both boards off, connect only the GND and application signals listed above, inspect for
   shifted header positions/shorts, then reconnect their separate USB supplies.

Never connect either 5 V or 3.3 V supply rail between boards. Do not connect RESET or BOOT0 in
Phase 2. Do not change STM option bytes.

## Source verification

- Nordic GPIO/header functions and physical connector numbering were checked against the official
  nRF54L15-DK hardware guide and PCA10156 1.0.0 schematic package.
- Nordic documents P1.11/P1.12 as exposed unused GPIOs. P1.06/P1.07 are the debugger UART's
  optional RTS/CTS pins and are explicitly available for other purposes when hardware flow control
  is disabled. P2.10 remains an optional trace-header debug output only.
- ST UM3256 Rev 2 and the MB1932-U083RC-B01 schematic define PA9/PA10 and PB3/PB4/PB5 on CN10.

References:

- https://docs.nordicsemi.com/r/bundle/ug_nrf54l15_dk/page/ug/nrf54l15_dk/hw_desription/connector_if.html
- https://docs.nordicsemi.com/r/bundle/ug_nrf54l15_dk/page/ug/nrf54l15_dk/hw_desription/power_supply.html
- https://www.nordicsemi.com/Products/Development-hardware/nRF54L15-DK/Hardware-files
- https://www.st.com/resource/en/user_manual/um3256-stm32-nucleo64-boards-mb1932-stmicroelectronics.pdf
- https://www.st.com/resource/en/schematic_pack/mb1932-u083rc-b01-schematic.pdf
