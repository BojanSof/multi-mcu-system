# Host CLI

Run from the project root after building/flashing the `phase9` Nordic profile and
installing `tools/requirements.lock` into `.venv`. Close the terminal log viewer
before opening the CLI. Phase 9 sends logs over RTT and reserves J-Link VCOM for SMP.

```sh
./scripts/mmctl device list
./scripts/mmctl --port /dev/serial/by-id/YOUR_JLINK status
./scripts/mmctl services list
./scripts/mmctl sensor sample
./scripts/mmctl actuator start --duration-ms 2000
./scripts/mmctl events watch
./scripts/mmctl node versions
./scripts/mmctl diagnostics records
./scripts/mmctl node maintenance enter --yes
./scripts/mmctl node maintenance exit --yes
```

If there is exactly one J-Link VCOM it is selected automatically. Otherwise pass
`--port`; stable `/dev/serial/by-id/` paths are preferred. Common options work
before or after subcommands. Add `--json` for one JSON object per line, `--logs`
for host log lines on stderr, or `--timeout` / `--wait-timeout` to adjust bounded
transport / operation-result waits.

Service commands print admission and then the asynchronous response. Use
`events watch` to observe actuator progress and completion. `diagnostics records`
drains up to 32 retained node records and then checks that the queue is empty.
`diagnostics clear --yes` disables injections; it does not erase history.

```sh
./scripts/mmctl diagnostics inject 3 --yes
./scripts/mmctl diagnostics clear --yes
./scripts/mmctl service call 0x1001 1 --data '' --yes
./scripts/mmctl host image list
./scripts/mmctl host image update path/to/zephyr.signed.bin --yes
```

Nordic image operations use a signed MCUboot `.bin`, not `dfu_application.zip`.
`update` checks the image SHA-256/SHA-512 TLV and confirms that its hash differs from the
primary image. It uploads, checks that the secondary slot matches, marks it for
trial, resets once, and reconnects using read-only image-list requests. It reports
the active image and confirmation flags. The Phase 9 firmware automatically
confirms after its node-discovery self-test; query `host image list` again if
confirmation has not completed yet. `host image confirm --yes` is an explicit
diagnostic bypass of the normal self-test policy.

An interrupted service call is never reconnected/replayed automatically. Retried
requests retain the same request ID and expected host boot ID; a Nordic restart
invalidates pending operations. Physical STM reset and STM update/recovery remain
unavailable until Phases 10/11.
