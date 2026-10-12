# Logic-analyzer connections for the Phase 2 link

A logic analyzer is optional. Connect its ground to the common board ground and use:

| Channel | Probe point | Decode |
|---|---|---|
| CH0 | nRF P1 pin 14 / STM PA10 | UART 115200 8N1, host to node |
| CH1 | STM CN10 pin 21 / nRF P1.12 | UART 115200 8N1, node to host |
| CH2 | STM CN10 pin 27 (PB4) | active-low IRQ level |

Alternative CH2 during timing work is STM CN10 pin 31 (PB3 `NODE_DEBUG`). It is a firmware event
marker, not a voltage or power-safety measurement. Use the `0x00` frame delimiters to locate COBS
packets. CRC-valid decoded traffic is authoritative; UART bytes alone do not prove application
acceptance.

Never label an illustrative or simulated trace as a hardware capture. Record analyzer model, sample
rate, threshold, firmware Git revision and whether the trace is real before storing any capture.
