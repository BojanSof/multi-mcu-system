# Compatibility and lifecycle policy

Firmware, transport and services are versioned independently. Discovering an
incompatible optional service does not disable compatible services or the link.

## Version rules

- Unknown application wire major: reject before state mutation.
- Management major mismatch during HELLO: discovery does not become online.
- Service compatibility: equal major and `offered_minor >= required_minor`.
- Unknown optional capabilities are ignored unless an operation requires them.
- Unsupported commands receive an explicit error without invalidating other services.

The host requires version 1.0 of known lifecycle, diagnostics and virtual-I/O
services. Reliability nodes offer lifecycle 1.1 with fault-history/event-status
commands. Firmware versions and build identifiers are informational; update
eligibility additionally checks target, hardware, slot/link address, size, key,
algorithm and protocol requirements. There is no semantic-version anti-rollback
policy: an otherwise valid signed older image is not rejected solely for age.

## Supported event combinations

| Host | Node | Event behavior | Persistent node faults |
|---|---|---|---|
| Original event client | Original node | Legacy unsolicited frames | Unavailable |
| Original event client | Reliability node | Legacy frames after HELLO/LIST | Available through lifecycle commands |
| Reliability host | Original node | Legacy frames; no extension sent | Unavailable |
| Reliability host | Reliability node | Numbered events, ACK and level-held IRQ | Lifecycle 1.1 journal |

Both virtual-I/O personalities use the same negotiation. Sensor-only nodes reject
actuator operations while keeping sensor and management services available.

## Lifecycle, health and sessions

Normal node progression is `INIT -> READY -> ONLINE`. `MAINTENANCE` and
software-only `SHUTDOWN` keep lifecycle, diagnostic and heartbeat access available
while restricting virtual-I/O work. Neither mode controls physical power.

Host health (`healthy`, `degraded`, `recovering`, `failed`) is separate from node
lifecycle mode. A node warm reboot changes its session. READY loss/re-rise also
forces rediscovery when volatile session history may restart after power loss.
Pending work is failed or canceled, never automatically replayed into a new
session. A host reboot likewise creates a new boot identity and loses its volatile
request history. The node may finish already accepted finite local work.

Sequence numbers correlate individual exchanges; operation IDs identify logical
work; event IDs identify acknowledged deliveries. Their caches are bounded and
session-scoped, not a durable exactly-once guarantee across arbitrary resets.

## Supervision and recovery ownership

- Framing errors are rejected and the parser resynchronizes at a delimiter.
- Request timeouts are 500 ms with at most three retries; protocol heartbeats run
  every two seconds while online and idle.
- The STM watchdog observes application progress. The Nordic 30-second watchdog
  requires both main-loop and system-workqueue progress; node availability is not
  a host-feed prerequisite.
- Unexpected READY loss first permits rediscovery/local-watchdog recovery. The
  host then uses a five-second discovery interval, a 20 ms NRST pulse, up to ten
  seconds for discovery, and one/five-second backoffs after failed attempts.
- At most three automatic NRST pulses are allowed in a rolling 60-second window.
  Exhausted recovery and three observed node reboots within 30 seconds latch a
  fault. Restore the underlying problem, then explicitly use
  `scripts/mmctl --port "$NRF_PORT" --yes node recovery clear` to rediscover.
- Intentional maintenance/shutdown and update/repair UART ownership suppress
  competing reset escalation. Automatic recovery never asserts BOOT0 or repairs
  firmware. Clearing a latch does not replay failed operations.

The verified Port 2 READY/IRQ pins remain readable but lack GPIOTE interrupt
support in the pinned SDK. `gpio_interrupts:false` is expected: UART wake plus
10 ms polling remains active. Exact voltage, pins and power order are in
[the pinout](../hardware/PINOUT.md); reset causes are diagnostics, not safety
certification.

## Verification

[VALIDATION.md](VALIDATION.md) separates software tests and user-reported hardware
results from remaining failure-injection, watchdog-expiry and power-loss checks.
A successful build or simulated fault is not hardware evidence.
