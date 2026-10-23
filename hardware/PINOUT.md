# Current pinout and electrical gate

Software mapping is frozen for NUCLEO-U083RC `MB1932-U083RC-B01` and nRF54L15-DK
`PCA10156 1.0.0` (date code `2025.30`). On 2026-10-12 the user configured Nordic VDD:IO to
3.3 V and measured both Nordic VDD:IO and STM VDD at 3.3 V with the boards disconnected. The
electrical voltage gate is `PASS`. Phase 6 moved READY/IRQ from P1.06/P1.07 to P2.06/P2.08 to
restore debugger UART hardware flow control for bidirectional MCUmgr.

## Frozen application signals

| Signal | nRF54L15-DK | NUCLEO-U083RC | Direction | Safe reset state |
|---|---|---|---|---|
| `HOST_TX` / `NODE_RX` | P1.11, header P1 pin 14 | PA10, CN10 pin 33 | nRF to STM | inputs until UART init |
| `NODE_TX` / `HOST_RX` | P1.12, header P1 pin 15 | PA9, CN10 pin 21 | STM to nRF | inputs until UART init |
| `NODE_IRQ_N` | P2.08, header P2 pin 11 | PB4, CN10 pin 27 | STM to nRF | STM open-drain/high-Z; nRF pull-up |
| `NODE_READY` | P2.06, header P2 pin 9 | PB5, CN10 pin 29 | STM to nRF | STM drives low until ready; nRF pull-down |
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
- Nordic documents P1.11/P1.12 as exposed unused GPIOs. P1.06/P1.07 remain assigned to debugger
  UART RTS/CTS so bidirectional MCUmgr works over the J-Link VCOM. P2.06/P2.08 are exposed optional
  trace CLK/DATA0 pins and are used for READY/IRQ; trace cannot be enabled concurrently. P2.10
  remains the analyzer debug output.
- ST UM3256 Rev 2 and the MB1932-U083RC-B01 schematic define PA9/PA10 and PB3/PB4/PB5 on CN10.

References:

## Supervision extension (Phase 10 only)

| Signal | Nordic | STM | Driver |
|---|---|---|---|
| NRST | P1.08, physical P1 pin 11 | CN7 pin 14 | open-drain low or released high-Z |
| BOOT0 | P1.14, physical P1 pin 17 | CN7 pin 7 | low normally; high only for explicit Phase 11 ROM repair; input on Nordic reset |

Source check: PCA10156 1.0.0 schematic sheets 5/7 show P1.08 connected to SW5
(button2) directly to ground, so it is compatible with open-drain reset. P1.14
connects to the LED3 transistor gate, not directly to its LED supply; R27 is a
1 MOhm gate pull-down. The profile disables button2/led3 software consumers.
Button2 physically resets STM; LED3 stays off because Phase 10 never asserts BOOT0.
ST UM3256 Rev 2 table 11 confirms CN7 pins 14/7 and BOOT0 default low.

PCA10156 1.0.0 hardware files identify QFAAC00 (SoC revision 2). Nordic anomaly
100's P1.08/P1.14 analog-short issue is documented for revision 1; do not extrapolate
this wiring approval to engineering/revision-1 boards or modified assemblies.
Verify board/SoC marking if different from the recorded kit.

Documentation/source gate is checked; physical wiring and default levels still
require the human checklist in WIRING.md. No option bytes are changed. Normal
and Phase 9 profiles do not initialize these supervision outputs.

## Source links

- https://docs.nordicsemi.com/r/bundle/ug_nrf54l15_dk/page/ug/nrf54l15_dk/hw_desription/connector_if.html
- https://docs.nordicsemi.com/r/bundle/ug_nrf54l15_dk/page/ug/nrf54l15_dk/hw_desription/power_supply.html
- https://www.nordicsemi.com/Products/Development-hardware/nRF54L15-DK/Hardware-files
- https://nsscprodmedia.blob.core.windows.net/prod/software-and-other-downloads/dev-kits/nrf54l15-dk/pca10156-nrf54l15-dk-1_0_0.zip
- https://docs.nordicsemi.com/r/bundle/errata_nrf54l15_rev1/page/err/nrf54l15/rev1/latest/anomaly_l15_100.html
- https://www.st.com/resource/en/user_manual/um3256-stm32-nucleo64-boards-mb1932-stmicroelectronics.pdf
- https://www.st.com/resource/en/schematic_pack/mb1932-u083rc-b01-schematic.pdf
