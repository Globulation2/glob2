# Breachable Highlands: broad generation study

This study checks revision 4 of generator 33 using the previously verified Linux
build. It tests construction reliability and static map properties; the separate
[playtest study](../playtest/README.md) covers AI games and cross-platform checks.

## Coverage

The immutable main manifest contains **3,590 requests**, with candidate selection
turned off (`candidates: 0`): each outcome belongs to the requested seed.

| Cohort | Requests | Coverage |
| --- | ---: | --- |
| Topology | 1,920 | Every width × height × colonies × valley-size combination, at seeds 71001 and 71002 |
| Individual levels | 234 | Every registered value of every control, at two seeds, starting from a spacious 512×512 map |
| Crossing grid | 220 | Every ridge-depth × pass-width × warp combination, alternating minimum/maximum pond sizes |
| Mixed settings | 1,024 | Reproducible uniform draws of registered levels; dimensions restricted to 128–512 to exercise construction |
| Corners | 64 | Every endpoint combination of ridge, pass, warp, pond, wheat and wood; eight workers and maximum algae/fruit |
| Default seeds | 128 | Unselected fresh seeds 77000–77127 |

Shared ranges are 64–512 tiles per axis, 1–12 colonies and 1–8 workers. Generator
ranges are valley size 64–96, ridge depth 3–11, pass width 6–12, extra passes
0–30%, wooded saddles 25–100%, warp 0–100%, pond size 3–6 and each ambient resource
0–300%. The manifest's catalog records every legal step and default. Every value
is tested, but the joint space has 3,088,426,598,400 combinations: this is an
exhaustive topology grid plus interaction sampling, not exhaustive joint coverage.
Some individual-control defaults repeat the same settings and seed; the summary
reports unique requested worlds separately and checks repeated reports for equality.
Width/height in structured jobs are exponents (7 means 128), while report parameters
use tile counts.

Before dispatch, the hypotheses were that supported layouts have no seed-dependent
failures, crossing extremes preserve their intended boundaries, and both resource
ends retain viable starts. No topology was excluded based on predicted success.
Minimum dimensions, the neutral-valley budget, separated home exits and availability
of a saddle can intentionally make a combination unsupported. Other diagnostics
must be investigated rather than folded into that exclusion.

A separate [held-out plan](plan-boundaries.py) adds 136 requests: the same 64
resource/geometry corners on 128×128/two-colony and 512×512/twelve-colony layouts,
plus eight seeds including 0, 2³¹ and 2³²−1. These probe the home-clearance
interaction at different map sizes and the high bits of the seed input.

## Measured layout support

The full topology cohort completed: **338/1,920 requests succeeded (17.6%)**,
covering 169/960 settings at both seeds. All other requests returned one of the
four anticipated layout diagnostics. There were **no paired-seed acceptance
disagreements**. This cohort deliberately includes the entire shared size/count
range, so its low success rate is a real limitation of unrestricted combinations;
it is not an estimate of the failure rate at the supported defaults.

Each table entry is the maximum supported colony count; every count from one to
that maximum succeeded at both seeds. A dash means no supported count. Any map
with a 64-tile side is unsupported. The full observations are in [topology.csv](topology.csv).

| Map size | Valley 64 | Valley 72 | Valley 80 | Valley 88 | Valley 96 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 128×128 | 2 | — | — | — | — |
| 128×256 | 4 | — | — | — | — |
| 128×512 | 6 | — | — | — | — |
| 256×128 | 4 | — | — | — | — |
| 256×256 | 9 | 3 | 3 | 2 | 2 |
| 256×512 | 12 | 7 | 6 | 5 | 5 |
| 512×128 | 8 | — | — | — | — |
| 512×256 | 12 | 7 | 6 | 5 | 5 |
| 512×512 | 12 | 12 | 12 | 10 | 10 |

The 128×512 / 512×128 difference comes from the cell-order sensitivity of the
shared greedy home-placement heuristic. These are measured limits of the current
construction algorithm, not a proof that denser valley arrangements are impossible.
For broad freedom to vary all valley-size levels, 512×512 with at most ten colonies
passed the topology grid. Terrain/resource interaction coverage is reported separately.

## Checks and interpretation

Every successful map passes the production final-world validator: intact ridges,
dry/nonregrowing wood plugs, walking access to every colony and expansion valley,
ponds, crop containment under future eight-neighbor growth, sealed valley boundaries,
connected gates touching the intended valleys, and at least 32 home 4×4 anchors.

The independent report audit checks actual team/worker counts, all-colony walking
connections, wheat access within 24 steps and wood within 32, local building room,
54/24 wheat/wood tile reserves in home-farm placement telemetry, and at least one saddle. It checks
telemetry completeness and retains worst-team observations, timing and memory tails.
A solo map has no rival; absent rival distance there is not a failure. Building
anchors overlap and must not be read as a count of simultaneous buildings.

Farm saturation is an intentional containment policy. Extra requested abundance
cannot consume the town apron. The audit tests the starter reserve separately from
ambient stock and preserves the requested/placed per-valley telemetry for review.
Static success does not establish competitive balance, late-game economy or human
fun; those remain playtest questions.

## Reproduction and execution

