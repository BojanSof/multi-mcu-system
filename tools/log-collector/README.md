# Dual log collector

Collect two independent debug streams for a bounded period:

```sh
python tools/log-collector/log_collector.py --host serial:/dev/ttyACM0 \
  --node serial:/dev/ttyACM1 --duration 30 --output capture.jsonl
python tools/log-collector/log_collector.py --host file:host.log --node file:node.log \
  --format csv --output capture.csv
```

Serial mode uses the Python standard library on POSIX and defaults to 115200 8N1. Every record
retains the original bytes as Base64 and a display form in `raw_line`. Collector receipt time is
only a correlation aid: output explicitly declares that the independent MCU clocks are not
synchronized.
