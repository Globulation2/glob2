# Eight-player gradient validation: prospective protocol

Frozen comparison: master37ffbcb05 versus behavior-changing candidate9e62e9c75.
No AI/farming changes between binaries. Use saved binaries from gradient-production,
record SHA256 and source revisions. Do not select candidates using this cohort.

16 map families [1,2,4,8,13,15,19,28,29,36,38,40,48,52,57,58], eight new seeds10501–10508,
256x256, eight teams. Two configurations per map: all Maxima or all Nicowar. Both
versions receive identical map bytes, positions, AI settings and game seed.256 paired
starts,512 games, cap65536ticks. Game is independent observation, not player; seeds
and configurations from one generated map remain clustered. No optional stopping.
Report families that cannot generate eight-team maps; do not silently replace them.

Primary: harvested wheat/common observed tick, all eight teams combined, separately
for each AI configuration. Secondary: meals/common tick, population-time, critical
hunger exposure, starvation per unit-time, combat deaths, and completion. Wins are
not a useful total-quality metric in homogeneous free-for-all games. Integrate each
pair over shared512-tick sample boundaries; report duration and early terminations.
Report zero-denominator cases explicitly. Also report a preselected early window
through16384ticks where both games survive, labeling the survivor restriction.

95% intervals: paired log ratios with equal map-family weighting and hierarchical
bootstrap over families then map seeds,10000 draws, keeping paired versions together.
Raw-count rate differences accompany starvation ratios. Test two AI groups separately;
claims across both require both to satisfy guardrails. Practical target: lower95%
limits above−5% for wheat and meal supply, no material starvation/hunger increase.
512 games do not guarantee precision; report inconclusive if intervals remain wide.
Use earlier two-player maps only as separate exploratory evidence, not pooled.

CPU: tournament timings are throughput diagnostics, not headline CPU evidence.
Preserve32768/65536 checkpoints only for seed10501 (all families/configurations/versions).
Compress generated checkpoints after completion; preserve original byte hashes.
Follow with randomized paired quiet runs from identical initial saves, four repetitions,
one task per physical core without SMT sibling contention. Benchmark both baseline-
and candidate-origin states. Pilot both variants, use common end, report all exclusions.
Profile independently after timed runs; inspect gradient work and population/buildings
rather than presenting population-normalized CPU as a causal adjustment.

Initial compatibility gate: same-candidate macOS/Linux full per-tick checksums on
representative eight-player cases and save/reload boundaries. Production save floor58
is unchanged; candidate119-layout headers use120 only for simulation gates. Any benchmark
header-only119 compatibility copies are separate, byte-hashed inputs to BOTH variants.
Do not merge or install from this experiment. Retain old evidence and all failed jobs.

Map-admission adjustment before any outcome collection: Braided Delta(default rejoining2)
rejects eight teams on256² because it has only six roomy islands. Use rejoining-frequency3
for ALL eight Braided Delta seeds, giving eight island slots. Preserve rejected generation
logs and map hashes; no seed substitution. All128 generated maps have eight-team headers.
