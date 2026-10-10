# Phase 1 hardware and electrical gate

No inter-board signal pin is assigned or approved in Phase 1. The firmware uses only each board's
devicetree/BSP LED and button aliases and its independent debug console.

## Mandatory gate before Phase 2 wiring

- Record the exact nRF54L15-DK and NUCLEO-U083RC PCB revisions and inspect their current schematics.
- Use Nordic Board Configurator and a meter to verify the DK I/O domain. Its default may be 1.8 V;
  do not connect it to a presumed 3.3 V STM32 signal.
- Verify physical header numbers, GPIO alternate functions, solder bridges, debugger/VCOM routing,
  pulls, and powered-off behavior before adding them to this file.
- USB-power the boards independently. Never join 5 V or 3.3 V rails.
- Connect no inter-board wire until the voltage domains are compatible. When approved, connect only
  common GND and verified application signals.
- Remote reset must be open-drain pull-low/high-Z release. BOOT0 must remain at its safe default.
- Do not write option bytes, enable protection, or flash either board as part of this gate.

Status: `BLOCKED_ELECTRICAL` for all inter-board wiring pending physical verification.
