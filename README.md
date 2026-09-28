Source commit: `ee3b010be1660976ceb697d949a029588cb5a595`

Download [the evidence archive](map-food-evidence.tar.gz) and extract it at the corresponding source checkout root. It contains reproducible requests, initial maps, native test logs and raw study results. Representative final saves are individually compressed under [saves/](saves/). No executable binaries are included.

[City States before/after](previews/city-states.png)

# Map food and landscape revisions

Review evidence for `codex/city-states-wheat`. These generated artifacts are local and ignored by Git. Earlier v15 pond-module designs are superseded.

## Representative maps

Seed 7, 256 × 256, four colonies, default controls, one candidate. Counts describe initial stock, not delivered food or renewable capacity. Open dry wheat sections are finite reserves. Renewable food remains in the irrigated fields.

| Map | Wheat before → after | Wood before → after | Water before → after |
| --- | ---: | ---: | ---: |
| Orchard Commons | 488 → 2255 | 2511 → 2428 | 9.2% → 12.0% |
| The Last Treeline | 384 → 6433 | 709 → 709 | 5.1% → 10.3% |
| The Comb | 726 → 1888 | 351 → 336 | 27.8% → 27.8% |
| The Faulted City | 1153 → 1242 | 345 → 1118 | 1.4% → 5.1% |
| Portage Lakes | 640 → 4262 | 18687 → 18740 | 9.8% → 9.8% |

[Before/after previews](previews/maps.png)

Orchard uses its outer streams, woodland and open grain country. Treeline retains its timber budget and adds outer wetlands and grain. Comb retains its coast and uses spare land for grain. Faulted City keeps the accepted irrigated city gardens. Portage plants more wheat within existing shore fields, leaves service courts in neutral fields, and seeds open woodland gaps while protecting cleared approaches. City States covers every plantable islet tile outside its central building plot with wheat.

## Structural validation

The exact logs below are the authority for completion and failures. Earlier failed iterations remain as diagnostic history and are not final validation.

### toolkit-v17.log
- PASS shared toolkit: floods, sketch, planting, roads, settlements, balanced starts, walls, stencil turns, routes, scatter, lattice noise, wedge frame, shuffle, drawing, branches, stretch, sand patches, algae growth, fields, clumps, walls, tower reach, territories, arena primitives, sealed lines, polygons, tessellations, warp, graph mazes, shortcuts, cell crossings, region labels, gate partitions, region leaks, grained landforms, teardrop homes, near trees and colony leaks
- PASS toolkit-only geometry, raster, resource and home contracts

### orchard-v17-tests.log
- PASS feeding courts at wheat 0/100/300, both harvest parities, three seeds

### treeline-v19-tests.log
- PASS Last Treeline: shapes, colony counts, refusals, telemetry determinism, extremes, dry wood and crop separation

### comb-v18-tests.log
- PASS Comb full-save repeatability, telemetry, save/load, food-loss and invalid requests
- PASS feeding courts at wheat 0/300, both harvest parities, three seeds
- PASS cross-channel fire tower=(60,55) target=(67,54) range=7 stone-consumed=1 shots=1; empty-ammo control=pass damage=pass target=unit
- PASS cross-channel fire tower=(105,64) target=(113,72) range=7 stone-consumed=1 shots=1; empty-ammo control=pass damage=pass target=building

### faulted-v10-tests.log
- PASS Faulted City: envelope, refusal, resource extremes, growth containment, stable resource terrain, narrow junction, masonry and token food mutations

### portage-v22-tests.log
- PASS Portage Lakes: compact/full, repeatability, growth containment, missing portage, abundance extremes

### golden-v22.log
- Wrote 544 rows for macos-arm64 to test/map-generator-golden.txt

City States evidence is in `artifacts/city-states-wheat/`: 45 maps and 975 islets, all non-plot plantable islet grass wheat, central plots clear, with a negative control against the old generator.

## Randomized cohorts

The four older-map cohorts reuse 500 requests each from `sweep-v6.metadata.json`. Portage has a separate deterministic 100-request matrix in `portage-v22-requests.json`. Each raw row retains request, outcome, timing, final map metrics and telemetry. Timing is from a shared loaded development machine.

| Map | Records | Accepted | Refused | Execution errors |
| --- | ---: | ---: | ---: | ---: |
| Orchard Commons | 500 | 497 | 3 | 0 |
| The Last Treeline | 500 | 500 | 0 | 0 |
| The Comb | 500 | 500 | 0 | 0 |
| The Faulted City | 500 | 500 | 0 | 0 |
| Portage Lakes | 100 | 100 | 0 | 0 |

Orchard Commons non-completions:
- Seed 10113: orchard-commons revision 2 seed 10113 [orchard resources]: An orchard home farm lacks opening inn room beside its grain.
- Seed 10119: orchard-commons revision 2 seed 10119 [generator validation]: An orchard colony lacks building and upgrade room.
- Seed 10496: orchard-commons revision 2 seed 10496 [orchard resources]: An orchard home farm lacks opening inn room beside its grain.

## AI games and limits

