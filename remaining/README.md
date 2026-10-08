# Four remaining resource-growth experiments

Tested implementation: `9e9497d7151effae8fdd00a48db515511d30077a`; current-master comparator: `6487b873dd3f29ad3ab75c7513597fa47905c132`; historical baseline: `edb09d40204a1fdb3a6d0e5934ef34e9c344de60`.

## Decision

Do not adopt A, B, C, D or their tested combinations. Preserve the existing contiguous snapshot-copy optimization and delayed shared growth. All prototypes and measurements are archived here, outside the implementation branch.

All four candidates were implemented before corrected timing. Separate release builds covered A, B, C, D, A+B and A+B+C+D, plus the unchanged executable and an all-off control. Component measurements, differential verification and paired whole-engine runs used fixed inputs. Uncertain qualification comparisons were extended to 30 pairs without discarding slow results. The initial pilot was excluded because shared scaffolding changed its all-off layout/setter; its evidence remains available.

A and A+B could not rule out the allowed dense regression at30 pairs; B and D remained uncertain on multi-material throughput; C could not rule out the disabled-growth CPU regression. All-four initially qualified against edb09d402, then failed fresh confirmation after integration with master 0f1a2569: dense throughput −2.95% [−4.08,−1.50], CPU reduction −3.52% [−4.84,−1.40]; saturated throughput −2.99% [−4.23,−2.19]. AI tick-p99 rose15.95% [12.11,17.41]. No candidate qualified for the proposed performance sensitivity sweep, so that conditional sweep was not run. The correctness matrix across delays/thread counts did run.

Component gains were real but insufficient: A improved the isolated multi-material stock update by about40%; C cut sparse512 capture time by about94.5% and explicit copied bytes by99.4%; D reduced clustered copy calls by96.9%. Component probes are instrumented and do not establish engine speedup. See raw component records and full tables.

## Final retained implementation against current master

Four reserved physical cores plus their SMT siblings, performance governors, one warm-up and ten alternating measured pairs per scenario, fixed1,024 ticks, delay 8/shared 4. All builds and checks completed before timing. Positive values favor the retained branch; paired bootstrap 95% intervals use 2,000 resamples. Warm-ups excluded, no slow samples dropped.

| Scenario | Throughput change [95% CI] | Engine CPU reduction [95% CI] | Wall saved / 1,024 ticks |
|---|---:|---:|---:|
| dense | +8.73% [+6.51, +11.30] | +7.66% [+5.27, +9.72] | +41.26 ms |
| multi | +58.25% [+47.67, +70.24] | +4.52% [+0.61, +7.29] | +2282.52 ms |
| ai512 | +4.21% [+2.82, +4.96] | +2.83% [+1.41, +3.53] | +59.06 ms |
| disabled512 | +2.01% [-1.64, +4.51] | +1.84% [-2.24, +4.27] | +20.68 ms |
| sparse | -0.40% [-4.56, +1.93] | +0.83% [-0.62, +2.94] | -0.40 ms |
| saturated | +6.84% [+4.03, +9.33] | +5.28% [+2.73, +7.53] | +30.71 ms |
| harvested | +2.85% [+0.50, +5.70] | +3.75% [+0.73, +6.18] | +13.76 ms |
| fragmented | +10.22% [+8.09, +13.70] | +5.32% [+2.29, +7.17] | +260.23 ms |

Master uses immediate growth, so this is a product-level comparison with different resource trajectories. It is not an isolated parallelism speedup. Final stock differences include −7.96% multi-material, −19.92% sparse and −20.29% fragmented. See [resource outcomes](./final-work-comparison.md), per-material/statistic records and playable saves.

## What the parallel controls establish

On the integrated experimental all-four build, shared versus direct-owner calculation improved multi-material throughput9.49% [5.89,11.12] but increased engine CPU2.36% [0.42,3.42]. Dense throughput fell1.67% [0.60,3.32] and CPU increased3.24% [1.60,4.95]. Against optimized immediate growth with applicable common map/copy changes, multi-material throughput was+3.19% [−0.49,12.14], inconclusive, with CPU3.86% higher [1.01,5.06]. These diagnostic comparisons use the rejected candidate and confirmation master 0f1a2569, not the final retained binary. They do not justify a universal parallel speedup claim.

## Integration and compatibility

Latest master independently allocated format 144 and SIM 30 to artwork, colliding with the earlier growth prototype. The delivered merge uses save/replay 146, protocol 64 and SIM 31, while preserving save floor 58. A bounded format 144 discriminator loads both lineages;145 compact-growth saves remain supported. New maintained binary fixtures exercise load/resave and owner/shared continuation. No A–D code or experimental switches ship.

Retained validation:210 engine cases passed, 10 display skips ; 21 unit cases passed; 12 golden cases passed. The retained 144-record per-tick matrix matches edb09d402 across eight fixtures, delays 1/3/8 and owner/shared 1/2/4/8. Goldens changed only for integrated version/header gates; the 150 seeded-resource digests are unchanged. See [verification](./verification-summary.md).

Linux x86-64/GCC 15.2/O3 only; Windows, macOS, Android, browser and threadless execution unavailable. PR remains draft; maintainer playtesting and other-platform execution are outstanding. Core reservation does not isolate shared memory bandwidth/cache/package power. All owned reservations/governors are restored in the audits.

## Reproduction and evidence

- [Full isolated, combination and final performance tables](./performance-tables.md), including absolute wall/CPU differences, confidence intervals, p99 and RSS.
- [Rebuilding prototypes](./reproduction/README.md), exact source overlays/patches and [scripts](./scripts).
- [Frozen final revision and executable](./retained-freeze.json), [toolchain/dependencies](./final-build-freeze.json), [input hashes](./manifest.json).
- [Timing boundaries](./timing-boundaries.md): overlapping stage timers are not additive costs; master histogram percentiles are not exact branch p99.
- [Final timing plan](./retained-plan.json), [raw measurements](./retained-comparison), [CPU reservation audit](./cpuset-retained.json), [governor audit](./governor-retained.json).
- [Final saves and stock/statistic inspections](./retained-work) and [current-master saves](./current-master-work).
- [Candidate acceptance decision](./adoption-decision.json), [corrected isolated results](./extended/summary.json), [fresh integrated confirmation](./confirmation/summary.json).

Raw commands contain original local paths: substitute your checkout, dependency and output directories. Evidence hashes cover the published bytes. Historical reports are retained with their revision identities; they are not claims about the final delivered source.


## Final memory and baseline caveats

Against untouched current master, median peak RSS rises from189.2 to248.4MiB on multi-material (+30.4% paired median) and200.1 to213.1MiB on fragmented (+6.87%). Additional snapshot retention accounts for part of the difference: buffer high-water marks and reported retained capacity are recorded in `final-memory-investigation.json`. These counters omit stock sidecars and proposals, so they do not fully attribute RSS. This is an existing delayed-pipeline memory cost; no memory improvement is claimed. Untouched-master copied-byte accounting also omits material-stock sidecars that the branch counts, so the raw byte totals must not be directly compared as physical traffic.

The final same-behavior comparison against edb09d402 is mostly uncertain at ten pairs. Sparse throughput improves4.56% [2.94,8.74]; harvesting throughput falls1.88% [0.11,3.11], and disabled-growth CPU rises1.87% [0.37,4.64]. These are integration comparisons, not qualifying new optimizations, and were not extended to30. The intervals do not establish equivalence or rule out every2% regression. No candidate is adopted on their strength.
