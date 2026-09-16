# Fractal map validation and tuning

The artifact index is `artifacts/fractal-maps/INDEX.md`. Implementation and the
practical completion checks below are finished. The larger original **gameplay**
tournament plan is only partially executed; planned and pending jobs are not
results. The final full-range **generation** sweep is reported separately below.
Nothing has been merged. Gameplay is localhost macOS ARM64; the explicitly named
Linux checks use x86-64 Docker emulation.

## Implemented and checked

Both generators are registered at revision 1: Gardens ID 49 and Hilbert ID 50 (they took 34 and 35 first, which went to Emoji and Forts).
ID 33 remains retired. Earlier prototype bundles used different IDs; interpret each
record with its own immutable bundle catalog.

- Optimized contracts passed after the shared-framework extraction. Fixtures cover
  exact recursive subdivision, rectangular Hilbert paths and orientations, bounded
  failures, deterministic crossing selection, endpoint connectivity, torus seams,
  crop containment, and disjoint building arrangements with circulation.
- Gardens 128×256, four and five colonies, seed 20001 regressions passed. A retained
  five-colony preview shows protected home irrigation and both causeway pairs.
- `custom-setup-framework.log` passed: both new maps generated, ran real 500-tick
  matches, and played 250 ticks of their replays with engine checksum assertions.
  The toolkit separately verifies zero-ambient-resource map serialization.
- Earlier optimized telemetry runs covered 99 catalog cases with identical worlds
  and RNG state when reporting was enabled/disabled. The framework recheck also passed all 99 cases: generation totals were
  33,718.104 ms off and 32,979.571 ms on under the shared host load. These timings
  exclude report export and do not establish a small performance difference.
- The preceding golden check compared 264 macOS rows without failures, including
  all 248 existing rows unchanged. The framework candidate also compared all 264 rows successfully; only its new
  Hilbert 512×512 seed-1 fingerprint changed when irrigation entered the crossing graph.

## Build identities

All bundles retain their executable/data manifest, catalog, source digest, dirty
source files, and tracked patch under the named build directory.

| Build | Bundle SHA-256 | Purpose |
| --- | --- | --- |
| `pilot-build` | `ec366b709717e74a101390e0c2c0fe39433cb0bac4a225102e29a4af771c785c` | Initial all-AI coverage and arena controls |
| `registered-build` | `d5ffc749c3f66eaf00bd6b9a22f906b5568d58f00f16314ce582a39a39f59ab1` | Registration audit and broad geometry diagnosis |
| `framework-build` | `eb4e84af9eea32c10629e179ba61cfd86c08bdf5c185792527be811d52178ee0` | Shared site/resource/room operations and protected causeways |

The source base is `c8a24ba3e01a4005604247cb19ba168504055d60`; each bundle adds its
retained dirty-source snapshot. Superseded exploratory bundles remain indexed.

## Geometry findings and changes

Initial probes reserved too many Gardens districts and over-budgeted Hilbert
clearance. Preferring complete home districts and using the actual module envelope
restored smaller lakes and second-order folds at 256×256. Geometry can stop maximum
nesting early; the requested number is not a promise to cut through homes.

Narrow rectangles exposed a crossing-graph omission: outer-bank distances alone
ignored the island objective. Required island connectivity now precedes optional
shortcuts, while floods retain all seam and end-around routes. Both 128×512
orientations passed 1–3-pair probes at seed 31001 in the earlier corrected build.

The registered-build matrix then found bridges overwritten by late home irrigation.
The framework candidate stamps irrigation first, protects water/crops/courts with
exact raster collision tests, and offers a bounded angular search when the usual
three approaches cannot fit. A shared compatibility predicate keeps both shores
apart. It does not flatten lakes, clear farms, or silently retry seeds.

Finished-world scoring measures renewable resource frontage, opening quarry access,
expansion room and walking/swimming contact before randomly dealing sites. A spare
site is considered only when it fits. Sand containment and protected grass service
aprons keep the construction court clear after crops spread. Ten separate building footprints must retain access after
all proposed buildings are blocked in the walking mask.

The original full matrix plan includes training seeds 20001–20016, held-out seeds 30001–30008,
all 128/256/512 axis combinations and 2–8 colonies, plus control/resource/worker
boundaries. Candidate selection is disabled. Dense small maps can be unsupported;
`support.csv` distinguishes rejected requests from accepted-generation failures and
lists mixed seed outcomes. The earlier diagnostic matrix was superseded; the final
immutable-bundle bulk matrix and its seed-by-seed support envelope are reported below.

