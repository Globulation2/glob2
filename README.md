# Greedy fetching versus round-trip gradients

Opt-in prototype on master after the engine snapshot/executor revamp. Greedy mode bypasses round-trip field construction and cached round-trip scores. Hiring, resource selection and swaps use their existing fallbacks; swaps remain enabled. Default gameplay is unchanged.

## Results

24 independent seed pairs, 48 sequential runs, 26,000 ticks per run. Timing: process CPU during ticks 16,000–26,000. Gameplay counts: cumulative over all 26,000 ticks. Each pair uses one binary, the same map/seed/AI setup and machine; variant order alternates. No demand/impact auditing or additional per-tick counters. The new exporter reads existing counters only after timing stops.

| Workload | Seed pairs | Late CPU change | Wheat delivery change | Construction completion change | Starvation deaths, round-trip → greedy |
|---|---:|---:|---:|---:|---:|
| Oazis, 11 Maxima, about 1,100 living units late | 12 | -12.7% | +0.2% | -1.1% | 131 → 163 |
| Coral 256², mixed Numbi/Castor | 6 | -12.4% | +4.8% | -1.6% | 1 → 1 |
| Old-town 256², mixed Numbi/Castor | 6 | -9.5% | +6.9% | +10.8% | 7 → 6 |

CPU averages weight independent matches equally, using percentage differences within each machine. Outcome percentages compare summed counts within each workload. Never combine raw CPU seconds across hardware to estimate a speedup.

**Across all 24 pairs:** mean late CPU reduction 11.8% (95% independent-match bootstrap interval 9.4–14.3%); mean whole-match wall reduction 12.0% (10.5–13.5%); mean peak RSS reduction 4.9%.

**Heavy workload uncertainty:** wheat change +0.2%, interval −2.5% to +3.6%; construction change −1.1%, interval −3.4% to +1.4%. Starvation rose by 32 deaths over 12 matches: 10.9 → 13.6 per match, paired difference +2.7 with interval −0.8 to +6.3. This does not establish a statistically clear starvation regression, but the sample also does not establish equivalence or acceptable feel. Both zero change and meaningful harm remain plausible.

The expensive fields are not needed for greedy routing. Avoiding them yields a consistent speed improvement in these sampled workloads without an average wheat delivery drop. Food-sensitive layouts remain a concern. Keep the feature off pending more food-scarce/human-layout testing and play review. This compares the combined greedy-routing/estimated-scoring policy; it does not isolate movement from hiring or swap-scoring changes.

## Method and limits

- 12 game seeds 201–212 on the fixed tracked Oazis map; six independently generated map/game seeds each for Coral and Old-town. All matches included, no outcome-based exclusions. Every run reached tick 26,000 and measured 10,000 late ticks.
- Mac: Oazis201–206 and210–212 plus Coral. Linux: Oazis207–209 plus Old-town. Each machine runs one benchmark process at a time. Low background load remained; these are balanced seed pairs, not ten temporal repeats per seed. Bootstrap intervals quantify sampled match variation, not all maps or measurement noise alone.
- Outcomes cover the full match. Late-only deliveries, trip-duration tails, hunger episodes, individual hiring delays and causal decision forks were not measured in this comparison. The earlier Petri human-layout benefit is not reproduced here.
- Zero gradient workers and one compute thread, hiring filter retained. The background building pipeline is absent. Both variants use the merged master resource pipeline and otherwise identical settings.
- Early failed map loads came from private experimental format141 files. Those files were excluded and regenerated from their seeds with the master-based Linux build before any generated-map comparisons.

## Reproduction and verification

[Combined manifests and exact commands](results/manifest.json) · [Statistics and distributions](results/summary.json) · [Analyzer](scripts/analyze.py) · [Benchmark runner](scripts/run.py) · [Map generation commands](maps/generation-commands.json) · [Cross-platform evidence](verification/cross-platform.json)

Source branch: [codex/greedy-resource-fetching-experiment](https://github.com/Globulation2/glob2/tree/codex/greedy-resource-fetching-experiment), draft [PR #883](https://github.com/Globulation2/glob2/pull/883). Final source a7425355eab5d7cf812ac59458ae9c1c6b954895, rebased onto master 6831dcfefeaac6255c9a93222d0c567fb9359385. The latest base adds a replay-evidence parser fix; native and parser validation was repeated after integration.

Timing binaries use the initial format143/SIM28 prototype (production implementation ce8c34446, simulation base64406b082; exact binary hashes and later test/fixture revisions are recorded in manifests). That source history remains reachable through [the measurement-source branch](https://github.com/Globulation2/glob2/tree/codex/greedy-resource-fetching-measurement-source). Final compatibility binaries use format144/SIM28 at a7425355e. Version gating and the parser-base update do not change routing/scoring code. The version-only update reproduces exact final checksums and outcomes for both variants of a representative26,000-tick seeded match. Final native checks cover43 focused cases plus12 Python evidence-parser tests on each platform. Binary hashes are retained separately.

Native macOS arm64, Apple clang21, release1; Linux x86_64, Ubuntu GCC13.3, release1. Registry/header/replay gates, nearest-patch movement, cached-field bypass, hunger gates, markets, swaps and golden match verification passed. Greedy traces match byte-for-byte for 0/1/2/4/8 gradient workers and corresponding 1/1/2/4/8 compute budgets, 1,024 ticks, on both platforms with snapshot verification enabled. Save/resume matches the uninterrupted next512 ticks on each. The experiment-off original master match record reproduces all702 checksums.

Replay floor144, SIM revision28, save floor58 unchanged. Native browser fixture regenerated; browser/WASM, Windows and Android execution were not verified. No promotion or merge is proposed.
