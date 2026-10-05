# Varied-terrain gradient optimization: review evidence

## Revision and conclusion

Candidate **`37d609766374e7efc1679d643c7d18cde84364ab`**, baseline **`3266c8e518a0d40b2100e2e36596822ad74a6f0b`**. PR #791 is stacked on the ecology optimization PR #787; this comparison starts from that ecology-optimized baseline.

General engine-gradient CPU time improved approximately **6%** in the larger real-terrain microbenchmark cohort. Strategic AI travel improved approximately **66%**, measured separately. The **20% general-gradient research target was not met**. The accepted work preserves scalar/SSE2/NEON execution and exact integer results, while avoiding the cache and queue experiments that did not show sufficient benefit.

Accepted changes: immutable movement-cost classes shared across calls; compact per-layer cost/cursor preparation and tighter queue reservation; bounded integer bucket traversal for strategic AI travel; a portable resumable-search seed-scan refinement that keeps the lazy object size unchanged. No new per-cell terrain-cost cache is retained.

Whole-game confirmation status: **complete** (66/66 completed baseline/candidate pairs). This report is generated from completed pairs only; rerun `published/generate-report.py` after remaining runs finish.

All evidence paths below are relative to `artifacts/gradient-optimization/` in the restored archive. These notes are review artifacts, not product documentation.

## Kernel measurements

`bench-confirmation/manifest.json` freezes compiler flags, source/adapter/binary hashes and copied headers. `samples.jsonl` contains each alternating timed sample; `summary.json` contains case medians. Timed builds disable allocation/work counters. Eleven alternating pairs use both shared buffers/workspaces and independent allocations; cold runs remain separately identified. Positive changes in the tables mean slower.

| Cohort, maps ≥128² | Allocation layout | Cases | Aggregate CPU change | Median case change |
| --- | --- | ---: | ---: | ---: |
| General engine kernel | shared | 147 | -6.4% | -3.8% |
| General engine kernel | separate | 147 | -6.0% | -3.5% |
| Classic specialized dispatch | shared | 21 | +2.8% | +0.7% |
| Classic specialized dispatch | separate | 21 | -1.1% | +0.3% |
| Strategic AI travel | shared | 63 | -66.1% | -72.5% |
| Strategic AI travel | separate | 63 | -66.3% | -73.0% |

The aggregate is the ratio of sums of per-case median CPU times; the median-case column weights cases equally. This is neither a whole-game speedup nor a claim that every individual case improves. The general cohort includes classic input deliberately forced through the general kernel; the specialized-dispatch cohort checks actual classic routing separately.

Coverage includes 32² through 512²; all seven swimming profiles; classic, uniform road/ice, sparse/connected roads, dense mixtures and enclosed modifiers; dense/deferred seeds and propagation limits; thin/rectangular wrapped grids; and 8/32/64 synthetic identities. Synthetic equivalent-cost cases preserve the same underlying cost map while increasing identities.

| Synthetic equivalent identities, 128² | Cases across both layouts | Aggregate CPU change |
| ---: | ---: | ---: |
| 8 | 6 | -3.8% |
| 32 | 6 | -14.5% |
| 64 | 6 | -22.4% |

This demonstrates reduced dependence on identity count when actual movement costs are identical. Distinct-cost cases are separate rows in the raw data; increased distinct costs still require additional work. Profile preparation, seed copying, optional plane construction and snapshot copying are reported separately. Tiny single-construction preparation timings are illustrative, not robust microsecond-level speed claims.

## Resumable searches

`integration/paired-process/portable-offset-summary.json` records the accepted portable lazy variant. Four buffer offsets (0, 1, 8 and 31 elements), five layouts, seven swimming profiles, cold/shared snapshot settings and repeated searches expose alignment sensitivity. The independent comparisons completed **37,800 assertions**. Production benchmark coverage also includes nearby, distant and unreachable requests.

| Map width | Grouped comparisons | Median CPU change | Best / worst grouped change |
| ---: | ---: | ---: | ---: |
| 32 | 40 | -7.6% | -20.5% / +1.1% |
| 128 | 40 | -10.6% | -30.7% / -1.8% |
| 512 | 40 | -8.3% | -14.9% / -2.8% |

Each group is itself a median of paired runs. These summaries concern the near-query experiment and must not be presented as a universal lazy-search speedup. Raw logs, source, build/link commands and hashes are in `integration/paired-process/`; `final-manifest.json` inventories the final variants. Source review confirms a paused search retains its terrain snapshot. The accepted change does not add class planes or a new invalidation mechanism.

## Whole-game confirmation

Frozen binaries run eleven alternating pairs per scenario, 4,096 total ticks each, excluding the first 512 ticks from timing; Maxima/Nicowar, game seed 12345, identical maps/orders, serial execution pinned to CPU 0. Growth is disabled **only for the 512² timing scenarios**. The smaller scenarios retain ordinary growth. Source/binary/map hashes, exact commands, affinity, load averages and `/usr/bin/time` output accompany every run in `final-games/<scenario>-<repeat>-<variant>/`.

