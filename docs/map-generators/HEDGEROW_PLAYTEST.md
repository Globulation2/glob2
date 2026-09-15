# Hedgerow Country: initial fairness playtest

Screenshots, sample maps/replays, complete bulk request/metric rows, and test logs
are available in the [checked-in review evidence](../artifacts/hedgerow-country/README.md).

## Changes selected

Revision 5 improves the optional map's starting locations and farm capacity.
No AI, simulation, shared primitive, save format or network version changed.

- Score alternate, well-separated starting-field arrangements by traversable
  territory and nearest-rival exposure. Randomize the final deal to team indices.
- Enlarge centred farm ponds from 16 to 100 pure-water tiles, with a 29×29-corner
  sand-capped enclosure. Move swarms north so their footprints remain outside it.
- Guarantee 48 wheat and 24 wood tiles in occupied fields before ambient scaling,
  and equalize corresponding crop stocks with a two/three-unit checkerboard.
- Preserve dry structural hedges, the original connected route construction,
  and all four layout controls. Clearing still opens access in both directions.

Code comments explain the geometry, bounded placement search, integer tie shares,
stock policy, and final engine checks. See [the design](HEDGEROW_COUNTRY.md).

## Why these changes

The original start selector spread colonies geometrically, but roads gave some
starts much more private land. On training and held-out maps (256×256, four colonies),
the final worker-based exclusive-territory ratios changed as follows. Here,
territory means walking tiles that the starting workers can reach sooner than
every rival; it is an access estimate, not ownership enforced by the game:

| Map seed | Revision 1 weakest/strongest | Revisions 4/5 weakest/strongest |
| --- | ---: | ---: |
| 7 | 0.243 | 0.506 |
| 19 | 0.498 | 0.740 |
| 20001 (held out) | 0.573 | 0.487 |
| 20002 (held out) | 0.275 | 0.527 |

These are actual finished-map measurements, independently computed from workers.
The generator optimizes a coarser design-time estimate and does not promise these
ratios for every seed. Seed 20001 regresses; this is not a universal improvement.
Across these four maps the mean ratio rises from 0.397 to 0.565. The crowded
128×128 controls change from 0.824 to 0.886 on seed 7 and from 0.743 to 0.622
on seed 20001. All fields are occupied in those controls, so placement has no
alternative sites; changed farm/worker geometry affects measured access. Some
unequal opponent distances remain intentional.

Food needed more than an initial stock increase. In 60,000-tick macOS Maxima
pilots on seed 19, starvation deaths per birth were 0.619 originally, 0.742 with
an intermediate 36-water-tile pond, and 0.483 with the selected larger pond.
The intermediate change was rejected. The corresponding Nicowar ratio fell from
0.364 to 0.199. These cumulative ratios are descriptive, not survival probabilities.
The larger farms allow faster population growth and actual fighting; the original
mirrors remained small with no combat deaths. Human review should assess this
change in pacing and the practical value of chopping a hedge.

## Game results

All 80 distinct matchups reached the 60,000-tick cap; none is treated as a win.

| Cohort / AI / size | Games per version | Opening starvation deaths, old → new | Late starvation / births, old → new | Mean final units per colony, old → new |
| --- | ---: | ---: | ---: | ---: |
| Training / Nicowar / 256² | 8 | 43 → 0 | 31.2% → 26.3% | 20.5 → 50.1 |
| Training / Maxima / 256² | 8 | 59 → 0 | 65.0% → 57.4% | 20.2 → 28.1 |
| Training / Nicowar / 128² | 4 | 22 → 0 | 87.7% → 25.2% | 5.5 → 35.1 |
| Held out / Nicowar / 256² | 8 | 26 → 0 | 31.5% → 25.6% | 20.7 → 47.6 |
| Held out / Maxima / 256² | 8 | 39 → 1 | 64.1% → 60.4% | 21.8 → 25.2 |
| Held out / Nicowar / 128² | 4 | 23 → 0 | 94.2% → 27.9% | 4.7 → 33.4 |

