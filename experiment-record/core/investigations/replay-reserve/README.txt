Feature-preserving replay telemetry reserve diagnostic
One production TU only: src/replay/ReplayTelemetry.cpp in independent frozen-latest clone.
Original release compiler/link argv replayed. Original source, objects and binary unchanged;35runtime library hashes identical.

GLOB2_INV_REPLAY_RESERVE read once: "off" preserves old vector growth, default/"on" reserves once for exactly valid schema values plus all named values. Every value, order, text, updated stamp, capture cadence, logical16MiB budget, truncation decision and read/write format remains identical. No headers/ABI or gameplay rules changed. Allocation-exhaustion behavior can differ; root conformance required before any recommendation.

GLOB2_INV_REPLAY_CAPACITY="1" enables write-time diagnostic stdout JSON only. Reports retained frame/row/value sizes/capacities, sizeof objects, vector object storage bytes, text lengths/capacities, logical budget usage, truncated/until. Text capacity includes SSO and is NOT an external heap byte counter. Current/latest scratch map allocations and allocator overhead are not included. This output is diagnostic only; disabled for timing windows.

replay-checks.json:8192 Playground, seed1056243202, explicit4 GPUoff, original/prototypeoff/prototypeon, full GCS and exact replay bytes. check_outputs.py reads completed artifacts only, requires full files and compares SHA256+size with no normalization. Missing files fail. All three checks are instrumented/excluded from timing.
replay-windows.json:16 timing windows,2seeds×2reps×2maps×off/on; N4GPUon; Playground20000/Oazis8192. Same binary A/B, diagnostics off. Preserve all AI/replay features. GPU scheduling/quarantine may differ when allocation work changes, so actual device health still must be interpreted separately.
replay-heap.json:two root-owned instrumented CPU4GPUoff8192 heap censuses, same symbolic prototype off/on, diagnostic capacity output enabled. Native timings must not be inferred from heaptrack.
Symbol-only relink matches prototype text and all allocated sections except GNUbuild-id; libraries match. No DWARF; existing function symbols support attribution, not full variable/inlined callsite reconstruction.

No games/profilers launched during preparation. Plans and scripts are ignored local evidence only. Parent/root owns scheduling and result interpretation; no improvement claim before data.