Run interruption: the timing process received SIGTERM after 45 complete pairs. The resume retained 90 completed samples, discarded 1 unmatched sample(s), and reran the interrupted pair in full before continuing. The tested revision and frozen binaries were unchanged; `final-games/resumption.json` records this recovery. Completed pairs were not rerun or selected by timing.

| Scenario | Pairs | Growth | Baseline ms/tick | Candidate ms/tick | Change | Paired median; bootstrap 95% interval |
| --- | ---: | --- | ---: | ---: | ---: | --- |
| classic128 | 11/11 | on | 0.465 | 0.458 | -1.7% | -2.7%; [-5.9%, +6.2%] |
| classic512 | 11/11 | off | 4.884 | 4.835 | -1.0% | -0.5%; [-2.4%, +3.2%] |
| network128 | 11/11 | on | 0.644 | 0.644 | +0.0% | +0.0%; [-12.4%, +13.7%] |
| mixed256 | 11/11 | on | 2.669 | 2.588 | -3.1% | -2.2%; [-17.5%, +3.5%] |
| mixed512 | 11/11 | off | 3.514 | 3.342 | -4.9% | -6.6%; [-9.7%, -2.9%] |
| ice128 | 11/11 | on | 0.264 | 0.253 | -4.4% | -4.8%; [-5.3%, -4.3%] |

The confidence interval resamples paired ratios with fixed seed 1427 (10,000 bootstrap samples); it describes these observations and cannot remove systematic host noise. The median-of-times change and median paired ratio are intentionally both shown. End-state checksums, team state and gradient scheduling counters match for every completed timing pair. These timing runs do not collect per-tick checksums; the separate continuation suite below does.

Neither classic whole-game scenario has a median regression above 3%; raw uncertainty remains visible in the paired intervals.

## Memory and lifetime

`memory-accounting.json` distinguishes game storage from narrower benchmark fixtures. Persistent prepared profiles total **1,064 bytes** (152 bytes per swimming profile), shared by all searches. Each executor workspace remains **2,072 bytes**; the lazy-search object remains **2,120 bytes**, identical to baseline. Game terrain snapshots remain **2N bytes** per live terrain generation, shared across profiles/searches; output fields remain **2N bytes** per worker field or active lazy field. No per-cell class plane is retained.

For simultaneous work, let `N = width × height`, `G` be captured live terrain generations, `W` executor workspaces each holding one active eager field, and `L` active lazy fields/searches. A useful active-set accounting model in bytes is:

```text
M = 1,064 + 2N·G + W·(2N + 2,072) + L·(2N + 2,120) + Σ private queue capacities
```

Only the 1,064-byte immutable global profile term is newly retained. The snapshots are shared across all seven profiles and any number of readers; they do not multiply by profile or search count. Queue capacities remain independently owned. Add `2N` for each additional queued or published field retained outside this active-set model, and count an aliased field allocation only once. This formula is not total process memory and excludes unrelated map/AI state and temporary stack storage.

| Concurrent component | Scaling | 512² example |
| --- | --- | ---: |
| All prepared profiles | One process-wide 1,064-byte table | 1,064 B total |
| Terrain snapshots | 2N per live generation; shared across profiles/searches | 524,288 B per generation |
| Active eager field + executor object | 2N + 2,072 + its variable queues | 526,360 B + queues per worker |
| Active lazy field + search object | 2N + 2,120 + its variable queues | 526,408 B + queues per search |
| Per-profile cost planes | Rejected; no retained allocation | 0 B |

The rejected plane approach could have added `7N` bytes per live generation for one-byte class planes across seven profiles (1.75 MiB at 512²), or `28N` for four-byte cardinal/diagonal planes (7 MiB). Multiple live generations would multiply those costs. Those potential worst-case profile-plane additions are avoided entirely by retaining no planes.

| 512² dense cold example | Baseline | Candidate | Change |
| --- | ---: | ---: | ---: |
| Engine retained queue capacity | 262,144 B | 245,760 B | -16,384 B |
| Engine additional peak live heap | 264,192 B | 247,808 B | -16,384 B |
| Strategic additional peak live heap | 1,097,728 B | 1,314,816 B | +217,088 B |

Strategic traversal uses a temporary 32-bucket object (1,024 bytes) and 28-byte cost table, plus temporary wide distances and allocated queue capacity; all are released on return. Its 512² example adds about **212 KiB** peak heap. Queue capacity is private to each worker/lazy search and varies with topology, seeds and profile; the table is a measured example, not a worst-case bound. Snapshot generations remain live while captured by outstanding jobs/searches. Compiler stack frames/spills and process RSS are separate from allocator counters; whole-game RSS is retained in `report-data.json`. Never use instrumented timing for performance claims.

