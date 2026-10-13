# Shared snapshot integration measurements

One completed-boundary union capture is used by the turret producer and existing core reader. Every timed sample asserts exactly 16 captures for 16 cycles. The producer shares component allocations, not separately recaptured equivalents. Timing uses native release, one process warmup, ten paired rotated repetitions, and no checksum/save exports.

| Fixture | Path | Wall µs/cycle | CPU µs/cycle | Capture + submit | Deadline publish | Owner consume | Wall change (95% paired CI) |
|---|---|---:|---:|---:|---:|---:|---|
| empty | pool_1 | 47.8 | 47.9 | 21.4 | 22.0 | 2.7 | -60.9% (-62.0–-59.8%) |
| empty | pool_2 | 36.8 | 65.5 | 22.3 | 8.9 | 2.7 | -69.9% (-70.2–-69.3%) |
| empty | pool_4 | 35.3 | 91.5 | 24.0 | 7.2 | 2.6 | -71.0% (-72.3–-70.2%) |
| empty | pool_8 | 37.1 | 132.3 | 25.5 | 7.5 | 2.7 | -70.1% (-71.5–-66.8%) |
| empty | original | 121.8 | 121.9 | 9.4 | 0.0 | 112.2 | +0.0% (+0.0–+0.0%) |
| combat | pool_1 | 115.7 | 115.8 | 38.9 | 62.9 | 9.0 | -38.5% (-51.6–-25.1%) |
| combat | pool_2 | 65.2 | 114.0 | 35.1 | 18.7 | 7.3 | -61.7% (-69.6–-52.6%) |
| combat | pool_4 | 63.6 | 137.7 | 39.9 | 15.7 | 8.2 | -63.4% (-68.2–-57.1%) |
| combat | pool_8 | 61.2 | 183.5 | 38.9 | 14.2 | 8.5 | -65.0% (-69.3–-55.3%) |
| combat | original | 187.5 | 187.7 | 17.9 | 0.0 | 169.3 | +0.0% (+0.0–+0.0%) |

Costs are per 100-turret targeting cycle, not per engine tick. The baseline includes the same shared core reader capture; comparing these totals to earlier standalone-capture totals is not a matched before/after comparison. Additional index/offense/source facts and required effects still cost work. Capture+submit includes index time; do not add index time again. Final draining completes future outputs without publishing early.

Participant count is total executor participants (1 means serial owner fallback), not additional turret threads. No test-private executor is created. Static core reader requirements model capture overlap with Castor; this does not run concurrent AI/hiring/gradient computation or establish whole-engine interference, CPU or responsiveness gates. Normal targeting remains authoritative and no live experiment/save pipeline is enabled.

Peak conservative index accounting: 18,456 bytes. Peak conservative shortlist reservations: 139,200 bytes. Process RSS samples are retained in host JSON; they are not incremental RSS qualification.

Correctness/compatibility evidence is in correctness.xml, correctness.log and correctness-evidence. Only Linux x86_64 release is verified; cross-platform execution comparisons remain unavailable. No outliers were removed. Host activity is recorded alongside exact commands/binary hash.

Validation: 117 focused snapshot/executor/gradient/turret/combat checks and 36 save/replay/version checks passed (153 total). All 20 Numbi/Castor paired continuations have identical AI order bytes, per-tick checksums and final saves.

Callgrind single-participant complete-cycle instructions, 16 cycles: empty original 34,582,925 → cached 8,539,076 (75.3% reduction); combat original 31,701,158 → cached 13,247,463 (58.2% reduction). Profiling is separate from native timing. Parallel client markers do not establish total worker instruction counts.

Host observation: another benchmark process was active during native timing (about one CPU core in the retained sample). These results support further performance work; quiet-host whole-engine qualification remains outstanding.

Tested base `f9eae2549f608f804c240709df819bfe5f09f3fb` with archived working sources. Fetched master `3e2ba6c07450c94ce0f997220479fd309b71e461` changes executor telemetry and gradient lifecycle calls. The turret producer passes a compilation check against those newer executor APIs with its slot reservation applied, but full integration/runtime against that newer master remains unverified.
