# Remaining resource-growth cost: controlled ablations and copy-path experiments

Source under test: `5430ef31c135667deeb8043c827d79b3fec4cda4`. Original immediate implementation: `d42d3e512`. These are experimental binaries built from copied translation units; production objects and the source branch were not replaced. Compiler/link commands and generated sources accompany this report. The baseline data/source root remains paired with its archived executable.

## Findings

- Proposal-record writes are around a 0–2% kernel effect in most matched-inlining controls, not a large remaining bottleneck.
- Sharing arrays already captured every tick adds no measurable capture cost in the controlled inputs. Longer lease retention can increase refresh traffic, but does not establish growth's actual incremental retention in every workload.
- A whole-array copy above 50% dirty chunks improves the multi-material full-engine fixture by **22.3% [17.1, 42.7]** versus current code. Other scenarios are inconclusive. The statistics batching candidate regresses dense throughput and is rejected.
- Applying the copy optimization to BOTH original and delayed engines still establishes no delayed-growth throughput advantage over original immediate growth in any fixture.
- With that copy policy, shared versus direct serial calculation is **−0.6% dense, +1.0% multi-material, +1.8% AI**, with all intervals including parity. The direct control preserves publication timing and avoids pulling unrelated executor jobs onto the owner.
- The useful implementation lead is the snapshot copy path. These data do not support further proposal packing as a major optimization or claim a proven parallel-growth speedup.

## Proposal writes

The count-only kernel reads identical snapshots and consumes identical RNG draws, preserving the proposal count and sampled-cell count while omitting record stores. Inputs and RNGs are reset outside timing. Fifteen map/scenario combinations, 128 correctness seeds each, one warm-up and ten measured pairs of 1,024 calculations. There are no proposal-vector growth allocations in the measured kernel.

The first ablation unexpectedly became slower: GCC inlined its proposal helper while outlining the writer. Merely adding a second diagnostic function also changed the writer’s inlining decision. Those raw trials are retained as `writes.json` and `writes-outlined.json`, but the controlled result is `writes-matched.json`: BOTH helpers explicitly use the same noinline boundary. Generated symbol sizes confirm outer loops of 0x1df1 and 0x1da2 bytes. This is an ablation, not an exact instruction-by-instruction decomposition; code generation can still differ.

| Map | Scenario | Kernel time saved by omitting records, 95% interval | Absolute saving, µs/batch | Proposals/batch |
|---|---|---:|---:|---:|
| 128² | sparse | -0.4% [-1.1, +1.5] | -0.008 | 1.3 |
| 128² | dense | +1.0% [+0.4, +1.7] | +0.044 | 21.7 |
| 128² | saturated | -2.1% [-2.5, -1.7] | -0.076 | 13.5 |
| 128² | blocked | -0.5% [-1.1, +0.2] | -0.019 | 14.9 |
| 128² | multi | +0.0% [-0.4, +0.5] | +0.001 | 43.7 |
| 256² | sparse | +0.8% [+0.1, +0.8] | +0.053 | 5.4 |
| 256² | dense | +0.4% [+0.1, +0.4] | +0.061 | 86.8 |
| 256² | saturated | -1.3% [-1.4, -0.0] | -0.196 | 54.4 |
| 256² | blocked | -0.3% [-0.6, -0.1] | -0.035 | 59.3 |
| 256² | multi | -0.0% [-0.3, +0.3] | -0.005 | 174.0 |
| 512² | sparse | +0.5% [+0.3, +1.0] | +0.151 | 21.7 |
| 512² | dense | +0.3% [-0.5, +3.1] | +0.192 | 346.5 |
| 512² | saturated | -0.9% [-1.2, +0.2] | -0.582 | 216.9 |
| 512² | blocked | +0.1% [-0.1, +0.2] | +0.050 | 237.6 |
| 512² | multi | +1.7% [+1.3, +8.2] | +1.387 | 692.0 |

Removing stores is generally around a 0–2% change in these warm-input controls, sometimes negative. It does not identify proposal packing or reserved-vector stores as a large remaining bottleneck. It does not establish their cost for cold inputs or arbitrary custom resource definitions.

## Identical-input snapshot controls

The baseline consumer requests the gradient-style Catalogs/Terrain/Resources/Occupancy/Areas union. Growth adds Growth/Rules and, when necessary, a capture every tick. Both modes receive the same sixteen deterministic stock edits per tick outside timing, and final stocks match. Initial captures and growth-field preparation are outside timing. Each case measures 256 ticks, one warm-up and ten rotated pairs.