## Correctness and compatibility

- Final unit suite: **800 passed**, 17 skipped, 0 failures/errors (`verified-unit-tests.xml` and log).
- Final engine suite: **274 passed**, 18 skipped, 0 failures/errors (`verified-engine-tests.xml` and log).
- Final continuation comparison: **57,344 paired tick records**, four full games and four continuations, checkpoints at tick 2,048 and completion at 8,192. Serial (1 compute / 0 gradient workers) and parallel (4 / 3) runs match per tick; final save bytes and replay bytes match, and save boundaries match uninterrupted execution. See `review-continuation-final/summary.json`, manifests, commands, saves, replays and checksums.
- The 701-tick match-verification golden matches exactly for both binaries. No simulation revision/save-format change is required for this output-preserving optimization. Per-tick checksums do not separately include RNG state; identical final save bytes include serialized RNG state, and modified algorithms make no random draws.
- Independent arithmetic review: 221 common scalar/SSE2/NEON cases have identical hashes; ASan/UBSan, ARM64 QEMU NEON and randomized independent-oracle testing passed. Native/scalar/NEON fuzz logs include 950-case runs; expanded future-cost fixtures test a larger ring. See `review-arithmetic/summary.json`, `fuzz-manifest.json`, architecture logs and later lazy reviews.
- Oracle fixtures cover cost aliases/extrema, wrapped/thin geometry, deferred seeds, propagation caps, workspace reuse, queued snapshot lifetime, paused terrain edits and concurrent searches. The eager microbenchmarks independently check every field against heap Dijkstra.

## Rejected experiments and limits

Per-cell class/direct cost planes, empty-bucket bitmaps, additional palette dispatch and alternative lazy object/alignment changes were investigated. Their gains were inconsistent, limited to selected cases, or did not justify added construction/lifetime complexity. Rejected prototypes and raw measurements remain under `integration/`, `kernel/`, `core-ablation/` and earlier benchmark folders; they are not part of product history. The accepted strategic bucket change is a separate profiled AI improvement, not a way to count AI gains toward the 20% engine-kernel target.

Master integration: fetched `e1634ecda` has a clean `git merge-tree` result with this branch (`master-merge-check.txt`); intervening seed-related changes were reviewed. The merged result against that newer master was **not built or run as a full engine**. Executable validation applies to the exact tested branch revision above.

Full-engine verification and performance measurements are **Linux x86-64 only**. ARM64 QEMU establishes tested arithmetic agreement, not real ARM/NEON speed. Full-engine Windows/macOS/Android/browser determinism and performance were not measured here. No claim is made about those platforms. Hardware performance counters were unavailable; no host security configuration was changed.

The host is shared. Final game pairs began after this task’s builds/tests ended, but unrelated jobs may still run; recorded load/affinity and paired samples expose some variability. Earlier contended/pilot results are exploratory and excluded from the final headline tables. No whole-game speedup should be inferred from the large strategic-field microbenchmark reduction. Human gameplay review remains useful for the terrain feature as a whole; this optimization is supported by unchanged tested simulation/save traces.

## Reproduction and provenance index

- `final-provenance.json`: final source revision/tree, compiler/platform, binaries, source SHA-256 values, linked-library hashes and build logs. `baseline-provenance.json` freezes the ecology-optimized baseline. `final-history.json` records the unchanged final tree after removing rejected experiments from product history.
- `bench-confirmation/manifest.json`, copied `source/`, adapters and binary hash: exact standalone compiler invocation and all hot-header hashes. `tools/gradient_benchmark.py` is the maintained opt-in runner in the PR. `bench-memory/` contains separately instrumented runs.
- `run-games.py`, `fixture-build-commands.json`, `fixture-spec.txt`, `make-fixtures.cpp`, maps and per-run `inputs.json`: map seeds, transformations, flags, input/library provenance and timing commands.
- `integration/paired-process/final-manifest.json`: lazy benchmark source/build/hash inventory and all offset comparisons. `review-continuation-final/manifest.json` and `environment.json`: exact full-game/save/replay verification commands and libraries.
- `review-arithmetic/*manifest.json`: sanitizer/cross-compiler/QEMU commands and hashes. `final-test-commands.md` records the native build/test commands; suite XML/log files enumerate retained tests and skips.
- `published/generate-report.py` regenerates this report and `report-data.json` using only recorded results; it performs no builds or measurements. Rerun it immediately before packaging to include all completed game pairs.

Compiler: `g++ (Ubuntu 15.2.0-16ubuntu1) 15.2.0`. Platform: `Linux-7.0.0-31-generic-x86_64-with-glibc2.43`. Production flags: `release=1; exact compile/link commands in logs`. Standalone benchmark flags and target architecture are recorded separately in its manifest.
