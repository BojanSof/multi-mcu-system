# Phase 12 freeze — 2026-10-23

Final software phase; physical acceptance remains separate. No new hardware,
flash layout, image schema, option bytes, production security claims or runtime AI.
Root CMake remains native-only. Existing phase9/10/11/default profiles preserved.

## Ownership

Integrator: shared header freeze, root build/wrapper, docs/report and integration.
Nordic agent: Nordic metrics/counters/SMP7/profile configuration/native tests.
CLI/demo agent: tools/cli, new tools/tests/test_phase12_cli.py,
scripts/demo.sh and tests/hil/acceptance_matrix.py (new files only there).
Release/validation agent: tools/firmware/release_archive.py,
tools/firmware/resource_report.py, tools/tests/test_phase12_release.py;
independent read-only safety review after implementation.

## Firmware metrics and profile

New `phase12` profile extends phase11; `MULTI_MCU_PHASE12_METRICS` requires11.
SMP group64 command7 **read only**, API stays1. Phase12 status0 adds metrics:true;
older profiles unsupported cmd7. Snapshot gathered main-thread, copied under control
mutex, SMP never touches manager directly. Lifetime counters span managerInitialize
but reset on Nordic reboot; deltas require same host_boot_id. New frozen manager
metrics type/header additions are integrator-owned. No clearing mutating metrics API.

Flat metrics keys (unsigned, uint64 where required): host_boot_id, uptime_ms,
requests, retries, responses, timeouts, protocol_errors, uart_errors, rx_bytes,
tx_bytes, rx_overflows, tx_overflows, events, dropped, manager_restarts,
online_count, session_changes, ready_losses, node_reboots, latency_count,
latency_sum_ms, latency_max_ms, latency_last_ms, main_stack_free, work_stack_free,
stack_supported. Requests=original successful UART requests; retries=retransmits;
responses=correlated accepted runtime replies; latency firstsend->matchedreply,
including retries, excludes asynchronous completion. Saturate counters if needed;
document wrap/delta semantics, never reset cumulative metrics on node reset.
UART error count increments on positive uart_err_check bits only, never -ENOSYS.
Dropped is ring-history overwrites, NOT necessarily unread event loss. Stackfree
from initialized-stack scan when supported; unsupported sentinelUINT32_MAX.

Phase12 status5/6 add elapsed_ms and bytes_per_second for current/latest job,
measured monotonic start to terminal (all setup/erase/verification included).
No estimate presented as wire throughput. Status polls don't affect inactivity.

Benchmark before claiming optimization: build identical metrics profile with
verbose logs (LOG_MAX_LEVEL3) first (`phase12-verbose`), then normal phase12 with
LOG_MAX_LEVEL2. Compare ELF flash/RAM and emitted project stackframes. This is a
measured flash/logging optimization, not a claim of target latency improvement.
Warnings/errors retained; operator events use SMP and are independent of log level.
No polling/buffer-size changes without measured evidence and regression coverage.

## CLI/demo/acceptance

Add `mmctl metrics`, `mmctl demo` and root scripts/demo.sh forwarding to mmctl demo.
Demo default non-destructive: discover/status/services/versions,100 sequential
sensor requests, bounded actuator operation with progress/completion,600s event
watch. Options --samples N --watch-seconds N --report PATH. --yes required only
optional --exercise-controls (maintenance enter/exit, supported fault exercise with
cleanup, remote reset and fresh-session verification). No updates/erase/reset by
default. Missing capability explicit NOT_SUPPORTED, not fabricated PASS.
Default must reject active maintenance/update/repair/busy node where unsafe.

Machine-readable report identifies mode hardware/simulated/not_run, hostboot/session,
checks PASS/FAIL/NOT_RUN_HARDWARE/NOT_SUPPORTED, request latency p50/p95/max,
unique operation IDs, expected sensor responses, event IDs/gaps and metric deltas.
Do not treat overwritten retained history as lost events; cursor sequence gaps
detect unread loss. Observe actual asynchronous events during watch or explicitly
report NOT_OBSERVED, not proof that no events were lost. Never replay mutations on
reconnect/host/session change; abort acceptance and keep evidence. Status0/api and
host_boot_id validated before sending operations. Failed operation never PASS.

Stable reconnect: record USB serial_number/VID/PID/interface for chosen port or
canonical by-id path before approved Nordic reset. Read-only reconnect matches the
same identity, not whichever singletty remains. If identity cannot be established,
use fixedby-id/path conservatively, never switch unrelated devices. No auto-replay.

HIL matrix newrunner is no-op without --run-hardware and --electrical-verified;
observation/runtime acceptance only by default. Explicit update/ROM/rollback cases
require operator action/checklists with saved keys/artifacts and report pending,
not automatic destructive stress. Matrix reports all scenarios including interrupted
update, bothslots, ROMrepair, rollback and unplug/reconnect with evidence source.
Existing observation-only HIL runner behavior unchanged.

## Release/resource tooling

Preserve release manifest v2 schema. New release_archive create/verify commands
validate existing bundle against publickey then package manifest+four artifacts.
Deterministic sorted ZIP_STORED entries, fixed timestamp/mode, canonical JSON,
no keys/buildtrees/captures. Reject duplicates/traversal/symlinks/missing/hash mismatch/
unexpected entries/encryption/bombs; bounded extraction only to temporary directory.
Don't overwrite unrelated existing output silently. Identical inputs byte-identical.

Resource report consumes actual explicit ELF paths (`--elf NAME=PATH`, --size-tool)
and optional --stack-dir/--output; captures text/data/bss, flash=text+data,
ram=data+bss, .su individual frame sizes. Fail missing inputs/tool errors; label
static frames not call-chain/highwater measurements. No invented hardware timings.
Root CI/native tests exercise fake devices, malformed/replay/reconnect, deterministic
release, size/stack parsing and acceptance failure paths. Clean-checkout reproduction
and complete testcommands/resourcecomparison documented before software sign-off.
