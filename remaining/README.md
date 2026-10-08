Follow-up: [player-free statistics and independent review](./player-free-review.md). New master `0507700f1` adds vertex terrain and remains unintegrated; the historical comparisons below retain their frozen revisions.

# Resource-growth experiments: final delivery

Delivered source: `b7a47812222f34a1a4bccc97adc59f516769fa56`. Current-master comparator frozen before final validation: `67fd5b935c98c3d6d0c5ba9d25754ce7df0f83f9`. Historical baseline: `edb09d40204a1fdb3a6d0e5934ef34e9c344de60`.

## Decision

No A–D optimization is adopted. All four candidates, A+B and A+B+C+D were built and measured independently; their sources remain reproducible in this evidence branch. The existing contiguous-copy policy and delayed shared growth remain.

Individual candidates and A+B did not meet the full acceptance rule, including after targeted extensions to 30 pairs. All-four initially qualified against edb09d402 but failed fresh integration with master 0f1a2569: dense throughput −2.95% [−4.08,−1.50], CPU reduction −3.52% [−4.84,−1.40]; saturated throughput −2.99% [−4.23,−2.19]; AI tick-p99 +15.95% [12.11,17.41]. It was removed. Later master integrations were validated and measured with the retained implementation; the rejected candidate was not relabeled or assumed to qualify on those newer revisions. The conditional performance sensitivity sweep was not run because no candidate remained selected. The delay/thread correctness matrix did run.

Component improvements were insufficient evidence for adoption: A accelerated isolated multi-material stock updates about 40%; C reduced sparse 512 capture time about94.5% and explicit copied bytes 99.4%; D reduced clustered copy calls 96.9%. Components are instrumented; whole-engine acceptance uses uninstrumented executables. The invalid initial all-off/scaffolding pilot remains archived and excluded.

## Final comparison against master 67fd5b935

Four reserved physical cores and their SMT siblings, performance governors, fixed 1,024 ticks, identical starting saves/seeds/orders, delay 8/shared 4. One warm-up and ten alternating measured pairs for each of eight scenarios. Builds/checks finished before timing; no slow samples discarded. Paired bootstrap 95% intervals use 2,000 resamples. Positive values favor the retained branch.

| Scenario | Throughput change [95% CI] | Engine CPU reduction [95% CI] | Wall saved /1,024 ticks |
|---|---:|---:|---:|
| dense | +6.93% [+5.64, +7.74] | +6.15% [+4.95, +6.77] | +29.76 ms |
| multi | +48.55% [+38.71, +56.53] | +5.53% [+3.04, +7.95] | +1893.61 ms |
| ai512 | -0.03% [-1.61, +2.30] | -1.57% [-3.43, +0.47] | -0.40 ms |
| disabled512 | +2.13% [+0.91, +3.86] | +1.54% [-0.15, +3.45] | +21.52 ms |
| sparse | -0.20% [-4.37, +0.59] | +1.93% [-1.07, +3.11] | -0.20 ms |
| saturated | +7.69% [+5.22, +9.50] | +6.37% [+3.78, +8.05] | +33.34 ms |
| harvested | +4.53% [+4.34, +5.85] | +5.44% [+5.12, +5.72] | +21.64 ms |
| fragmented | +16.18% [+14.33, +16.68] | +5.12% [+3.41, +6.24] | +387.14 ms |

This is a product-level comparison with different growth trajectories, not an isolated parallelism speedup. Both final engines reproduce their earlier 6487-based 1,024-tick traces and complete stock/statistic totals. Delayed growth leaves 7.96% less final stock on multi-material, 19.92% less on sparse and 20.29% less on fragmented. Disabled growth matches. See [resource outcomes](./final-work-comparison.md), [trace agreement](./final-work-trace-comparison.json), [stock/statistic agreement](./final-work-stock-comparison.json), and final playable saves.

## Same-behavior comparison against edb09d402

This comparison includes later master integration. No A–D change is present. Ten pairs describe the integration; they are not an optimization-adoption decision and were not extended to 30. Intervals including zero do not establish equivalence.

