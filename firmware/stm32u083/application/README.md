# NUCLEO-U083RC application

STM32 HAL and FreeRTOS application with fixed task storage, a local watchdog,
separate logging, service registry, virtual sensor/actuator and onboard button events.
All commands run from the repository root after [setup](../../../docs/SETUP.md).

## Recommended signed-slot build

```sh
source ./.env.local
scripts/build/build_stm32.sh --profile reliability
```

This builds personality B for both execution addresses, with persistent fault
history and negotiated reliable events. Outputs are under
`firmware/stm32u083/application/build/release-reliability-slot0/` and
`firmware/stm32u083/application/build/release-reliability-slot1/`. Each configure
exports `compile_commands.json`. See [installation/signing](../../../docs/FIRMWARE_UPDATES.md)
for the required loader and matching key; never flash an unsigned slot ELF as if
it were a complete signed package.

## Standalone and development variants

`debug`/`debug-personality-b` presets are standalone bring-up images linked at
`0x08000000`; programming one overwrites the custom bootloader. Use them only for
an intentional standalone setup, not an existing signed-A/B installation.
Personality A has GPIO events and the virtual sensor; B adds the actuator.

For development faults on the signed installation, use `debug-reliability-slot0`
or `debug-reliability-slot1`, sign for the inactive slot, then update normally.
CMake rejects enabled development faults in Release. Runtime injections clear at
reset. Deliberate failing-trial presets are described in [testing](../../../docs/TESTING.md).

## Runtime behavior

The lifecycle progresses INIT/READY/ONLINE. Maintenance and software shutdown keep
management available while declining new virtual-I/O work; no physical power is
switched. READY briefly pulses during deliberate transitions to force rediscovery.
LD4 is a firmware heartbeat; the onboard button publishes an event and does not
change that LED. USART2 PA2/PA3 carries ST-LINK VCOM logs, independently of USART1.

The IWDG is nominally 4.096 seconds at 32 kHz LSI and is refreshed only when the
application progress counter advances. Development stall injection lasts six
seconds and should reset the node. The normalized reset cause and warm-reset boot
count are exposed by lifecycle status. `.noinit` boot count is volatile across
power loss; the optional dedicated flash fault journal is persistent storage.
See [pinout](../../../hardware/PINOUT.md), [fault exercises](../../../docs/FAULT_INJECTION.md)
and [validation](../../../docs/VALIDATION.md).