| Map | Existing capture cadence | Existing capture ms | With growth ms | Extra capture ms, 95% interval | Tracked copied MB, existing → growth |
|---|---:|---:|---:|---:|---:|
| 256² | 1 | 1.386 | 1.398 | +0.009 [-0.005, +0.101] | 9.82 → 9.82 |
| 256² | 4 | 0.836 | 1.400 | +0.561 [+0.497, +0.572] | 5.86 → 9.82 |
| 256² | 32 | 0.412 | 1.243 | +0.822 [+0.797, +0.962] | 2.83 → 9.82 |
| 512² | 1 | 5.806 | 5.747 | -0.029 [-0.102, +0.038] | 21.13 → 21.13 |
| 512² | 4 | 4.015 | 5.004 | +1.049 [+0.926, +1.140] | 13.47 → 21.13 |
| 512² | 32 | 4.067 | 5.744 | +1.614 [+1.253, +1.730] | 8.31 → 21.13 |

At the same per-tick boundary with the necessary arrays already captured, growth adds no measurable capture cost and exactly zero additional copied array bytes here. Additional capture frequency has a cost.

A separate control keeps the capture cadence and edits fixed, warms 32 ticks to populate the buffer pool, then measures 256 ticks while retaining 0/1/4/8 prior growth projections. Retention does not change stocks. It changes how old the available pooled buffers are.

| Map | Retained snapshots | Capture ms | Tracked copied MB |
|---|---:|---:|---:|
| 256² | 0 | 0.765 | 6.87 |
| 256² | 1 | 0.775 | 6.87 |
| 256² | 4 | 1.338 | 12.49 |
| 256² | 8 | 2.537 | 16.83 |
| 512² | 0 | 2.425 | 15.83 |
| 512² | 1 | 2.437 | 15.83 |
| 512² | 4 | 4.824 | 28.95 |
| 512² | 8 | 7.581 | 39.07 |

This identifies a retention mechanism, not the actual growth lease duration in every engine workload. Growth releases its snapshot when calculation completes, not at publication. The eight-tick publication horizon therefore does not imply eight-tick snapshot retention.

## Publication/statistics ablation

The diagnostic omits growth-statistics writes while keeping all resource mutations and acceptance/clamping counters. This is NOT a valid production mode. All growth counters and final engine checksums match in the four fixtures; checksum equality alone does not validate statistics, which are intentionally missing. Existing display statistics must be preserved.

| Scenario | Publication time reduction | Whole-engine throughput gain | Saved wall ms / 1,024 ticks |
|---|---:|---:|---:|
| ai512 | +48.5% [+45.9, +49.5] | +0.4% [-1.9, +2.1] | +5.12 |
| dense | +75.8% [+74.3, +76.6] | +1.5% [+0.2, +3.0] | +7.09 |
| disabled512 | no growth | -0.7% [-2.7, +0.3] | -7.40 |
| multi | +35.4% [+33.5, +36.8] | -3.1% [-5.8, +17.0] | -157.85 |

Statistics consume much of dense publication time, but removing them produces only a small or inconclusive total-engine change. A valid alternative was tested: aggregate global material counters per batch, use bit masks to visit covered teams, and use map shifts/masks instead of division for coordinates. It passes differential checks against original publication for 32,768 mixed signed operations, four teams, overlapping coverage, and material stocks after each batch (3,146,307 assertions), plus all ten growth tests. Its actual throughput result is below; fewer-looking operations did not guarantee a win.

## Copy-path candidate and complete-engine measurements

Current dirty-chunk refresh performs sixteen row copies per dirty 16×16 chunk. The candidate counts dirty chunks and chooses one contiguous array copy when more than half are dirty. It deliberately copies some unchanged cells to reduce tiny-copy overhead. This applies to shared snapshot arrays, not just growth.

One warm-up and ten rotated paired repetitions, 1,024 ticks, four executor slots, growth delay eight, fixed AI/gradient settings and identical starting saves. Timers include final compute drain and omit per-tick checksum sidecars. Every candidate has matching final checksums and growth counters against the current implementation. The batched statistics candidate is a separate variant; it is not combined with the copy change.

| Scenario | Batched-statistics throughput gain | Large-copy throughput gain | Large-copy process CPU reduction |
|---|---:|---:|---:|
| ai512 | +0.6% [-0.8, +1.6] | -0.0% [-1.8, +1.6] | -0.5% [-2.8, +2.8] |
| dense | -2.2% [-3.9, -0.6] | +4.2% [-1.4, +7.6] | +3.6% [-2.3, +7.4] |
| disabled512 | -0.5% [-2.4, +3.1] | +0.6% [-2.1, +2.4] | -0.3% [-3.6, +2.5] |
| multi | -7.3% [-14.5, +4.3] | +22.3% [+17.1, +42.7] | +1.3% [-0.5, +4.4] |

