# Compact signed resource growth: implementation and reserved-core measurements

## Implementation

- Proposals are eight bytes: uint32 cell, uint16 resource type, uint8 material, int8 delta. The material slot uses the byte that would otherwise be padding in a naturally aligned cell/type/delta record. Multi-material deposits emit separate deltas; record count is therefore not directly comparable to the previous mask representation.
- Removed deposit incarnation counters from live state, snapshots, checksums and new saves. ResourceCell is back to twelve bytes. No spread/replenishment flag or inherited variety is retained.
- Positive deltas add one unit to a matching deposit or seed an empty cell with exactly one selected material unit. Negative deltas subtract one unit from a matching deposit and do nothing to an empty cell. Different types reject the proposal. Capacity/floor limits and authoritative setters remain. The natural-growth kernel currently emits positives only.
- Habitat, growth-permission and occupancy checks run during calculation. They are not repeated at publication. Removing a deposit or replacing it with the same type does not invalidate pending deltas. Disabling growth rejects the due batch. World/catalog identity is checked once per batch.
- Preallocate a full delay horizon of pooled vectors, initially max(128, cells/16) records each. Raise the reservation to an observed batch size plus 25% headroom if needed. Vectors may grow beyond this heuristic; no proposals are dropped. Capacity growth is measured, not assumed to fit a theoretical percentile.
- Statistics consume the known accepted material delta directly, avoiding full before/after material-stock arrays and material-mask scans per proposal. New-tile, addition and reduction counters are tested.
- Save/replay format 145, protocol 63, simulation revision 30. Save floor remains 58. Format 144 incarnation streams are consumed/discarded; pending masks become ordered positive unit deltas with deadlines retained. Older save loading and current pending save/load are covered.

## Throughput

One warm-up and ten paired measured repetitions per scenario/variant, rotated/reversed order, 1,024 ticks from identical starting saves. Four executor slots; gradient workers two; growth delay eight. Same AI settings for all variants. Separate original-immediate, previous-delayed-shared, simplified-owner and simplified-shared binaries/modes. Ticks/s columns are independent medians; percentage changes are medians of paired ratios and need not equal ratios of those columns. Intervals are deterministic bootstrap 95% intervals over paired median ratios (2,000 resamples).

| Scenario | Original ticks/s | Previous delayed ticks/s | Simplified owner ticks/s | Simplified shared ticks/s | Shared vs previous (95% CI) | Shared vs original (95% CI) |
|---|---:|---:|---:|---:|---|---|
| dense | 2197.8 | 2038.8 | 2146.6 | 2160.2 | +5.4% (+4.1 to +6.7%) | -0.6% (-3.4 to +1.5%) |
| multi | 180.9 | 180.3 | 174.2 | 209.8 | +5.3% (+1.4 to +19.6%) | +12.7% (-0.6 to +20.1%) |
| ai512 | 686.7 | 661.0 | 672.1 | 696.5 | +5.9% (+4.0 to +7.6%) | +1.2% (-0.1 to +3.0%) |
| disabled512 | 952.1 | 924.0 | 945.2 | 956.0 | +2.6% (+1.6 to +4.4%) | +0.3% (-2.3 to +2.6%) |

| Scenario | Shared CPU vs previous (95% CI) | Shared CPU vs original (95% CI) | Paired wall difference vs previous, ms/1,024 ticks | Paired CPU difference vs previous, ms/1,024 ticks |
|---|---|---|---:|---:|
| dense | -5.3% (-6.6 to -4.0%) | +0.9% (-1.4 to +4.0%) | -25.1 | -104.4 |
| multi | -3.7% (-8.3 to -1.1%) | +0.6% (-1.8 to +5.5%) | -259.6 | -509.9 |
| ai512 | -6.4% (-7.9 to -4.6%) | -1.4% (-4.2 to +0.9%) | -86.4 | -319.8 |
| disabled512 | -3.8% (-5.4 to -1.0%) | +0.7% (-1.6 to +3.3%) | -27.4 | -131.5 |

## Effective growth and allocations

