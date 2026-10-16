# Compatibility and lifecycle policy

Firmware, transport and services are versioned independently. Compatibility is evaluated before a
service is used; discovering one incompatible optional service does not take the link or other
compatible services offline.

## Version rules

- Unknown protocol wire major: reject the frame and do not mutate state.
- Management major mismatch during HELLO: discovery fails explicitly.
- Service major mismatch: that service is unavailable.
- Equal service major with offered minor greater than or equal to the host-required minor:
  compatible. Unknown optional fields or capabilities are ignored unless a command requires them.
- An unsupported command receives an explicit error and does not break other services.
- Firmware semantic versions and the 32-bit build identifier are informational in Phase 5. Update
  eligibility and rollback policy are added in later phases.

The host currently requires lifecycle `1.0`, diagnostics `1.0`, and the known virtual-I/O service
majors. It never infers compatibility from firmware version alone.

## Lifecycle and sessions

Normal node progression is `INIT -> READY -> ONLINE`. `MAINTENANCE` and software-only `SHUTDOWN`
stop acceptance of new virtual-I/O work while keeping lifecycle, diagnostics and heartbeat control
available. Neither state controls physical power. `FAULT_LATCHED` requires a deliberate local
restart; it is not cleared by another transient UART response.

A node warm reboot creates a new session. READY loss/re-rise independently forces rediscovery and
counts a reboot, covering cold power loss where volatile session history may restart. The host
discards pending work and does not automatically replay an unconfirmed application command. A host
reboot likewise starts with no pending application operation; the node finishes only already
accepted bounded local work.

## Supervision ownership

- Parser errors are rejected and resynchronized by the protocol layer.
- The STM watchdog is refreshed only while application progress advances.
- READY loss identifies node unavailability; request timeouts with READY still asserted identify a
  stalled or degraded application/link path.
- Three observed node resets within 30 seconds latch the host fault. Physical NRST escalation is
  disabled until its electrical connection is separately verified and approved.
- Reset-cause values are diagnostic evidence, not safety certification. Raw hardware flags are
  included so unknown combinations remain observable.
