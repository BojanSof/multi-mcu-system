# Optional two-board HIL observer

This harness only observes existing 115200-baud serial logs. It never builds, flashes, resets, or
changes option bytes. Hardware execution is opt-in and requires two explicit, distinct device paths
plus confirmation that the wiring and Nordic 3.3 V VDD:IO were checked.

From the repository root, a safe no-op status check is:

```sh
python tests/hil/run_hil.py --scenario phase5_smoke
```

A deterministic simulated check (not a hardware capture) is:

```sh
python tests/hil/run_hil.py --scenario phase5_smoke \
  --fixture-host tests/hil/fixtures/phase5_host.log \
  --fixture-node tests/hil/fixtures/phase5_node.log
```

After independently building, flashing, wiring and starting both boards:

```sh
python tests/hil/run_hil.py --scenario phase5_smoke --run-hardware \
  --electrical-verified --host-port /dev/ttyACM2 --node-port /dev/ttyACM0
```

Device names are examples; identify them by stable `/dev/serial/by-id/` paths when possible. The
single JSON result uses the project statuses and identifies execution as hardware, simulated, or
not run. Raw logs are intentionally not persisted by this runner.
