# Two-board wiring procedure

Use only the exact board revisions and voltage gate in [PINOUT.md](PINOUT.md).
Disconnect both USB supplies before changing any jumper or signal wire. Nordic
VDD:IO must be configured and measured compatible with STM32's 3.3 V signals.

## Connections for the complete operator system

| From | To | Signal |
|---|---|---|
| nRF P1 pin 14 (P1.11) | STM CN10 pin 33 (PA10) | Host TX / node RX |
| STM CN10 pin 21 (PA9) | nRF P1 pin 15 (P1.12) | Node TX / host RX |
| STM CN10 pin 27 (PB4) | nRF P2 pin 11 (P2.08) | Active-low IRQ |
| STM CN10 pin 29 (PB5) | nRF P2 pin 9 (P2.06) | READY |
| STM CN10 pin 20 (GND) | nRF P30 pin 2 (GND) | Common reference |
| nRF P1 pin 11 (P1.08) | STM CN7 pin 14 (NRST) | Open-drain reset |
| nRF P1 pin 17 (P1.14) | STM CN7 pin 7 (BOOT0) | Factory-ROM selection |

Remove any STM BOOT0-to-VDD jumper (CN7 pins 5–7) before Nordic drives BOOT0.
Never connect VDD, 3.3 V or 5 V rails between boards. RESET sinks low or releases
high-Z; it must not be driven high. BOOT0 is low normally and high only for an
explicit ROM operation. Keep SWD and independent logging connections accessible.
Before reset/update, verify BOOT0 low and released NRST near STM VDD.

P1.06/P1.07 belong to debugger UART RTS/CTS and must not be wired to STM32.
P2.06/P2.08 are also optional trace pins; do not enable trace while using them.
The P2.10 host and PB3 node debug pins are analyzer outputs, not interconnects.

On this exact Nordic revision, button2 physically pulls STM reset and LED3 reflects
BOOT0 selection. Their conflicting software consumers are disabled by the operator
build. Do not select a build that repurposes these pins with this wiring attached.

## Power and startup

Power **Nordic first, then STM32**, each through its own USB connection. Reverse
order can partially back-power unpowered Nordic through STM UART/READY outputs and
prevent a clean boot. If that happens, disconnect both supplies for several seconds
and restart in the documented order. This wiring does not provide arbitrary
power-order isolation.

Use the `reliability` firmware. Startup should reach READY, discovery and `online`.
Nordic logs use RTT; Nordic VCOM is SMP-only. STM ST-LINK VCOM remains a separate
log channel. If discovery fails, remove power before moving wires; never short a
signal to a supply as a probe.

Port 2 has no GPIO interrupt support in the pinned SDK. `gpio_interrupts:false`
is expected: UART RX wake and 10 ms polling operate independently of a missed IRQ
edge. IRQ_N can still be read as a level while events await acknowledgment.

## Factory-ROM prerequisites

No extra wires or BOOT0 jumper are needed for host-driven ROM repair. Before using
it, manually verify `NBOOT_SEL=0`, `nBOOT1=1`, `BOOT_LOCK=0`, usable SWD rescue and
unprotected bootloader read/program access. Wiring does not change option bytes.
See [firmware recovery](../docs/FIRMWARE_UPDATES.md#explicit-factory-rom-repair)
for the read-only option check and explicit operator assertion.

ROM mode uses USART1 115200 8E1. Finish/abort/error restores BOOT0 low, 8N1 and
reset; restoration failure retains exclusive UART ownership. Keep other STM UART
interfaces idle during selection and never run CubeProgrammer concurrently with
host ROM transfer. The Nordic SMP VCOM is independent and retains its settings.