## Gameplay findings so far

Initial coverage is explicitly selected in `initial-coverage-selection.json`:
12 completed Econo/Numbi/Warrush games from the original study plus a 20-game
continuation for Castor/Cortex/Cabino/Nicowar/Maxima. Each AI has both generators and
both seat rotations, map seed 20001, game seed 19, and a 90,000-tick cap. Continuation
omits optional debug sidecars but retains initial/final saves, replays and timelines.

One initial Econo/Hilbert rotation ended at tick 14,146 with no observed combat or
conversions. Both colonies peaked at six units, each had one opening starvation
death, and their inns were undergoing upgrades. The other rotation grew to peaks
71 and 51. Saves and a visual replay inspection support an opening-economy diagnosis;
an early engine end alone would not establish its cause.

The two Symmetric arena Econo controls both reached the 90,000-tick cap. Their four
seats peaked at 305, 406, 422 and 582 units with zero opening starvation deaths.
This demonstrates that the same AI can expand on the control; it does not isolate
which map parameter explains the Hilbert result. A focused six-tile northward starting-swarm offset is under paired test for Econo
and Cortex, with all terrain, crops and AI policy unchanged. It passed contracts,
and diagnostic games retain intermediate saves every 5000 ticks. The broad paired
cohorts were superseded before gameplay so tuning can be evaluated first.

Warrush also failed to establish food on the arena control: all four seats recorded
one opening starvation death and no opening meals; three seats recorded no meals
throughout the game. This supports an AI limitation extending beyond these maps,
without proving that every map interaction is harmless.

The Econo controls finished while debug-output compression exhausted the local spool
budget. Their collection leases expired. Both complete late attempts are retained
and analyzed explicitly (`--attempt`), never silently counted as committed results.
Two automatically launched duplicate retries were stopped and remain separately
identified interruptions. Faster lossless packing and verified hard-link staging
used the standard artifact acceptance/acknowledgement path; raw hashes are retained.

Four-colony Nicowar/Maxima mirrors and final all-AI held-out games were planned but
are not completed in this delivery.
Capped games are reported separately from engine victories. Complete seat blocks,
per-team food/population/building timelines and missing telemetry remain visible.
No pooled win rate is being used to claim balance or human fun.

## Reproduction

```sh
scons release=1 server=0 -j3 map-generator-defaults-test map-generator-golden-test custom-setup-test build/src/glob2
build/src/MapGeneratorDefaultsTest artifacts/fractal-maps/test-profile
build/src/MapGeneratorGoldenTest artifacts/fractal-maps/golden-framework-profile --require-rows
env GLOB2_USER_DIR="$PWD/artifacts/fractal-maps/replay-framework-profile" build/src/CustomGameSetupHarness
python3 -m tools.fractal_maps.snapshot artifacts/fractal-maps/new-build
python3 -m tools.fractal_maps.plan --bundle BUNDLE --output PLANS
python3 -m tools.tournaments submit MANIFEST RESULTS --bundle BUNDLE
python3 -m tools.tournaments run RESULTS --hosts artifacts/fractal-maps/hosts.json
python3 -m tools.fractal_maps.analyze RESULTS --output ANALYSIS
```

Host settings allocate two game slots and one separate geometry slot, at most three
engine workers. Disposable profiles and immutable bundles isolate runs. Generation
time is separate from report export; throughput under this heavily shared host is
not an isolated performance benchmark.

## Remaining acceptance limits

Initial all-AI coverage is complete (32 selected games, both seat rotations).
Following the user’s request to prioritize practical completion over perfection,
the gameplay checks focus on default seed coverage, short paired opening cases,
and compatibility verification. Further tuning, the four-colony cohort and the
all-AI held-out **games** are not completed. The separate final bulk generation
matrix is reported below. Reproducible AI opening failures
are reported as limitations, not hidden or treated as passing balance evidence. Human play remains necessary to judge pacing and fun.

No simulation, AI policy, save format, replay/network version, or engine economy
was changed. Cross-platform execution checksums and new Linux golden rows have not
been verified; local save/replay success does not establish platform equivalence.

## Opening-economy diagnosis (ongoing)