The statistics candidate is not adopted: it regresses dense throughput and does not establish a gain elsewhere. The copy candidate improves the multi-material case; other scenarios must be judged by their intervals, not their point estimates.

## Fair optimized-original comparison

The SAME dirty-copy threshold is applied to the original immediate engine and the delayed shared engine. Ten paired measurements after one warm-up per fixture. This avoids attributing a generally useful snapshot optimization solely to parallel growth. These are still different gameplay revisions: RNG sequencing, within-pass feedback, publication timing and one-unit multi-material seeds differ. Earlier ecology measurements quantify those differences; no exact old/new behavioral equivalence is claimed.

| Scenario | Optimized original median ticks/s | Optimized delayed median ticks/s | Delayed throughput gain, 95% interval | Delayed CPU change |
|---|---:|---:|---:|---:|
| ai512 | 727.2 | 720.6 | -1.3% [-2.1, +2.2] | +3.3% [+0.4, +4.3] |
| dense | 2257.5 | 2260.3 | +0.9% [-3.6, +4.1] | +0.4% [-3.4, +4.8] |
| disabled512 | 992.9 | 1001.1 | +1.3% [-2.7, +3.4] | -0.7% [-2.3, +2.3] |
| multi | 269.5 | 275.5 | +1.7% [-4.3, +10.7] | +2.3% [-1.2, +4.7] |

Ticks/s columns are independent medians; gains are paired ratios and need not equal ratios of the displayed medians.

## Coarse CPU attribution

Five measured repetitions after warm-up. Thread-CPU clocks wrap capture, calculation, publication, submission, gradient seeding/propagation and AI; nested scopes subtract child CPU. This avoids treating queue latency or descheduling as computation. Instrumented results are attribution evidence, not speedup measurements. Small scopes include probe overhead. Values below are median CPU ms per 1,024 ticks; independent medians need not sum exactly.

| Scenario / revision | Snapshot capture | Growth calculation or old immediate pass | Publication | Submission | Gradient seed + propagation | AI | Total process CPU |
|---|---:|---:|---:|---:|---:|---:|
| ai512 / legacy | 453.0 | 258.6 | 0.0 | 0.0 | 2938.6 | 529.3 | 4470.0 |
| ai512 / current | 473.5 | 217.1 | 9.3 | 1.6 | 2881.0 | 528.9 | 4389.3 |
| dense / legacy | 228.5 | 108.3 | 0.0 | 0.0 | 900.3 | 561.4 | 1868.3 |
| dense / current | 261.1 | 67.2 | 28.4 | 1.3 | 912.8 | 616.8 | 1976.9 |
| disabled512 / legacy | 167.2 | 0.8 | 0.0 | 0.0 | 2627.3 | 269.9 | 3274.9 |
| disabled512 / current | 160.2 | 0.0 | 0.0 | 0.6 | 2574.6 | 276.3 | 3214.0 |
| multi / legacy | 4771.5 | 858.0 | 0.0 | 0.0 | 5704.0 | 2074.1 | 13650.9 |
| multi / current | 4744.6 | 573.0 | 355.7 | 2.8 | 5648.9 | 2270.4 | 13804.4 |

Old/new attribution combines implementation overhead with changed game trajectories and integrated source revisions. It must not be read as an identical-work kernel comparison; the dedicated controls above address that question.

## Measurement limits, interruptions and validation

