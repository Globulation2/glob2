# Terrain refactor performance evidence (local working artifact)

This directory is intentionally ignored and is evidence, not permanent documentation.

Final decision: the constexpr metadata optimization was REVERTED after matched neutral workloads regressed. The retained source is the pre-optimization implementation measured by the primary seven-pair matrix. No functional terrain work was reverted.

## Whole simulation

`run.py` used three baseline-generated map files and verified their SHA256 against the ecology corpus. Each map ran normal and disabled resource growth, seven alternating baseline/candidate repetitions, seed 12345, Maxima vs Nicowar,16,384 ticks,2,048 warmup ticks, one compute thread, zero gradient workers. Measured quantity is the engine's process CPU nanoseconds /14,336 measured ticks. Each `inputs.json` records the exact command and binary/map SHA256. `provenance.json` records compiler, revision and initial tracked diff hash; binary hashes were rechecked after the matrix. The original invalid CLI rule attempt is retained separately under `invalid-rule-attempt` and is not a sample.

Baseline binary: e8aae5a19155b6e69f9cc4088833c62b0bdb87aec1cc85df70e462f95808e15a.
Candidate before metadata optimization:00a8d43494919ea78900d77214f93e75cf7516c8f6e7bceeb393120d715330ca, preserved as `glob2-pre-metadata-optimization`.
GCC15.2.0, x86_64 release -O3, AMD 2950X, boost disabled, affinity CPU 15 (sibling 31).

| Map | Growth | Candidate change in median CPU/tick |
|---|---|---:|
| allotments-1427 | normal | +18.89% |
| allotments-1427 | disabled | -10.47% |
| last-treeline-1427-256 | normal | -7.13% |
| last-treeline-1427-256 | disabled | -10.89% |
| river-1427 | normal | -12.81% |
| river-1427 | disabled | +0.13% |

The unweighted median of these six normalized changes is -8.80%; the ratio of summed scenario medians is -6.95%. Those summaries do not erase the allotments normal slowdown. The engine changes RNG consumption and AI decisions, so these runs share initial map/seed but do not perform identical work. Final teams and histories, checksums and counters are retained in `samples.json`. Allotments normal ends with70 units/19 buildings in the baseline and63 units/17 buildings in the candidate. No property-only overhead claim follows from the aggregate ratio.

`profile.py` separately ran allotments normal/disabled with telemetry. Diagnostics cover all16,384 ticks and use elapsed scopes, unlike the primary process CPU benchmark. Normal candidate extra time is concentrated in Maxima (+325 ms) and map work (+313 ms); units (-16 ms), buildings (-2 ms), gradient propagation (-9 ms) and Nicowar (-5 ms) are lower. Growth-disabled map and AI diagnostic times are approximately equal. Profiles locate changed work but do not prove its cause. Whole-process peak RSS in these128² diagnostic runs was49,128→49,292 KiB normal and48,928→49,260 KiB disabled; these are scenario-specific, not bounds.

Concurrent corpus/tests were pinned to 0–11, but unrelated Playwright Chrome processes were observed without affinity restrictions during part of the matrix. See `host-notes.json`. Affinity does not remove shared-cache/bandwidth/thermal noise. Chrome ended before follow-up diagnostics; background corpus continued. These are local-host observations, not a controlled dedicated-machine performance guarantee.

## Matched gradient kernels

`kernels.cpp`: deterministic classic grass/sand/water grid versus the legacy sprite predicate, property fast path, forced generic path on the *same* classic field, and actual ice/road mixed field.128²/256²/512², swim 0/2/3/6,21 rotating repetitions of three runs each; process CPU timing. Seed/reset and checksum work are outside the timer. Classic legacy/fast/generic digests must match. Mixed costs produce different fields/work and are not an equal-work comparison.

An early mode-selection-inside-callback implementation is explicitly retained as invalid dispatch evidence; use `kernels.csv` for the corrected independent-buffer experiment. That experiment showed a128²uniform anomaly. `kernels-shared.cpp` controls gradient and workspace allocation addresses by using the same storage for every mode. The anomaly disappears; the classic property fast path is approximately1–5% faster there. Allocation/code layout sensitivity is why tiny differences must not be treated as precise overhead.

`kernels-prepared.cpp` is an artifact-only interleaved before/after experiment hoisting unique-cost deduplication outside each cost bucket. It shows approximately2–6% generic-kernel savings at128²/256², little stable savings at512². This motivated a small production constexpr metadata experiment, reviewed independently by the core reviewer. It was subsequently reverted because the neutral whole-simulation tradeoff was unfavorable.