The six-tile swarm offset did not remove all opening collapses. Econo's Hilbert
rotation 0 ended at tick 9410: a saved-state inspection found the sole inn closed
for an upgrade, four of eight wood delivered, and a 26-tile timber haul. The next
immutable candidate moves wood into the outer bays beside the grain frontage.
Its first same-seed Econo Hilbert game still ended at tick 8130; shorter timber
access alone is insufficient. Both candidates retain diagnostic artifacts; their
unfinished seat blocks are not counted as complete paired evidence.

A Cortex checkpoint at tick 5000 showed one colony without an inn. Its placement
policy requires the grown inn footprint immediately beside wheat; the sand gap
between the original crop strip and main court does not provide that frontage.
A subsequent candidate adds a grass service apron protected by the existing saved
resource-growth restriction. Runtime growth and round-trip fixtures exercise the
shared operation. This candidate subsequently passed the practical completion checks reported below.

The read-only inspector and states are under `artifacts/fractal-maps/inspection/`.
Linux x86-64 under Docker emulation passed the optimized contract suite and all
248 existing golden rows for the pre-timber transverse candidate. These are build
and generator checks; matching cross-platform simulation execution is unverified.

### Complete initial coverage summary

Selected immutable pilot build `ec366b709717e74a101390e0c2c0fe39433cb0bac4a225102e29a4af771c785c`: seed 20001, engine seed 19, 256², two colonies, both rotations. Each row contains two games and four colony observations.

| AI | Map | Caps / 2 | Peak population range | Opening starvation range |
| --- | --- | --- | --- | --- |
| cabino | hilbert-river | 1 | 19–86 | 1–2 |
| cabino | sierpinski-gardens | 2 | 19–19 | 2–2 |
| castor | hilbert-river | 2 | 13–37 | 0–0 |
| castor | sierpinski-gardens | 2 | 12–28 | 0–0 |
| cortex | hilbert-river | 0 | 7–7 | 1–1 |
| cortex | sierpinski-gardens | 0 | 6–7 | 1–1 |
| econo | hilbert-river | 0 | 6–71 | 0–1 |
| econo | sierpinski-gardens | 0 | 56–69 | 0–1 |
| maxima | hilbert-river | 0 | 65–112 | 0–0 |
| maxima | sierpinski-gardens | 0 | 64–75 | 0–0 |
| nicowar | hilbert-river | 0 | 48–82 | 0–0 |
| nicowar | sierpinski-gardens | 0 | 41–58 | 0–0 |
| numbi | hilbert-river | 2 | 31–47 | 0–0 |
| numbi | sierpinski-gardens | 1 | 25–37 | 0–0 |
| warrush | hilbert-river | 0 | 8–8 | 0–0 |
| warrush | sierpinski-gardens | 0 | 8–8 | 0–1 |

All 16 seat blocks are complete, with no missing gameplay telemetry in this summary.
These observations cover one map seed and one engine seed; they do not estimate a
reliable population-wide balance rate. The pilot predates subsequent tuning. Exact
selected job IDs and per-seat results are in `initial-selected-jobs.json` and
`initial-combined-analysis/` under the artifact index.

## Practical completion scope

The delivery candidate is `service-apron-build`, bundle
`75e3ede47341117101794d5fd357252844585626914a56ae1e36783c38921102`.
`service-defaults` checks both generators at 256²/four colonies for all 24 planned
training and held-out seeds. `service-opening` checks Econo and Cortex on both maps,
both rotations, seed 20001/game 19, with a **20,000-tick opening cap**. This shorter
check is not the originally planned 90,000-tick final tournament. Older tuning
queues are draining; unfinished rotations are explicitly incomplete evidence.

### Delivery contract and telemetry results

`contracts-service-full.log` passed the optimized suite. The growth fixture runs the
actual engine growth operation beside a wrapped protected grass mask; unprotected
wood spreads and protected cells remain empty. The final-world fixture also checks
that serialized growth flags survive loading. `custom-setup-service.log` passed
both generators through local real games and replay playback.

`telemetry-service.csv`: **99 cases generated, zero semantic failures**, identical
serialized worlds and RNG with telemetry off/on. Summed generation times were
61,189.925 ms off and 61,579.602 ms on. Export time is excluded; the shared host load
makes these unsuitable for claiming a small performance regression or improvement.

Cortex's two 90,000-tick Symmetric arena control games both capped, with all four
colonies peaking at 261–392 units and zero opening starvation. Its initial fractal
failures therefore warrant a map-layout diagnosis rather than the Warrush control
explanation. Raw control results remain in `cortex-control-analysis/`.