Run from the repository root, supplying the revision-4 Linux bundle recorded in the
[build evidence](../playtest/builds.json):

```sh
PYTHONPATH=. python3 docs/artifacts/breachable-highlands/bulk-generation/plan.py "$BUNDLE" plan.json
python3 -m tools.tournaments submit plan.json RESULTS --bundle "$BUNDLE"
python3 -m tools.tournaments run RESULTS --hosts hosts.json
PYTHONPATH=. python3 docs/artifacts/breachable-highlands/bulk-generation/analyze.py RESULTS ANALYSIS
```

[plan.py](plan.py) verifies the immutable supplied bundle and its revision/catalog.
[analyze.py](analyze.py) reads accepted logical samples using `Results`; attempts and
late duplicates do not increase the sample count. Engine and worker artifacts use
the framework's content hashes and compressed-object storage.

The run began with eight slots over SSH. To reduce coordination overhead, its
existing ledger and pinned worker package were moved to `therig.local`, retaining
job IDs, tokens and results. Independent worker directories on that same 32-core
host supplied additional queues. [run-coordinator.py](run-coordinator.py) uses the
shared `Coordinator.run`, with 16 transfer threads and a two-second poll
interval. These are recorded execution settings, not changes to generator requests.
As the retained reports grew, repeated decoding during progress reporting delayed
collection enough to expire leases. The study wrapper now polls only job counts
and decodes old records only when their exports are missing; full resource/host
status is computed at completion. The shared dispatch, acceptance, lease and
artifact-verification methods remain in use. The
[recovery check](check-coordinator-recovery.py) compares missing-export restoration
against the pinned implementation, including late native success/failure records.
Lease-expired attempts remain in the original ledger and do not inflate the map
sample count. No separate engine launcher or SSH worker implementation was
introduced. The final
host configuration records the actual queues and slot limits. Generation timings
under concurrent load are diagnostic, not a controlled single-process benchmark.

## Final results

All **3,726 requests** completed: **1,454 successes**, **2,272 expected unsupported
layouts**, **zero unexpected failures**, and **zero audited invariant violations**.
There were 3,698 unique requested worlds and 1,426 unique successful worlds;
28 repeated baselines had identical native map reports. All 1,806 non-topology
outcomes agreed with the independently swept topology support table.

The main study generated 1,318 maps; the 136 held-out requests all succeeded.
All successful reports carried complete generation telemetry. Minimum home reserves
were 54 wheat and 24 wood tiles. The smallest home had 262 overlapping 4×4 building
anchors; worst starting wheat/wood walks were 17/18 steps. Maximum generation time
was 2.372 seconds and peak resident memory was 84.3 MB under concurrent load.

This does **not** mean unrestricted combinations mostly work: only 39.0% of these
requests generated, because the shared controls permit incompatible topology choices.
Within the observed supported envelope there were no failed generations. The
128 default seeds all passed. Very long routes remain possible: the maximum
worst-team nearest-rival walk was 1,440 steps, and the lowest report fairness score
was 0.755. These are pacing/balance tails, not disconnected maps, and deserve
human playtesting before claiming competitive balance. Farm saturation appeared
in 1,218 successful requests; it preserves the fixed farm boundary and town apron.

### Retained evidence

- [Main summary and metric tails](summary.json), [held-out summary](boundary-summary.json).
- [Main reports, immutable jobs and build](bulk-reports.zip), [retention audit](bulk-retention.json).
- [Held-out reports](boundary-reports.zip), [retention audit](boundary-retention.json).
- [Final worker configuration](hosts.json), [coordinator recovery check output](coordinator-recovery.log).

The complete object stores and coordinator ledgers remain on `therig.local` under
`/home/bradley/glob2-highlands-20260915/bulk-results` and `bulk-boundaries`.
The review archives retain full native reports; their duplicate compressed objects
are omitted explicitly. Lease retries are retained separately from accepted samples.

### Tail cases for play review

| Case | Seed | Dimensions / colonies | Observation |
| --- | ---: | --- | --- |
| Least local room | 74028 | 128×512 / 4 | 262 local 4×4 anchors; ridge depth 11, wheat 225%. Starts still reach wheat within 6 and wood within 13 steps. |
| Lowest fairness | 74967 | 512×256 / 6 | Fairness 0.755; all six starts viable, at least 603 local anchors; worst wheat/wood access 10/17 steps. |
| Longest nearest rival | 74623 | 512×512 / 3 | 1,440 steps for one colony with zero extra passes and 25% saddles; at least 641 local anchors and wheat/wood within 8/13 steps. |

The last case demonstrates why the quality score is insufficient on its own: its
fairness score is 0.984 because the isolation component saturates, yet one start
has a much longer initial route to combat. The separate walking-distance tail is
therefore retained. Wood-clearing shortcuts may change those routes during play.
The post-study [shared-primitive comparison](../framework-refactor/README.md)
retains these exact worlds as native maps and previews.

The later saved-map comparison also exposed and fixed an existing study-tool
rotation-header defect at 11–12 colonies. It was absent from this generation-only
run because `outputs.map` was false. The [follow-up record](../framework-refactor/README.md)
separates that artifact failure from generation outcomes and retains the successful
full-rotation regression after the fix.