Old/new trajectories intentionally differ. In particular, new multi-material seeds receive unit deltas instead of configured initial-stock bundles. These measurements describe actual engine throughput under the chosen rules, not a pure identical-work algorithm speedup. Sampled/accepted/rejected/clamped counters are retained in raw rows; stock and tile additions below expose gameplay differences.

| Scenario | Previous / simplified stock additions | Previous / simplified tile additions | Simplified peak proposals per tick | Calculation-time vector growth batches / submitted (measured owner + shared) | Reserved proposal-buffer high-water bytes |
|---|---:|---:|---:|---:|---:|
| dense | 150129 / 150446 | 34173 / 34115 | 228 | 0 / 20480 | 294912 |
| multi | 1717789 / 1547310 | 176242 / 170995 | 2265 | 0 / 20480 | 1179648 |
| ai512 | 19688 / 19688 | 936 / 936 | 51 | 0 / 20480 | 1179648 |
| disabled512 | 0 / 0 | 0 / 0 | 0 | 0 / 0 | 0 |

Zero calculation-time growth does not mean zero allocations: startup reserves the pool, and the queue/other engine components allocate independently. The estimator is conservative and has no universal 99th-percentile guarantee for arbitrary custom resource definitions. Memory accounting now includes known reserved capacities in in-flight jobs; old proposal-buffer metrics undercounted these, so use process peak RSS for cross-version memory comparisons.

| Scenario | Copied snapshot MB, previous / simplified | Peak process RSS MiB, previous / simplified |
|---|---:|---:|
| dense | 1069.4 / 818.3 | 73.5 / 71.8 |
| multi | 4760.6 / 3824.1 | 207.1 / 200.4 |
| ai512 | 1059.4 / 936.3 | 182.4 / 172.2 |
| disabled512 | 554.4 / 552.3 | 156.2 / 152.1 |

## Component experiment

The component harness resets fixtures outside the timed region and charges every variant for the same pre-existing shared snapshot component union. It compares the original immediate algorithm, the new kernel with immediate serial publication, delayed owner and delayed shared scheduling in the same executable. These fixtures evolve during 64 ticks, so old/new effective work can differ. A split pass still has overhead in some scenarios; simplification does not make the architecture free.

New immediate split elapsed-time change versus the original immediate algorithm, paired medians with bootstrap 95% intervals. Positive values mean slower. Raw compute/publication/capture/copy/stock metrics for all four variants are in component.json (924 rows).

| Scenario | 128² | 256² | 512² |
|---|---:|---:|---:|
| sparse | +22.3% (+21.0 to +27.5) | -1.2% (-3.7 to -0.3) | +34.8% (+33.1 to +38.2) |
| dense | +8.8% (+7.8 to +15.1) | -5.7% (-6.5 to -4.1) | +1.1% (-1.4 to +4.0) |
| saturated | -10.2% (-11.0 to -9.7) | +9.6% (+8.8 to +10.4) | +9.7% (+7.9 to +11.4) |
| harvested | -9.3% (-9.7 to -9.1) | +1.9% (+1.0 to +2.6) | -1.1% (-1.9 to +4.7) |
| blocked | -4.4% (-5.8 to -3.6) | -3.4% (-4.5 to -2.2) | +16.6% (+13.8 to +20.0) |
| multi | +0.7% (+0.3 to +1.2) | +3.3% (+1.3 to +6.1) | +19.5% (+17.9 to +22.2) |
| disabled | -0.4% (-1.4 to +2.2) | -1.2% (-1.8 to +0.1) | -0.4% (-4.6 to +0.4) |

The identical-input live/snapshot kernel control also passed: 15 scenarios, 32 seeds each, exact ordered proposal/RNG continuation equality; 630 timing rows are included in kernel.json. It excludes capture and publication.

## Twenty-seed ecology check

128² dense fixture, 512 ticks, twenty fixed seeds, compared with the retained immediate algorithm in the same executable. Harvest every eight ticks, either retaining the last unit or allowing depletion. Values are means over seeds. These are controlled ecology probes, not a substitute for playing a full match.

