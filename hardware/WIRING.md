# Phase 2 wiring procedure

Do not begin until every voltage-gate item in `PINOUT.md` is recorded and passes.

With both USB cables disconnected, make these five inter-board connections:

| From | To | Signal |
|---|---|---|
| nRF P1 pin 14 (P1.11) | STM CN10 pin 33 (PA10) | host TX / node RX |
| STM CN10 pin 21 (PA9) | nRF P1 pin 15 (P1.12) | node TX / host RX |
| STM CN10 pin 27 (PB4) | nRF P1 pin 10 (P1.07) | active-low node IRQ |
| STM CN10 pin 29 (PB5) | nRF P1 header position labeled P1.06 | node ready |
| STM CN10 pin 20 (GND) | nRF P30 pin 2 (GND) | common reference |

Do not wire the analyzer-only debug pins unless probing them. Do not connect RESET, BOOT0, VDD,
3.3 V or 5 V. Recheck every header label before applying USB power.

Power the STM board first, then the Nordic board. Keep each debug VCP open separately. A correct
startup proceeds through READY, HELLO, identity and capability discovery. If discovery fails, remove
USB power before moving wires; never probe by shorting a signal to a supply rail.