Across 40 games per version, opening starvation fell from **212 deaths to 1**
(3,316 versus 6,125 births in that window). By tick 60,000, starvation per birth
fell from **56.9% to 41.9%**. Absolute starvation deaths rose from 2,898 to 4,897
alongside births rising from 5,096 to 11,686. Later food management remains a
problem, especially for Maxima; the larger farm does not remove it.

Combat deaths rose from 13 to 1,500. The revised economy supports much more
growth and fighting. Final population therefore mixes food supply, expansion,
AI decisions and combat outcomes; it is not a clean balance score.

**Seat balance remains mixed.** The weakest/strongest ratio of early population,
averaged over team rotations at each physical start, improves in 4 of the 10
map/AI blocks and declines in 6. For example, training Nicowar seed 19 improves
from 0.919 to 0.984, while held-out Maxima seed 20001 falls from 0.953 to 0.862.
This change improves measured land access on average and makes the opening
economy more dependable. It does not establish equal AI outcomes or human balance.

## Larger-field regression

The extreme-control sweep found fertile hedge tiles on seed 30002 with field
size 80, 256×512, seven colonies, thickness 4, no extra gateways and fully wooded
boundaries. A centre-distance budget alone missed diagonal growth reach.
Revision 5 raises the clearance from 28 to 34 for field sizes 80/96. It leaves
the field-size-64 code path, terrain, stocks and starting locations unchanged.
The default-layout game evidence therefore describes both revisions; the eight
default golden fingerprints are checked for continuity. The original failures
remain in `field-controls-results/`, and the correction has a C++ regression.

## Reproducible study

Evidence root: `artifacts/hedgerow-fairness/` in this checkout.

- `PLAN.md`, `plan_games.py`: shared tournament planner configuration.
- `baseline-source.tar.gz`, `candidate4-source.tar.gz`, `candidate5-source.tar.gz`
  and SHA-256 files:
  source snapshots; `bundles/` contains immutable Linux executable/data cohorts.
- `*-plan.json`, `*-results/`: authoritative manifests, committed result records,
  checksummed compressed artifacts and retained failure information.
- `analyze_games.py`, `analysis.json`: offline per-game/per-start observations and
  aggregate summaries. Starts retain physical coordinates and cyclic team rotations.
- `candidate4-19.map/png/json`: revised training map and independent report.
- `local-*/`: exploratory macOS games, final saves, replays and timeline logs;
  `maxima-final4.png` shows the loaded final save's terrain/resource preview.
- `static-plan.json`, `static-results/`: held-out parameter sweep using the same
  shared coordinator. `contracts4.log` and `golden-check.log`: C++ regressions.

Training seeds are 7 and 19; held-out seeds are 20001 and 20002. Each build/cohort
uses homogeneous Nicowar and Maxima games on 256×256 four-colony maps, all four
cyclic team reindexings, game seed 41 and a 60,000-tick cap. A 128×128 Nicowar
control uses the first seed in each cohort. That is 20 games per build/cohort.
Rotations reindex the same map's starts; they do not rotate the landscape. The
held-out settings were selected before examining their results.

Baseline training retains per-tick checksums and 15,000-tick saves. Subsequent
cohorts retain final saves, replays and team timelines; omitting repeated checksum
sidecars reduces archive cost without changing map, AI, orders or tick limits.
The early observation window is the last timeline sample at/before tick 15,000.
Early population is averaged over four team rotations per physical start before
comparing the weakest and strongest seats.
Tick-cap outcomes are unresolved, never counted as victories.

Slow artifact collection expired nine training-candidate leases. The shared
framework retained those late attempts and retried the same nine logical games.
All nine pairs have identical result JSON, replay bytes and final-save bytes
(`retry-continuity.json`). Thus the study contains 80 distinct matchups and 89
remote game executions; duplicate attempts are not counted as extra samples.