| Policy | Measure | Immediate | Simplified delayed | Change |
|---|---|---:|---:|---:|
| reserve | harvested | 16261.0 | 15999.3 | -1.6% |
| reserve | food | 7947.9 | 7642.8 | -3.8% |
| reserve | deposits | 5799.1 | 5720.4 | -1.4% |
| reserve | seeded | 1703.2 | 1624.5 | -4.6% |
| reserve | replenished | 12261.0 | 11773.0 | -4.0% |
| reserve | depleted | 0.0 | 0.0 | both zero |
| deplete | harvested | 10477.5 | 10460.5 | -0.2% |
| deplete | food | 244.2 | 207.4 | -15.1% |
| deplete | deposits | 90.5 | 78.3 | -13.5% |
| deplete | seeded | 109.0 | 199.6 | +83.2% |
| deplete | replenished | 368.0 | 223.6 | -39.3% |
| deplete | depleted | 4114.4 | 4217.3 | +2.5% |

Removing identity restrictions permits pending increments to revive depleted cells, increasing seed/depletion turnover in the depletion probe. Sustainable harvested output in the reserve probe is 1.6% lower; depletion-policy harvest is 0.2% lower. Multi-material seeding changes are quantified separately in the engine table. Updated dense/multi-material saves are under playable/.

## Reproducibility and validation

- AMD Threadripper 2950X, Linux x86-64, GCC 15.2, release/O3; existing SDL3 and recording dependency prefixes unchanged. Exact build/link logs, binary hashes, input hashes and run commands are included. Original immediate binary is d42d3e512; previous binary is the preserved e93956015 production executable from source through 5925e3fe6. Simplified code also integrates master 06a106d3a; this is a full-revision comparison, not a single-change assembly ablation.
- Existing benchmark cpuset wrapper reserves physical cores 0–3 and SMT siblings 16–19, engine affinity 0–3. Governor wrapper selects performance and restores prior settings. Raw rows include host CPU activity/frequency snapshots. Reservation does not isolate shared memory bandwidth, package power or all kernel activity.
- The requested threaded default remains enabled. Worker timing alone is not used as proof of speedup. Main throughput runs have no checksum sidecars and drain pending computation before stopping the timer. Owner/shared final heavy checksums and growth counters are required to match in every measured run.
- Focused growth suite: 10 passing cases. Broader resource/snapshot/gradient/save/replay/network-version/executor/engine-session/diagnostics coverage: 152 passed, zero failed, five display-dependent tests skipped. Twelve golden tests regenerated the affected traces and match record. The simulation-revision gate passed against integrated master. All twelve golden cases passed again without update mode. The three component/kernel/ecology cases passed. Final validation revision is 5430ef31c; a fresh merge-tree against master 52e4d3dc0 is clean, whose additions after the integrated base only change scripting test/trace handling.
- A real format-144 save with outstanding work loads, resaves to 145 and continues identically. CLI checksum comparison normalizes only the known map-header file-format contribution (144 xor 145, rotated through the two-team/two-player checksum); all 64 overlapping ticks and the final state match after that normalization. The initial unnormalized failure is retained. Native unit tests compare current-format continuation exactly, including pending output.
- All 96 native per-tick checks passed across delays 1/3/8, owner/shared placement and executor sizes 1/2/4/8, over four scenarios and 128 ticks. Both world and replay checksum sidecars match at each fixed delay; raw traces are under determinism/. Cross-platform Windows/macOS/browser and actual threadless builds are not verified in this local campaign. The PR remains draft; maintainer playtesting of changed seeding and stale-condition behavior remains necessary.
- Initial build was superseded when current master integration removed an editor source file. The final integrated build and focused rerun passed. An initial unit assertion assumed a fixed stock from randomized resource placement; it was corrected to set the input stock explicitly. Failed exploratory logs are retained, not counted as successful validation.
- Raw measurements, checksums, fixtures/playable saves, wrappers audits, build/test logs and reproduction scripts accompany this report. No timing runs overlap our builds or correctness tests. Only light inspection/report preparation ran while timing.
