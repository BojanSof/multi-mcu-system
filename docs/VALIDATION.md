# Validation status

Software tests, user-reported board tests and unperformed fault exercises are tracked
separately. A successful build or simulated peer is not a hardware result.

## Software baseline

The working firmware baseline is Git revision `dc9a9b3`. Its available verification
passed 26 native root CTests, 2 separate bootloader CTests and331 Python tests. Nordic
reliability, operator and standalone builds, both STM signed slots, debug/failing-trial
variants and the STM bootloader were built successfully. Runtime stack measurements
are observations, not guaranteed call-chain bounds.

A publication check on 2026-10-31 cloned that revision into a fresh temporary checkout:

| Environment | Result |
|---|---|
| No SDK/submodules/STM dependency trees, existing Python venv explicitly reused | PASS: 20 root CTests, 2 bootloader CTests, 331 Python tests |
| Same checkout with NCS sources supplied through `HOST_CONTROL_NCS_ROOT` | PASS: 26 root CTests, 2 bootloader CTests, 331 Python tests |
| Python dependency consistency | PASS |

The six extra native tests exercise real SDK CBOR/management integration. Without
SDK sources, they are unavailable rather than hardware-tested. No new package
installation or network fetch was part of this reproduction check. Run
`scripts/test/run_native.sh` from the project root after [setup](SETUP.md).

## Hardware evidence

These results were supplied by the board operator; the development environment did
not itself flash or measure boards during documentation cleanup.

| Check | Evidence / result |
|---|---|
| Host startup and normal discovery | PASS: nonzero host boot ID, online/healthy, watchdog/update/recovery capabilities enabled |
| Services and reliable events | PASS: sensor responses, actuator progress/completion, onboard button events; no observed protocol/UART errors or reliable-event gaps in the supplied short run |
| Warm-reset fault persistence | PASS: journal generations 1/2 remained; generation 3 recorded the next boot/session; storage_errors 0 |
| Extended runtime acceptance | PASS, user reported: 100 samples and 600-second watch; latest detailed JSON was not added to this repository |
| Signed STM updates | PASS, user reported normal inactive-slot update/confirmation; protected target correctly rejected |
| Factory-ROM repair and runtime restoration | PASS, user reported repair completion, version and sensor checks |
| Nordic image-management normal flow | PASS, user reported upload/test/boot checks |

One supplied short reliability run observed approximately 85.93ms median PC request
latency, 86.55ms maximum, 2268B main-stack free and 3864B workqueue-stack free. These
values include software scheduling/transport behavior and do not prove worst-case
latency or stack safety. Host `dropped` counts overwritten retained history records;
it is not automatically unread event loss or corrupted UART data.

Port2 GPIO interrupts are unavailable in the pinned SDK; gpio_interrupts:false is
expected. UART wake plus 10 ms polling is the validated startup/delivery path. No real
logic-analyzer capture is included for this implementation.

## Remaining physical fault acceptance

The following are still **NOT_RUN_HARDWARE** for the current reliability configuration:

- Deliberate expiry/reset of both watchdogs and automatic-recovery budget/latch timing.
- Prolonged recovery exclusion in maintenance/shutdown/update modes.
- Fault-journal retention over a controlled power cycle and physical flash-write interruption.
- Deliberate lost ACK, retry exhaustion and legacy reconnect with observable wire evidence.
- Update interruption at commit boundaries, failing signed-trial rollback and damaged metadata.

Use [TESTING.md](TESTING.md) for bounded procedures, explicit destructive-test gates
and rescue requirements. Keep real captures/logs labeled with board revision, source
revision and tool versions. Never turn missing manual evidence into PASS.
