# Dual log collector

The collector preserves original bytes (Base64 plus display text) and host receipt
timestamps. Independent MCU clocks are not synchronized; receipt order is only a
correlation aid.

Current Nordic operator firmware logs through RTT; its J-Link VCOM is reserved for
SMP. Export RTT with your debugger and use that file alongside STM ST-LINK serial
logs, or collect two existing log files. Never share a serial port with `mmctl`.
From the repository root:

```sh
mkdir -p build/logs
.venv/bin/python tools/log-collector/log_collector.py \
  --host file:build/logs/host-rtt.log --node serial:/dev/serial/by-id/YOUR_STLINK_VCOM \
  --duration 30 --output build/logs/capture.jsonl
.venv/bin/python tools/log-collector/log_collector.py \
  --host file:build/logs/host-rtt.log --node file:build/logs/node.log \
  --format csv --output build/logs/capture.csv
```

File inputs must already exist. POSIX serial mode defaults to 115200 8N1. Identify
the correct ST-LINK port and close its other readers. Review captures before
publication; leave private captures under ignored directories.