## Final practical-completion results

- **48/48 default maps generated successfully**: both generators, 256×256, four
  colonies, all seeds 20001–20016 and 30001–30008, candidate selection disabled.
  There were no generation failures, rejected defaults, crashes or missing reports.
- **8/8 opening games reached the 20,000-tick cap**, covering Econo/Cortex, both
  maps and both rotations, map seed 20001/game seed 19. All four seat blocks and all
  16 colony timelines are complete. These caps are not engine victories.
- All 496 pre-existing golden rows (248 each for macOS and Linux) remain byte-for-byte
  unchanged. The current macOS run measured 264 rows and refreshed only the 16 new
  generator rows; 14 changed during tuning. New Linux fingerprints for the final
  service-apron candidate are unverified.

| AI | Map | Peak population range across four seats | Starvation deaths by tick 20,000 |
| --- | --- | --- | --- |
| Cortex | hilbert-river | 29–40 | 0–0 |
| Cortex | sierpinski-gardens | 24–34 | 0–1 |
| Econo | hilbert-river | 45–54 | 0–1 |
| Econo | sierpinski-gardens | 30–54 | 0–3 |

The service apron resolved the observed early-collapse pattern in these paired
opening cases. This does not prove longer-term balance, behavior for every AI on
the final build, or human fun. Earlier all-AI results still identify risks: Warrush
food failures also occurred on its arena control; Cabino sometimes stalled badly;
Castor often capped. No additional tuning is claimed after the user's request to
prioritize practical completion.

Current previews and a 20,000-tick Hilbert save are in `service-previews/`. Visual
inspection shows retained folds, causeways, separated farm fronts and clear home
courts, including a home spanning the left/right seam. The initial pilot included
an observed replay; there was no human gameplay assessment of the final candidate.
Cross-platform simulation checksum comparison remains unverified. Final queued
superseded work is paused/cancelled, and task-owned idle workers were shut down;
`shutdown-audit.json` records those processes.

## Final full-range bulk generation

The immutable service-apron bundle
`75e3ede47341117101794d5fd357252844585626914a56ae1e36783c38921102`
ran **3,616 committed localhost generation jobs** with candidate selection
disabled. Training seeds were 20001–20016 and held-out seeds were 30001–30008.
The default grid covered both generators, all 128/256/512 width-height pairs, and
2–8 colonies. Separate cohorts swept control bounds, resource amounts 0/100/300,
worker counts 1/4/8, and exposed 64-tile or one/twelve-colony controls. This is a
boundary matrix, not every Cartesian combination of every control value.

| Cohort | Requests | Generated | Explicitly rejected | Other failures / missing |
| --- | ---: | ---: | ---: | ---: |
| Training default grid | 2,048 | 1,695 | 353 | 0 |
| Held-out default grid | 1,024 | 849 | 175 | 0 |
| Control/resource/worker boundaries | 284 | 272 | 12 | 0 |
| Registered control edges | 260 | 63 | 197 | 0 |
| **Total** | **3,616** | **2,879** | **737** | **0** |

The in-range default grid generated **2,544/3,072 requests (82.8%)**. The
128/256/512-by-2–8 support envelope deduplicates the additional identical
baseline request per seed: **89/126 combinations passed all 24 seeds**, 16
rejected all 24, and 21 had mixed seed outcomes. **55/56 combinations with a
shorter side of at least 256 tiles passed every seed**; the sole mixed setting was
Hilbert 256×256/eight colonies, which rejected seed 20008 because no crossing
court fit outside reserved homes. Both 256×256/four-colony defaults passed all
24 seeds each. The other mixed settings had a 128-tile short side. No repeated
baseline request disagreed with its mate.

Rejections are geometric diagnostics before a finished world is accepted: 184
edge requests were outside the agreed size/team target, 171 lacked a legal
crossing court, the crowded remainder mostly could not fit the requested
separated 56×56 home/farm modules, and two Gardens requests lacked distinct
useful causeway approaches. Thus some settings inside the broad target range
are unsupported or seed dependent. `default-envelope.csv` identifies their
exact sizes, team counts, and seeds; the generators do not silently replace a
failed seed. No `generation_failed`, engine crash, timeout, or missing committed
report occurred in these cohorts.

