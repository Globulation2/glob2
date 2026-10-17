# Attribution of proactive building-gradient CPU cost

Production PR1197 remains unchanged at `69e6c86014c50053d36e840a8cfe5290226b7163`. This is a private diagnostic overlay, based on c7 plus the production behavior and instrumentation. It compares baseline, proactive, and an early-ranking intervention.

## Findings

The increase is concentrated in **additional building-gradient initializations and repeated warm refreshes**. It is not simply the same builds occurring earlier. Per-tick build counts rise 8.48% on four teams and 7.47% on eleven teams. Map cold builds decline, but warm refreshes rise 6.90% and 5.28%, and private initializations rise 49.87% and 64.99%.

| Workload | All building-field builds/tick, before → after | Warm Map refreshes/tick, before → after | Proactive fields retiring before consumption |
| --- | ---: | ---: | ---: |
| holiday | 0.4134 → 0.4484 (+8.48%) | 0.3688 → 0.3942 (+6.90%) | 29.58% |
| large-eleven | 2.0579 → 2.2117 (+7.47%) | 1.8417 → 1.9389 (+5.28%) | 35.35% |

Consumption means a hiring **ranking** query or a live owner request. Merely prewarming a shortlisted primary does not count as consumption. A field is counted here only if it is retired and remains unused after retained worker leases have drained. This avoids treating post-retirement ranking queries as wasted preparation. Fields still active at the cutoff are right-censored and excluded from that wasted-field percentage.

| Workload | Proactive fields/game | Unconsumed, retired/game | Discarded before publication/game | Own-edit invalidations before use/game | Idle eviction before use/game |
| --- | ---: | ---: | ---: | ---: | ---: |
| holiday | 908.9 | 272.8 | 27.2 | 90.4 | 137.8 |
| large-eleven | 4016.2 | 1425.2 | 399.1 | 378.6 | 590.1 |

These categories overlap by design: discarded and invalidated/idle fields contribute to the unconsumed-retired total. Across all private fields, including baseline hiring-only fields, unconsumed retired seeds rise from 140.2 to 376.1 per game on four teams and 620.8 to 1,842.5 on eleven teams. Thus the waste is not solely relabeling baseline prewarming.

## CPU attribution

These are **instrumented diagnostic means**, not replacement estimates for the earlier performance measurements. Thread CPU timers, lifetime tracking and counters introduce overhead. All parent/child timing overlaps are accounted for in the building-gradient sum below: private initialization CPU + scheduled Map build CPU + ranking/primary/live search-resume CPU. Each private initialization includes allocation, map-sized seed/goal scanning, terrain inputs and initial shallow propagation; Map build CPU includes its seeding, inputs and prepared search depth.

| Additional CPU per simulated tick, µs | Four teams | Eleven teams |
| --- | ---: | ---: |
| Initial private seeding | +0.656 | +13.755 |
| Scheduled Map building builds | +2.520 | +23.053 |
| Hiring ranking search extension | +0.116 | +0.965 |
| Primary-candidate search extension | -0.123 | -0.805 |
| Live search extension | +0.116 | +0.485 |
| Building-gradient total | +3.285 | +37.452 |
| Entire process | +4.352 | +37.745 |

The building-gradient point estimate accounts for about 75% of the four-team total increase and 99% of the eleven-team increase in these diagnostic runs. Those are descriptive decompositions, **not independently identified causal percentages** of the original 2.2–3.6% measurements. The diagnostic total CPU delta intervals include zero; the eleven-team building-gradient CPU increase is clearer: +37.45 µs/tick, paired 95% interval [+8.73, +65.31]. Private seed CPU increases are clear on both maps. Map CPU estimates are noisier and their individual intervals include zero; the extra warm-build counts are directly measured.

Part of the work really is shifted: primary hiring-preparation CPU falls from 1.959 to 1.255 µs/tick on four teams, and 27.383 to 18.391 on eleven teams. But speculative preparation grows by 1.242 and 21.974 µs/tick, more than the respective primary-preparation savings of 0.704 and 8.992. Overall hiring CPU rises 0.652 and 14.153 µs/tick.