Calibration uses four homogeneous four-colony games per map, Nicowar/Maxima/Cortex/Cabino: map seed 7, game seed 1, 25,000 ticks. Final maps use Orchard/Treeline/Comb `v18-*`, Faulted `v11-64-*`, Portage `v22-65-*`; baselines use `before-*`. The first baseline Portage Cabino run aborted during translation loading; the successful retry occupies `before-65-cabino`, with the original failure retained as `before-65-cabino-initial-error`. The Portage v18 games predate the final approach protection and are not final evidence.

Held-out mixed-AI rotations use map seed 401, five candidates, game seed 19, four rotations, 15,000 ticks, with Nicowar/Maxima/Cortex/Cabino. Requests, maps, logs and final saves are in `rotations-final/`. Generation binaries v19 for IDs 58/70/62/64 and v22 for ID 65 contain their final map algorithms.

Read `economy-summary.json` and raw team telemetry per AI: resource increases do not guarantee each AI performs better. Cortex still fails some openings, especially Faulted City. These runs cannot establish human fun or universal balance.

Human playtesting and Windows checksum comparison remain unperformed. The subsequent PR checks below add native Linux coverage and a matching macOS/Linux simulation trace. Generator revisions intentionally alter newly generated maps. Engine simulation, save formats and existing saves are unchanged. Darwin golden records are updated only from native output; Linux goldens were subsequently imported from native output, as recorded below.

## Reproduce

From this worktree:
```sh
CCACHE=1 scons -j4 release=1 server=0 build/darwin/client/release/src/glob2 map-generator-defaults-test comb-generator-test map-generator-golden-test orchard-conversion-test
build/darwin/client/release/src/MapGeneratorDefaultsTest . --toolkit-only
build/darwin/client/release/src/MapGeneratorDefaultsTest . --treeline-only
build/darwin/client/release/src/MapGeneratorDefaultsTest . --portage-lakes-only
build/darwin/client/release/src/MapGeneratorDefaultsTest . --faulted-city-only
python3 artifacts/food-redesign/probe.py v22 artifacts/food-redesign/v22-glob2 65
python3 artifacts/food-redesign/recheck.py artifacts/food-redesign/v19-glob2 orchard-v19 58 500
python3 artifacts/food-redesign/portage-sweep.py artifacts/food-redesign/v22-glob2 portage-v22 65 100
python3 artifacts/food-redesign/rotations-final.py artifacts/food-redesign/v22-glob2 65
```

Durable lessons live in the existing map-design skill, tuning playbook, game-rules handbook, framework manual and map guides. This review report remains uncommitted.

## Delivered food in paired 25,000-tick games

Each entry totals four colonies running the same AI. One seed per map: these are observations, not universal balance claims.

| Map | Nicowar | Maxima | Cortex | Cabino |
| --- | ---: | ---: | ---: | ---: |
| Orchard Commons | 1506 → 2483 | 2193 → 1993 | 1436 → 1425 | 1767 → 2218 |
| The Last Treeline | 3298 → 3646 | 2119 → 2383 | 2399 → 3972 | 2411 → 3406 |
| The Comb | 4194 → 4507 | 2352 → 3027 | 4991 → 4625 | 2566 → 2924 |
| The Faulted City | 1576 → 2484 | 1964 → 2321 | 649 → 982 | 1656 → 1911 |
| Portage Lakes | 2219 → 3514 | 2102 → 2387 | 1661 → 3799 | 2144 → 3187 |

Treeline delivered more food and produced more warriors with all four AIs in the paired run. Portage delivered more food with all four; warrior births increased for three and were similar for Maxima. Orchard improved for Nicowar and Cabino but declined for Maxima and slightly for Cortex; Comb declined for Cortex. Faulted City delivered more food for all four, but Cortex still failed its wood/construction opening and produced no warriors. Raw counters include starvation and combat deaths; increased population can still raise total starvation.

Held-out rotation details are in `rotation-summary.json` and `rotation-summary.txt`. All 20 games reached 15,000 ticks. Cortex had weak starts on Orchard and one stalled Comb opening; Faulted City retained Cortex starvation. Other starts supported growing armies. No win-rate conclusion is drawn from these short, unresolved runs.

## Native Linux and PR verification

Final source commit: `47c53e94c7511115b4c4b4955cfcad92c819be4e`. The map algorithms are unchanged from the initial evidence; PR verification also initializes the Orchard conversion fixture's engine wait flag and asserts all 64 requested ticks execute.

Native Linux x86-64 produced 544 golden rows. The import refreshes the revised generators and previously missing/stale Linux records already present in the base branch (Glacis, Rice Terraces, and four missing newer generators). Every changed existing row has a newer registered revision; no unchanged-revision output drift was accepted. Raw output: [linux-goldens.log](platform/linux-goldens.log). Native Portage, Treeline, toolkit and Orchard logs and the compiler version are in [platform/](platform/).

The same saved four-AI initial Portage game (map seed 7, game seed 41, Nicowar/Maxima/Cortex/Cabino) ran for 6,000 ticks on macOS ARM64 and Linux x86-64. Full checksum files are byte-identical: SHA-256 `a1d412d05c4bb1833309385f51ff359019c8c02a113bf4e5283c901851101bf5`. The saved initial state and both checksum traces are included. This verifies that run, not every possible state or Windows execution.

Reproduce after decompressing `platform/initial.game.gz`:

```sh
glob2 --run-game --load-game initial.game --ticks 6000 --telemetry checksums --output-dir compat-result
```