```sh
python3 -m artifacts.hedgerow-fairness.plan_games candidate4
python3 -m tools.tournaments submit \
  artifacts/hedgerow-fairness/candidate4-plan.json \
  artifacts/hedgerow-fairness/candidate4-results \
  --bundle "$(cat artifacts/hedgerow-fairness/candidate4-bundle-path.txt)"
python3 -m tools.tournaments run \
  artifacts/hedgerow-fairness/candidate4-results \
  --hosts artifacts/hedgerow-fairness/game-hosts.json
python3 -m artifacts.hedgerow-fairness.analyze_games
```

The host is `devlaptop.local`; all remote work is isolated under
`/home/bradley/glob2-hedgerow-playtest-20260915/`. Initial worker throughput was
measured before increasing concurrency. The original checksum-heavy queue and
new games use separate worker directories because the framework serializes
archive compression. Superseded generation-only queues are preserved as
`*-archival-queue/`; their games were drained before any dispatch, then the superseded queues were
cancelled and collected. `prefetch.py` optionally downloads completed spool files
with rsync; the shared coordinator still validates hashes and accepts each result.

## Final validation

- **132/132** Linux default-field sweep cases passed: seeds 20001–20004,
  128/256-tile sides, one/four colonies, minimum/maximum hedge thickness and
  gateways. All telemetry records were healthy. These revision 4 field-size-64
  outputs are unchanged by revision 5.
- **32/32** Linux larger-field cases passed on revision 5: field sizes 80/96,
  256×512, seven colonies, maximum hedges, no extra gateways, fully wooded
  boundaries, all resource sliders at 0%/300%, seeds 30001–30004 and 40001–40004.
  The original seed 30002 failed at both resource extremes before the fix;
  those two failure records remain available.
- In the 132-case sweep, the furthest starting wheat/wood was 10/14 walking
  steps away; the minimum connected 4×4 placement count was 564. In the 32-case
  sweep those figures were 14/17 steps and 935 placements. Placement origins
  overlap; these are not counts of independent buildings. Larger farms consume
  more village ground than the original design.
- Default-field generation averaged 54 ms and peaked at 243 ms in the Linux
  sweep. These are observed wall times on the shared host, not a controlled
  before/after performance benchmark.
- The C++ default-generator suite passed, including dry hedges, access to every
  field, equal farm stocks at resource extremes, telemetry isolation through the
  placement search, invalid-size rejection, unfair-start fixtures and the new
  wide-field regression. The maximum 512×512 / 12-colony layout also generated.
- **256/256** macOS ARM64 golden rows passed. All eight Hedgerow default hashes
  match revision 4 after its revision-only table update; other generator hashes
  remain unchanged. `default-continuity.json` separately confirms identical
  default terrain, resources, starts, room and movement reports for seed 19.
- Revised initial-map and loaded-final-save previews were inspected. The isolated
  test workers were stopped after their work was acknowledged; sources, immutable
  builds and evidence remain available for review.

See `study-summary.json`, `final-fields-summary.json`, `contracts5.log`,
`golden5-check.log`, `golden-continuity.txt`, `retry-continuity.json` and
`host-finish.json` in the evidence root. The updated playable file is
`artifacts/hedgerow-country/Hedgerow Country.map`; its adjacent PNG/JSON are current.

## Interpretation limits

This is an initial AI study, not proof of human balance. Two AI styles and a small
seed set cannot establish every colony count or strategy. Earlier fighting also
makes final population an unreliable standalone fairness metric: starvation,
combat, births and physical starting position must be read together.

The tests establish repeatable generation with telemetry on/off, dry hedges,
connected routes and fields, equal home stocks at resource extremes, supported
size rejection, and retained regression seeds. Golden verification covers macOS
ARM64. Linux games are real headless executions on x86-64. Cross-platform
per-tick simulation equivalence and Windows execution have not been established.