The experimental binary `kernels-final` was compiled from `kernels-shared.cpp` against the now-reverted metadata headers. Its historical filename does not identify the retained final source. `kernels-final.csv` repeats the exact cost matrix. Classic property fast-path medians range -12.4% to+1.5% relative to the legacy predicate; most are within about5%. Forced generic classic medians cost+4.5% to+65.1%, because per-cell arbitrary costs retain dynamic vector/cursor selection and more queue work. Actual mixed ice/road costs differ intentionally; do not label their difference pure overhead. All matched digests pass. Retained independent gradient-workspace capacity is approximately66–133 KiB legacy versus142–267 KiB mixed in this matrix; shared-workspace benchmarks retain the largest mode's allocation and are not standalone memory comparisons.

The metadata change passes180 randomized mixed-terrain Dijkstra oracle cases and1,000 independent injected cost/alias cases (equal and unequal cardinal/diagonal costs, odd/even widths, height 1, blockers, multiple goals and caps). Native ASan/UBSan, scalar and ARM64/NEON checks are retained here and in the independent core-review evidence. No save data, cost, seed ordering, snapshot or gameplay arithmetic changed. `post-control.py` is ready to compare preserved and final production binaries after root's rebuild, using identical neutral-map inputs and asserting checksum/final-team equality.

## Rebuild time and memory

`compile-fertility.py` compiles the real baseline/current Field implementation with matching flags and standalone link garbage collection. `fertility-rerun.py` alternates baseline/current then current/baseline; each invocation does one cold and nine warm rebuilds for128²/256²/512² sparse-random, striped shoreline, and dense layouts. All nine classic field digests match. `fertility-repeated-summary.json` is the latest comparison; initial sequential results remain as context.

Striped rebuild medians:1.25→1.81 ms at128²,4.86→8.62 ms at256²,19.24→51.22 ms at512². Dense layout improves20–28%; sparse layout is noisy (-2% to+74%). The generalized signed weighted arithmetic therefore has material cold/rebuild cost even though tick consumers read cached arrays. Current land-field persistent capacity is4 bytes/tile plus object; baseline retained additional scratch/wrapped indexes (512²striped2.36 MB vs current1.05 MB).

`growth-rebuild.cpp` additionally measures the current paired land+aquatic core algorithms and local multiplier, excluding map property gathering. Warm medians across sparse/striped/dense:128²1.99/4.69/18.68 ms;256²7.19/18.54/73.21 ms;512²31.68/84.71/347.8 ms. Cold512²dense358.5 ms. Persistent combined growth cache is12 bytes/tile+128 bytes on this ABI (land4, aquatic4, local multiplier2, habitat mask2); temporary rebuild storage is additional. Authoritative terrain identity is another2 bytes/tile and lazy frozen terrain snapshots use2 bytes/tile, replacing the previous1byte water snapshot when allocated. This cache latency on dense large-map terrain edits is a real limitation, not a per-tick cost.

## Rejected metadata optimization: production control

`post-control.py` completed two alternating pre/final pairs each on allotments normal and river with growth disabled. Every run contains 16,384 checksum records. All per-tick sidecars are byte-identical within each scenario, and final checksums and full team result state match. Per-tick equivalence applies to this metadata-only optimization, not the earlier baseline-versus-refactor simulation change. The two-pair CPU comparison includes sidecar I/O (+3.18% allotments,+1.53% river); a separate seven-pair no-sidecar timing control follows in `post-timing.py`/`post-timing.json`.

The final seven-pair control (without checksum sidecar I/O) gives allotments-1427 +2.47%, river-1427 +5.16%. Initial/final checksums and complete team results remain identical in all28runs. This is a small local timing change with the same workload; it does not change the primary baseline-versus-refactor performance conclusions.

Decision after seven pairs: revert only the four-file constexpr metadata change. Neutral allotments regressed2.47% and river5.16% despite identical results, outweighing the modest2–6% small generic-kernel benefit. Exact cause was not proven; compiler/code-layout effects are an inference. All preceding functional terrain changes and alias tests remain. The original full matrix and shared-address kernel evidence apply to the restored implementation. `metadata-experiment.patch` and `metadata-experiment/src/` preserve the rejected code for reproduction; compile its oracle with `-Imetadata-experiment/src -Isrc/field -Isrc` from appropriate absolute paths.

Verified per-tick sidecars are stored losslessly as `post-*/game.replay.checksums.gz` to reduce evidence size; `post-control.json` records each uncompressed SHA256.