| Scenario | Throughput change [95% CI] | Engine CPU reduction [95% CI] |
|---|---:|---:|
| dense | +4.69% [+1.46, +6.57] | +5.09% [+1.33, +6.20] |
| multi | +2.17% [-5.38, +15.86] | +3.94% [+1.41, +5.11] |
| ai512 | +0.19% [-1.19, +0.82] | +1.12% [-1.78, +2.80] |
| disabled512 | -0.68% [-3.53, +0.10] | -2.47% [-5.01, +1.93] |
| sparse | +5.16% [+3.46, +10.58] | +2.37% [+1.35, +5.51] |
| saturated | +2.49% [+0.39, +4.14] | +2.29% [+0.35, +3.56] |
| harvested | -0.04% [-0.19, +0.13] | +0.74% [+0.27, +1.27] |
| fragmented | +1.15% [-1.18, +1.96] | +1.84% [-0.62, +3.06] |

## Memory and attribution

- multi: peak RSS 192.6→250.0MiB; paired change +28.59% [+25.79, +33.85]. Snapshot-buffer high-water marks 18→37.
- fragmented: peak RSS 182.3→211.6MiB; paired change +15.61% [+10.57, +18.82]. Snapshot-buffer high-water marks 25.5→37.

Additional retained snapshots account for part of the memory difference, not all of it. Native retained-byte counters omit stock sidecars/proposals. OS peak RSS is the memory measure. Untouched-master copied-byte accounting omits material-stock sidecars counted by the branch; raw byte totals must not be treated as directly comparable physical traffic. Stage timers overlap and are not additive costs. Master exposes histogram tick percentiles, not exact branch p99.

On the rejected integrated candidate with confirmation master 0f1a2569, shared versus direct-owner calculation improved multi-material throughput 9.49% [5.89,11.12] but increased CPU 2.36%; dense throughput fell 1.67% with CPU 3.24% higher. Against optimized-original with common map/copy improvements, multi-material throughput +3.19% [−0.49,12.14] remained inconclusive, with CPU 3.86% higher. These controls do not establish a universal parallel win and are not measurements of the final retained binary.

## Compatibility and verification

Master independently allocated formats 144 and 145 to map/building artwork, overlapping earlier growth prototypes. The final integration uses format/replay 147, protocol 65 and SIM 32, retaining save floor 58. Bounded layout probes preserve both 144/145 lineages and growth 146. Seven real fixtures cover load/resave and owner/shared continuation. No A–D code or experimental switches ship.

Final validation: 238 engine passes, 11 display skips; 29 unit passes; 12 golden passes. All 144 differential records (24 reused references plus 120 fresh final-executable runs) match edb09d402 per tick across eight fixtures, delays 1/3/8, owner 4 and shared 1/2/4/8. All 150 seeded-resource digests remain unchanged; golden checksum changes are header/version changes. An initially stale replay-floor constant was caught, corrected and fully retested; the failure remains in evidence.

Linux x86-64/Threadripper 2950X/GCC 15.2/O3 only. Windows, macOS, ARM, Android, browser, threadless and display execution remain unavailable. PR stays draft for platform execution and maintainer playtesting. Core isolation does not isolate shared cache, memory bandwidth or package power. All owned reservations and governors are restored in the audits.

## Evidence and reproduction

- [All isolated, combined and final tables](./performance-tables.md), including intervals, absolute wall/CPU differences, p99 and RSS.
- [Verification details and commands](./verification-summary.md), [final source/binary/dependency freeze](./final-integration-freeze.json), [input hashes](./manifest.json).
- [Prototype source overlays and reproduction](./reproduction/README.md), [scripts](./scripts), [timer boundaries](./timing-boundaries.md).
- [Final raw timings](./final-integration-comparison), [plan](./final-integration-plan.json), [core audit](./cpuset-final-integration.json), [governor audit](./governor-final-integration.json).
- [Final retained saves/statistics](./final-retained-work), [final master saves](./final-master-work), [memory attribution](./final-integration-memory-investigation.json).
- [Candidate acceptance decision](./adoption-decision.json), [isolated results](./extended/summary.json), [fresh candidate confirmation](./confirmation/summary.json).

Earlier 6487 and0f1 comparisons remain under their original names and revisions. They are not substituted for this final source. Raw commands contain original local paths; substitute your checkout/dependency/output directories. Exact executable hashes identify the tested binaries, built just before committing the identical source tree.
