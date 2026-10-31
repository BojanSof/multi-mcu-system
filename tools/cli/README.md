# Host CLI

Run `scripts/mmctl` from the project root. The full operator interface uses the
Nordic `reliability` build and its J-Link VCOM exclusively; logs use RTT.

See the [operator reference](../../docs/CLI.md) for service, event, lifecycle,
recovery and metrics commands, [firmware management](../../docs/FIRMWARE_UPDATES.md)
for signed updates and ROM repair, and [performance notes](../../docs/CLI_PERFORMANCE.md)
for bounded serial reads/polling. Use `scripts/mmctl --help` for current syntax.

```sh
scripts/mmctl device list
scripts/mmctl --port "$NRF_PORT" status
scripts/mmctl --port "$NRF_PORT" sensor sample
scripts/mmctl --port "$NRF_PORT" events watch
```

Exact retries keep request and boot identities. Mutations are never blindly
replayed across host restart. Offline image inspection uses the supplied trusted
public key; private keys are never sent to either MCU.
