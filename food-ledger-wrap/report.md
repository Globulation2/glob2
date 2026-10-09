# Food-ledger bitmask wrapping experiment

Candidate `d9b52a702d5589d80cb9513e66d18eee162c410b`, baseline `6f8cf442fe5e35aa7e7773f51f3854fa3c8e0ccf` (merged PR 963). therig Linux x86-64, GCC 13.4.0, ordinary release -O3 with symbols, profile=0, no -pg. The candidate binary was built from that baseline plus source.patch before the source commit was made; metadata.json records its hash and patch digest. The production files match the committed candidate.

## Change

Precompute masks independently for positive power-of-two food-ledger dimensions. Normalization compares the cached mask against the current dimension before using unsigned AND, so existing callers that directly modify public dimensions safely fall back to modulo. Nonpositive dimensions still return zero. Placement configures the dimensions through the new helper. Search loops, visitation order, thresholds, scheduling, serialized state and simulation version are unchanged. No intended gameplay-feel change.

## Correctness

- 57 Maxima unit cases passed, including 29 FoodLedger cases and three new focused cases for signed/extreme coordinates, arbitrary dimensions, stale masks and equivalent ledger claims/queries.
- The affected engine harness rebuild and focused Maxima engine checks are still running. No pass is claimed for these additional checks at acceptance.
- Four 4,096-tick continuations: established and dense fixtures, one/four participants. Every per-tick checksum trace, replay byte and final binary save matches the canonical archived reference. These reference bytes were also validated by the preceding integration campaign. verification.json files record SHA-256 comparisons; download the canonical bytes from https://github.com/Globulation2/glob2/releases/tag/evidence-serial-loop-963.
- The simulation-version contract passes without a golden or SIM_REVISION update. No dependencies or public game interfaces changed.

## Paired continuation screen

Established fixture, four participants, 4,096 ticks, ten alternating baseline/candidate pairs. Loading, fixture generation and teardown are excluded. No checksum/replay trace output is enabled in timed runs. Own builds were complete before timing; unrelated builds were active on the shared host, with per-run load snapshots retained. Other-thread CPU is process CPU minus owner CPU. Bootstrap 95% intervals use 10,000 paired-median resamples and seed 7349. Negative changes mean less time.

| Metric | Median paired change | Paired 95% interval |
|---|---:|---:|
| owner | +0.78% | [-16.65%, +10.19%] |
| worker | -9.29% | [-15.96%, -1.63%] |
| process | -7.04% | [-14.83%, +1.10%] |
| wall | -11.68% | [-23.20%, -7.26%] |
| join | -18.99% | [-25.33%, -14.32%] |

The worker-CPU and wall-time intervals exclude zero in this screen. Owner and total process CPU are inconclusive. These are observations for this fixture/toolchain under shared-host load, not universal speedup claims. No median owner/wall regression exceeds 2%, so the planned regression-triggered twenty-pair repeat is not required.

## Mechanism

Optimized normalizeX/normalizeY disassembly shows an AND-and-return path after the mask check, bypassing signed division. The general fallback retains idiv and negative-remainder correction. This confirms the intended mechanism independently of timing noise.

## Scope and next action

The maintainer explicitly accepted this evidence and requested merging the experiment. The original five optimizations are already merged. Local GCC 13 evidence only: no new ARM64/GCC 15 run, complete engine suite, text-save matrix, or all-fixture timing campaign is claimed. The selected continuations stress recurring food searches and dense state, while unit tests exercise arbitrary dimensions and wrap seams. The previous merged-revision profiles and other candidates remain in serial-reanalysis/.