- Linux x86-64, AMD Threadripper 2950X, GCC 15.2 release/O3. The same SDL3/recording dependencies and production object files are reused. No builds or correctness suites overlap timing. Only light inspection/report work ran during measurements.
- Reserve physical cores 0–3 and SMT siblings 16–19; engine affinity 0–3; performance governor. This does not isolate package power, memory bandwidth, shared caches or all kernel work. Intervals are 2,000-resample deterministic bootstrap intervals for paired medians on this host, not population guarantees.
- An initial runner stopped after successful statistics measurements because its stage output directory collided with the stage executable name; the output directory was corrected before stage measurements. The log is retained.
- Two original-wrapper attempts stopped when unrelated reservations changed the ordinary root CPU set. Our own exclusive CPU partition remained intact. Both owned groups were removed and governors restored; the first recovery audit explicitly verifies root CPUs/affinity returned. Completed pairs were retained; interrupted runs were not counted. Resume metadata and all failure logs are included.
- A local audited wrapper variant allows unrelated root partitions to change ordinary CPUs while still validating our exact effective/exclusive CPU set, root-partition validity, memory node, controllers, unprivileged process identity and engine affinity. It only removes its own group. Five focused fake-system checks validate acceptance of unrelated partitions and rejection of own-partition/memory/overlap changes. Its source and restoration audit are included; it is not a committed infrastructure change.
- Snapshot copy candidate: 27 of 28 snapshot tests pass. The sole failure is the old metric assertion requiring exactly three chunks to be copied when three of four chunks changed; the experimental threshold copies all four. Content/isolation assertions pass. This known contract change must be updated/tested before promoting the candidate.
- Found a pre-existing accounting gap: `WorldCapture.cpp` copies the multi-material stock sidecar via `copyArray`, which does not increment `storage.bytesCopied`. Full-engine copied-byte counters therefore omit that sidecar and must not be described as total copy traffic. The single-material capture/retention controls do not have that sidecar. Wall/CPU timings are unaffected by this reporting gap.
- Native final checksums are checked for both experimental candidates; the statistics differential additionally compares the actual measurement arrays because normal engine checksums do not include them. No cross-platform or actual threadless candidate validation is claimed. Production snapshot contracts, platform coverage and an appropriate copy-policy regression test remain work before adoption.
- Fresh master remains `52e4d3dc0440e12ff65494ad3eb1681e9c2c0af3`; the existing tested source integrates `06a106d3a888ef11482a955bddb713d584cd5570`. Only scripting tests/traces differ on that newer base. The source worktree remains clean.

Raw rows, source copies, compiler/link commands, test logs, scripts, interrupted audits and successful restoration audits accompany this report. Experimental binaries and object files are omitted from the evidence branch; their hashes are in run metadata.

## Direct-owner scheduling control

OwnerOnly jobs are invisible to workers, but any owner join can execute older queued jobs; they do not necessarily remain uncomputed until their own publication deadline. The first attempted eager control immediately joined each growth batch. Inspection showed that this also steals earlier AI/gradient jobs onto the owner. Its interrupted `owner-control` rows are retained but excluded from pure placement conclusions.

The corrected direct-owner diagnostic invokes only the pure growth job on the simulation owner at capture, without submitting a growth executor job or draining unrelated jobs, then retains its output until the unchanged eight-tick publication deadline. Shared and direct modes use the SAME experimental executable, large-copy policy, four executor slots, starting saves and AI/gradient settings. Ten rotated measured pairs after one warm-up; 1,024 ticks each. This compares shared execution with a strong serial baseline, including queue and lease effects, rather than claiming to isolate only thread dispatch instructions.

| Scenario | Shared throughput gain vs direct-owner | Shared process CPU change | Saved wall ms / 1,024 ticks |
|---|---:|---:|---:|
| ai512 | +1.8% [-0.7, +3.0] | +0.6% [-2.0, +3.2] | +25.93 |
| dense | -0.6% [-3.5, +0.7] | +1.2% [+0.1, +5.2] | -2.62 |
| multi | +1.0% [-8.6, +15.7] | -0.3% [-2.9, +7.1] | +39.44 |

| Scenario / mode | Wall ms | Process CPU ms | Capture ms | Tracked copied MB |
|---|---:|---:|---:|---:|
| ai512 / shared | 1416.3 | 4472.5 | 410.9 | 938.0 |
| ai512 / owner-direct | 1442.2 | 4420.9 | 374.3 | 924.3 |
| dense / shared | 458.7 | 1808.7 | 102.7 | 853.5 |
| dense / owner-direct | 443.8 | 1714.9 | 101.9 | 853.2 |
| multi / shared | 3938.1 | 12767.6 | 3134.6 | 4427.2 |
| multi / owner-direct | 3876.7 | 12722.1 | 2755.1 | 4430.0 |

Direct-owner validation: all ten growth tests pass. Six separate 128-tick runs match exact world and replay checksum sidecars between owner/shared for all three active scenarios at delay 8/thread count 4. All timing runs additionally require equal final checksums and growth counters. The direct mode is an experimental scheduling control, not a supported production mode. Earlier eager-join tests had nine passes and one expected deferral-metric failure; their six checksum runs also matched, but this did not make their scheduling comparison clean.


[Machine-readable summary](summary.json) · [Revision/hash/checksum and cleanup verification](verification.json) · [Copy-policy test log](bulk-copy-tests.log.gz) · [Direct-owner test log](direct-owner-tests.log.gz)
