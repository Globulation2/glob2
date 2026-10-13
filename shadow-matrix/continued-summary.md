# Continued turret shortlist testing

The expanded stock combat tests support continuing the prototype. No live cache firing is enabled.

Twenty paired Numbi/Castor runs use ten seeds in two seeded 64x64 stock-unit layouts, for 1024 ticks each. The fortified first-256-tick workload finds targets in at least 10% of eligible searches; quiet continuation stays in the recorded data. Actual original firing attempts are observed after ammunition conversion and before cooldown. Original bullets remain authoritative.

| Configuration (K,R,L,margin) | Original shots | Immediate valid cached shots | Same target and aim | Missed windows | Combat p95 added delay | Stock control missed / offered | Stock control p95 |
|---|---:|---:|---:|---:|---:|---:|---|
| 1,1,4,1 | 868 | 864 | 756 (87.10%) | 0 | 0 ticks | 0 / 14 | 1 tick |
| 1,1,8,1 | 868 | 866 | 758 (87.33%) | 0 | 0 ticks | 0 / 14 | 1 tick |
| 1,4,8,1 | 868 | 864 | 709 (81.68%) | 0 | 0 ticks | 2 / 14 | missed-window failure |
| 4,1,8,1 | 868 | 866 | 708 (81.57%) | 0 | 0 ticks | 8 / 14 | missed-window failure |

The control covers eight admission phases each for stock births and approaching warriors. Two approaching cases never offer the original turret a shot and are explicitly retained as no-reference rows. Missed initial windows count as failures even if the cache fires in a later window.

All 52 focused release correctness cases pass, including building combat and shared-worker lifecycle regressions. All 20 paired battles have byte-identical AI order traces, per-tick checksums and final saves. The previously used fixture omitted registration in static ability lists and did not exercise turret firing; both AI fixtures now register their finished buildings and the matrix requires actual shots.

The isolated shared executor control matches serial ordered outputs at 1,2,4,8 participants over four overlapping generations with reversed submission/join orders. This does not yet integrate targeting into the game's shared completed-tick capture or establish interference bounds.

The custom one-tick fast-air birth stress still misses all 16 windows for every delayed configuration. It is preserved as an adverse cohort. Neither seeded stock combat nor that extreme fixture establishes the frequency of such windows in played matches.

Recommendation: continue integrated shared-pool performance work with K=1,R=1,L=4 or 8,margin=1 as candidates. They remain within the four-tick budget. L=4 and L=8 are within one agreement percentage point; final selection needs CPU/memory measurements under the specified tie rules. Larger capacity and a second guard tile do not improve agreement in these samples.

Timing is exploratory: the current release query microbenchmark retains one warmup and ten rotated paired samples, but unrelated six-compiler host activity was present. No whole-engine speedup, process CPU gate or shared producer memory/lease qualification is claimed. Live experiment registration, admission/source-epoch persistence, version/golden/replay/network acceptance work and supported-platform comparisons remain unfinished.

Verified host: Linux x86_64, GCC 13.3, native SDL3 prefix, ordinary -O3 release. Tested base: f9eae2549f608f804c240709df819bfe5f09f3fb. Master advanced to c7d688e9c with gradient/runtime and renderer changes during this work; integration against that newer revision is unverified. Windows, macOS, browsers and mobile are unavailable in this coverage.

Raw evidence and reproducible aggregation are in continued-findings.json, summarize-continued.py, continued-final.xml/log, continued-query-costs.log, continued-birth-stress.log, continued-host.json and continued-evidence/. Each paired battle retains both saves, checksums and AI orders; every configuration retains successful delays and failure/censor counts. Hiring work and authoritative targeting remain unchanged.
