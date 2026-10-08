# One-unit-per-material natural seeding experiment

Prototype only: shipping source/defaults are unchanged. Baseline is current snapshot growth, **not master**. Both run delay 8, shared executor 4, 512 ticks with no players/units/buildings/harvesting. Initial placement stocks remain configured at food 1/paper 2.

## Rule and implementation

For an empty snapshot destination, the candidate emits one eight-byte seed proposal (`material=255` experimental marker), skipping per-material replenishment checks. Publication creates every configured material at one; an ordinary positive replenishment arriving after removal follows the same creation rule. A seed arriving at an already matching resource adds one per material with independent capacity checks; another resource rejects it. Existing deposits continue to use material growth rates when calculating replenishment. Same-type resurrection remains allowed.

There is no per-seed heap allocation: existing pooled proposal vectors are reused. Seed operations count as one proposal/accepted operation; stockAdded counts actual material units. Therefore proposal/accepted counts cannot be compared with baseline as units of growth. Stock/deposit totals and sampled cells are retained for that purpose. In the full-rate controls, identical recorded growth used 17–22% fewer proposal records (one seed record replaces two material records). Performance was not measured; this is a storage/work-count observation, not a throughput/CPU claim.

The marker is not supported by shipping save/replay formats. Do not use this prototype for persistent games. Adoption requires queue serialization/version handling, the ordinary compatibility work, and integration with newer master.

## Matrix and results

Five controlled layouts: uniform-grass control 64²; river corridors 128²; islands with sandy beaches 128²; dry land/sand with sparse ponds 128²; mixed terrain with blocked-growth area 256×128. Non-control resources use land ecology and terrain-dependent fertility. Twenty seeds shift terrain/resource placements. These are scripted terrain fixtures, not production-generator maps or representative player matches.

Food replenishment is 100%; paper is 100%, 25% or 0%. Full matrix: 300 matched baseline/candidate pairs, plus 15 owner repeats per binary (630 world runs). On seed 1 of every layout/rate, owner/shared per-tick heavy checksums match. Other runs allow jobs to overlap normally. Every 64 ticks, independent stock scans reconcile with team statistics, physical tile additions and publication outcomes. Full-rate baseline/candidate physical hashes and stocks match at every recorded checkpoint for all 100 pairs.

Primary outcome is **new stock added over 512 ticks**, not final stocks including starting deposits.

| Layout | Paper 25%: new-stock change [95% CI] | Absolute extra units | Paper 0%: new-stock change [95% CI] | Absolute extra units |
|---|---:|---:|---:|---:|
| grass-control | +24.10% [+21.28,+27.00] | +358.80 | +41.81% [+40.62,+43.11] | +487.75 |
| river | +25.39% [+22.99,+27.53] | +69.85 | +38.23% [+37.01,+39.48] | +85.50 |
| islands | +18.83% [+17.09,+20.57] | +55.20 | +32.53% [+31.31,+33.78] | +76.45 |
| dry | +22.22% [+15.74,+30.21] | +1.20 | +44.19% [+35.71,+55.06] | +1.90 |
| mixed | +22.71% [+20.52,+24.96] | +116.45 | +34.82% [+33.83,+35.80] | +143.35 |

At 100% paper replenishment every layout is exactly unchanged. All ten lower-rate primary contrasts pass a two-sided exact paired sign test after Holm adjustment across 15 contrasts. Pointwise paired bootstrap intervals use 30,000 seed-pair resamples; owner repeats and ticks are not independent observations. Dry terrain produces almost no growth: baseline 5.4→candidate 6.6 units for 25%, and 4.3→6.2 for 0%; its large percentages should not be read as large gameplay effects.

## Stock accumulation and spread separately

| Layout | Paper rate | Food added baseline→candidate | Paper added baseline→candidate | New deposits baseline→candidate |
|---|---:|---:|---:|---:|
| grass-control | 25% | 1191.75→1206.75 | 297.10→640.90 | 415.75→449.20 |
| grass-control | 0% | 1166.70→1214.10 | 0.00→440.35 | 391.15→438.65 |
| river | 25% | 220.90→224.95 | 54.25→120.05 | 82.85→85.55 |
| river | 0% | 223.65→223.90 | 0.00→85.25 | 83.75→85.20 |
| islands | 25% | 236.15→233.25 | 56.95→115.05 | 76.45→76.60 |
| islands | 0% | 235.00→235.35 | 0.00→76.10 | 73.75→76.10 |
| dry | 25% | 4.30→4.30 | 1.10→2.30 | 1.90→1.90 |
| dry | 0% | 4.30→4.30 | 0.00→1.90 | 1.90→1.90 |
| mixed | 25% | 409.30→412.45 | 103.50→216.80 | 142.60→148.40 |
| mixed | 0% | 411.70→410.75 | 0.00→144.30 | 140.60→144.25 |

Giving new deposits secondary stock can also change subsequent local-versus-spread decisions, since the crop branches on total stock. At fractional rates the candidate also skips seed-material RNG draws, changing subsequent private RNG choices. These results measure the combined proposed rule; they do not separately attribute every stock/spread change to initial amount versus RNG draw removal. The earlier full-rate multi-material deficit relative to the old immediate algorithm is unaffected by this rule: the current implementation already seeds both full-rate materials at one.

## Verification and reproduction

Baseline and candidate full runs each passed 31,322 assertions. Candidate direct tests passed 12 assertions: seed creation with zero-rate paper, replenishment after removal, ordinary single-material replenishment, competing seeds, independent capacities and different-type rejection. Candidate pilot passed 9,947 assertions. First harness build hit a TerrainType conversion error, fixed before execution; retained in build-first.log.

Source parent: `a4cf15104`. See build-identity.json for full SHA and hashes, ResourceGrowth.cpp for candidate source, candidate.patch for its difference, seed-rule-harness.cpp for fixtures, and commands.json for exact compiler/link invocations. Existing release objects/dependencies match the preceding experiment build. Embedded TestMain provenance retains the older object-build label; the external build identity and explicit harness/candidate hashes identify these experimental binaries.

Build release objects using the existing dependency setup, then run build-growth-seed-rule.py (which extracts compiler/link commands from the preceding player-free/delay-ablation logs). Run run-growth-seed-rule.py, then analyze-growth-seed-rule.py. Scripts and generated source are included here; substitute the original checkout/dependency paths for another machine.

Linux x86-64 only; no cross-platform/threadless/save-continuation coverage for this prototype. Fetched master 607d2d06f before reporting; newer vertex-terrain/save integration remains outstanding. Builds finished before data collection. The two correctness runs ran concurrently in isolated test profiles, without reserved cores; no wall/CPU timing conclusions are drawn.
