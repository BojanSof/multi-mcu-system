# Maintenance roadmap

Multi-MCU Lab is a working host/node reference system. The current architecture and
operating contracts are documented in [README.md](README.md),
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) and
[docs/CONTROL_API.md](docs/CONTROL_API.md). Contributor rules are in [AGENTS.md](AGENTS.md).

## Current priorities

1. Complete physical failure acceptance: both watchdog expiries, bounded recovery
   escalation, power-cycle fault retention, interrupted updates and signed trial rollback.
2. Measure boot verification, CRC and transport costs before changing implementations.
   Hardware acceleration is not yet implemented; preserve hash/signature checks and formats.
3. Keep setup reproducible, dependency pins explicit and portable libraries independent
   of board/SDK code. Maintain current user guides alongside behavior changes.

## Change gates

Freeze shared interfaces before parallel edits. Preserve working build profiles,
slot/link addresses and image signatures. Use bounded buffers and explicit ownership;
never introduce automatic replay after uncertain reset/update outcomes. Run relevant
native, Python and firmware checks and distinguish actual hardware results from simulation.

No new electronics, irreversible protection changes, automatic ROM repair, production
security claims or runtime AI dependency are part of this roadmap. Older numbered
plans and reports are preserved as [historical records](docs/archive/development/README.md),
not as current operating instructions.
