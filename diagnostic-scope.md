# Diagnostic and correctness evidence scope

`diagnostics-and-correctness.zip` contains362 files, SHA256 `6fceb7183cad117da38c3e8f03f06b13b9144549e3ff3ac025fc4b3bda3d071f`; every embedded file hash was independently read back and checked after ZIP creation.

- `merged-diagnostics`: exact merged-production Maxima/Cortex priority profiles and separate perf counters. Source/build/input provenance is retained. These ran with other workloads present and are diagnostic only.
- `ring-correctness`: unaccepted Cortex vertical-ring candidate comparisons with the frozen merged engine, worker1/4 traces and pending-gradient save continuations. All40 recorded engine child exits are0; the independent output audit verifies produced files and equality. The enclosing original shell returned143 after printing PASS; its cause remains unresolved. Successful output verification does not establish a successful wrapper exit.
- `candidate-ring.patch` and provenance identify the uncommitted experimental candidate. No controlled end-to-end benefit has been established and this patch is not proposed for merging.
- `platform-closure/resource-component-stress`: six component scenarios across256/512 maps, eight teams, sparse resources,512 equivalent definitions and mixed stocks; counters/storage/timings are diagnostic, concurrent with input recovery. Component timings are not end-to-end or before/after comparisons.
- `priority-timing/host-preflight*`: observed external CPU contention before timing. No live governors were changed during these preflights.

Executable binaries are identified by hashes/build provenance, not embedded in this diagnostic ZIP. The published recovered98-window ZIP supplies exact legacy benchmark fixtures separately. Local absolute paths identify original execution locations; reproduction requires relocating inputs while preserving their hashes. No current-master integration or new macOS/Windows/browser result is implied by these Linux diagnostic artifacts.