`bulk_inspect` applied hard checks to every successful finished-map report:
exact colony count, one walking-land component, finite walking routes among all
colonies, protected crop-growth service aprons, reachable wheat/wood/stone, and
at least ten complete nearby 4×4 construction anchors per colony. It also
selected soft outliers for unusually long starter hauls, low fairness/quality,
or excessive water. **No hard or soft flags were raised** across 2,879 generated
maps. These cheap report checks do not prove every game will run well.

The final `service-bulk-worst-final/` selector reconstructed ten successful
maps and previews from the same immutable bundle: the lowest generic quality,
lowest fairness, smallest expansion catchment, wettest terrain, and longest
walking pair for each generator. All rebuilds succeeded. Visual inspection found
the designed water, causeways/bank courts, separate homes and farms, and torus
seam land routes intact. The lowest fairness was 0.886 Gardens / 0.874 Hilbert;
the smallest measured catchment held 798 / 788 complete 4×4 anchors. The
longest colony walking pair was 255 / **443 tiles**. That Hilbert 512×256,
eight-colony route may slow contact; swimming can offer alternatives, but this
visual check is not a human playtest or tournament balance claim. The zero-
ambient-resource/one-worker cases produced the lowest generic scores, while
retaining reachable guaranteed starter supplies.

The PR includes default 256×256/four-colony engine screenshots in
`docs/map-generators/images/` and the compact reviewer-visible
`docs/map-generators/evidence/` summaries. The indexed local artifact directory
retains submitted manifests, reports, reconstructed `.map` files, previews,
logs, initial/final saves, replays, and gameplay timelines. Reproduce the bulk
summary and previews with:

```sh
python3 -m tools.fractal_maps.plan --bundle artifacts/fractal-maps/bundles/75e3ede47341117101794d5fd357252844585626914a56ae1e36783c38921102 --output artifacts/fractal-maps/service-bulk-plans
python3 -m tools.fractal_maps.bulk_inspect artifacts/fractal-maps/service-bulk-boundaries artifacts/fractal-maps/service-bulk-registered-edges artifacts/fractal-maps/service-bulk-training artifacts/fractal-maps/service-bulk-held-out --output artifacts/fractal-maps/service-bulk-inspection
python3 -m tools.fractal_maps.bulk_support artifacts/fractal-maps/service-bulk-inspection/jobs.csv --output artifacts/fractal-maps/service-bulk-inspection
python3 -m tools.fractal_maps.render_worst artifacts/fractal-maps/service-bulk-boundaries artifacts/fractal-maps/service-bulk-registered-edges artifacts/fractal-maps/service-bulk-training artifacts/fractal-maps/service-bulk-held-out --bundle artifacts/fractal-maps/service-apron-build/supplied --output artifacts/fractal-maps/service-bulk-worst-final
```

Three localhost engine slots served these cohorts. Reports and result collection
are separate from map generation time, and concurrent studies on this shared host
make throughput an unsuitable performance benchmark. The implementation avoids
engine/save/replay/network changes. Local save/load and replay checks passed; a
cross-platform per-tick checksum comparison for the final code remains unverified.

## Rotation tournaments

Six 256×256 maps (seeds 101 to 106), four colonies, every cyclic team rotation, four Nicowars,
45,000 ticks, on each map at its defaults; every game reached the tick cap.

- **Sierpiński Gardens** is food-capped. Pooled per-start units of 20 to 30 (peaks 35 to 42), ten
  buildings, three or four warriors, 13 to 17 starvation deaths per colony, 23 eliminations in 96
  colony-games, about 123 wheat and 490 wood harvested per colony in the whole game, no prestige.
  Position bias 17 points (not significant). The home module's sealed wheat and wood plots are
  worked the way every sealed-plot map in the framework is: a Nicowar colony harvests little of a
  small capped plot whatever its density, and under about 150 wheat in 45,000 ticks a colony never
  grows. Whether the map is meant to be played at that pace, or its plots should open toward the
  district's building ground, is the design question for a maintainer.
- **Hilbert River** grows more (pooled per-start units 23 to 48, peaks 38 to 62, 180 wheat and
  620 wood harvested per colony, eight eliminations) but unevenly: the map's colony index 1 was
  the weakest start on five of the six maps and won none of 24 games (position bias 28 points,
  p = 0.004). The home modules are identical by construction and the finished maps' start
  metrics (reachable tiles, catchment, building sites, quality) show no index pattern, and sites
  are dealt at random, so what the index sees is not in the geometry the generator measures; it is
  recorded as an open question for a larger sample and other AIs, as Drumlin field's index 2 is.
