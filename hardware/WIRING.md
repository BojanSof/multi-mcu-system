# Phase 2 wiring procedure

Do not begin until every voltage-gate item in `PINOUT.md` is recorded and passes.

With both USB cables disconnected, make these five inter-board connections:

| From | To | Signal |
|---|---|---|
| nRF P1 pin 14 (P1.11) | STM CN10 pin 33 (PA10) | host TX / node RX |
| STM CN10 pin 21 (PA9) | nRF P1 pin 15 (P1.12) | node TX / host RX |
| STM CN10 pin 27 (PB4) | nRF P2 pin 11 (P2.08) | active-low node IRQ |
| STM CN10 pin 29 (PB5) | nRF P2 pin 9 (P2.06) | node ready |
| STM CN10 pin 20 (GND) | nRF P30 pin 2 (GND) | common reference |

Do not wire the analyzer-only debug pins unless probing them. Do not connect RESET, BOOT0, VDD,
3.3 V or 5 V. Recheck every header label before applying USB power.

P1.06 and P1.07 are reserved for debugger UART RTS/CTS and must not be connected to the STM board.
P2.06/P2.08 are optional trace pins, so do not enable or attach the trace interface during this demo.

Power the Nordic board first, then the STM board. The reverse order was tested on hardware and can
leave the unpowered Nordic MCU partially back-powered through STM push-pull UART/READY signals,
preventing a clean Nordic boot. If the order is reversed accidentally, disconnect both USB cables
for several seconds and restart Nordic-first. Keep each debug VCP open separately. A correct startup
proceeds through READY, HELLO, identity and capability discovery. If discovery fails, remove USB
power before moving wires; never probe by shorting a signal to a supply rail.

## Phase 10 supervision addition

Only for the exact revisions/3.3 V gate recorded in PINOUT.md:

1. Disconnect both USB supplies. Keep the five existing wires unchanged.
2. Remove any STM BOOT0-to-VDD jumper (CN7 pins 5–7). Never connect that jumper
   while Nordic drives BOOT0 low.
3. Add Nordic physical P1 pin 11 (P1.08) to STM CN7 pin 14 (NRST).
4. Add Nordic physical P1 pin 17 (P1.14) to STM CN7 pin 7 (BOOT0).
5. Inspect header numbering. No VDD/3.3 V/5 V rail connections.
6. Use the phase10 Nordic profile. Power Nordic first, then STM. Before requesting
   reset/update, verify BOOT0 is low and released NRST is near STM VDD. If not,
   disconnect power and check wiring; do not proceed to flashing.

DK button2 now physically resets STM; its normal software input and LED3 output
are disabled. BOOT0 stays low in Phase 10. A command sinks NRST for 20 ms then
releases it; it never drives NRST high. Retain independent SWD and logging ports.
Do not run an older profile that repurposes these pins while supervision is wired.

## Phase 11 factory-ROM mode

No additional wires. The explicit `phase11` recovery command alone drives BOOT0
high, pulses open-drain NRST and changes application UART to 115200 8E1. LED3
indicates BOOT0 high. On finish/abort/error it restores BOOT0 low, 8N1 and reset;
if restoration fails, runtime ownership remains blocked pending explicit recovery.
Before enabling, manually verify NBOOT_SEL=0, nBOOT1=1, BOOT_LOCK=0 and unprotected
bootloader read/program access. Do not change option bytes or protection in software.
See ../docs/PHASE_11_TESTING.md for the read-only check and operator assertion flag.
Keep other STM UART interfaces idle during ROM selection; never type into its log
terminal or run CubeProgrammer concurrently with a ROM transfer. Nordic SMP VCOM
remains independent, 8N1 with hardware flow control.