Individual Map build cost remains close: approximately 88.8 → 90.3 µs/build on four teams and 275.4 → 275.6 µs/build on eleven teams. Full-depth build counts do not show a clear increase. The stronger explanation for scheduled-build growth is **more refreshes**, rather than dramatically more expensive individual refreshes.

## Normalization and ranking intervention

The result is not explained by simply having more units: warm refreshes per million unit updates rise 3,414 → 3,646 on four teams and 4,405 → 4,560 on eleven teams. Paired delta intervals exclude zero: [+71.94,+391.69] and [+72.30,+242.00]. Warm refreshes per building-tick also increase (the eleven-team interval excludes zero; see `waste-summary.json`). The increase persists after normalizing for consumer activity. This supports additional repeat work rather than just population growth. Unit-step counts change only +0.44% and +1.61%, with intervals including zero. Unit, building and AI CPU differences do not account for much of the overall diagnostic delta; detailed component intervals are retained.

The heuristic intervention leaves proactive fields and normal live extension enabled, but excludes an untouched proactive field from hiring ranking until a primary preparation or live reader uses it. This reduces earlier exact ranking work. On eleven teams, ranking CPU falls by 1.444 µs/tick versus ordinary proactive preparation, interval [−1.831,−1.071], but total CPU does not improve: the point estimate is +12.49 µs/tick, interval [−16.68,+42.19]. On four teams, total CPU is also not reduced: +4.12 µs/tick [−1.00,+9.39]. Extra builds persist in the intervention. Early exact ranking is therefore a real but smaller direct cost, and removing it alone does not recover the observed overhead.

The remaining causal limitation is specific: these policies can alter routes, ranks and subsequent match development. Profiling locates the added work; it cannot assign a unique fraction of the repeat-refresh increase to earlier field age versus changed later demand. The evidence does rule out the assumption that proactive preparation only moves an unchanged set of eventual builds earlier.

## Experiment and reproduction

192 serial diagnostic runs: 32 paired seeds42–73, two fixed maps, four participants, and three policies. Maps, AI roster, 32,768-tick cap, zero warmup, release -O3/-g0 build, CPU gradients and host-contention guard match the earlier experiment. No detected contention required exclusion. Baseline and proactive final checksums **and actual tick counts** match the earlier unprofiled dataset for all 128 corresponding games. All three policies start with matching checksums. Production source remains clean and unchanged.

The single frozen private binary selects its policy through `GLOB2_GRADIENT_ATTRIBUTION=baseline|proactive|heuristic`. Baseline suppresses new proactive requests; ordinary primary-only hiring behavior remains. Runtime baseline reproduces c7’s simulation and proactive reproduces 69e’s simulation. The full source patch, the two added private headers, source manifest and binary hash are retained. The heuristic policy is an experimental intervention and has no production PR change or save/network support claim.

To reproduce, apply `source.patch` to c7f1cd299 and copy the two private Probe headers into `src/building/`; use `PYTHONPATH=. scons release=1 CXXFLAGS=-g0 client_profile=full server=0 -j6`. Copy the executable to `artifacts/proactive-attribution/binaries/profile`, and put `run_matrix.py`, `analyze.py` and `waste.py` under `artifacts/proactive-attribution/`. The runner also expects the earlier dataset under `artifacts/proactive-gradient/runs/` for exact checksum/tick verification. `run_matrix.py` records every command, environment, hash and result. Analysis uses `uv run --no-project --offline --python 3.12 --with numpy==2.5.4 python artifacts/proactive-attribution/analyze.py` and then `waste.py`.

`results/` exposes all result JSON and run metadata. Six map/policy archives contain field lifetimes, Map build ledgers, raw chronological tick times and run logs. `summary.json` and `waste-summary.json` contain all means and paired bootstrap intervals (10,000 game-level resamples, seed20261018). Timings use CLOCK_THREAD_CPUTIME_ID. Queue-entry counts include duplicates and count work processed, not unique cells. Lifetime classification uses final consumer flags after worker draining, rather than only flags at the moment of retirement. The field ledger flags and retirement reasons are documented in `provenance.json`. `per-game.csv` is the analysis input table.

This is CPU-only evidence from Apple M3/macOS arm64. It does not measure other devices or rendered frame pacing. It changes no production behavior, release dependency, simulation version or existing PR qualification.
