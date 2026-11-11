# Implementing and verifying the design

Source anchors: [adding a generator](../../../../docs/map-generators/adding-a-generator.md), [framework](../../../../docs/map-generators/map-generator-framework.md), [development conventions](../../../../docs/development/conventions.md). Consult these for full interfaces; the guidance below connects them to design decisions.

## Worked design: a city around contested gardens

This is a proposed application of the toolkit, not an existing generator or a validated balance recipe. Suppose the user wants walled city districts with a fertile common garden: a safe opening, then fights over expansion space and food.

Start with the decisions. Each colony must establish its economy within a district, but a growing colony should benefit from leaving it. Give the common garden enough usable grass and accessible crop edges to reward occupation. If all surplus is behind one permanent tower-covered gate, the map encourages waiting rather than contest; provide a flank or another relevant objective. A terrain perimeter (Obstacles, or Ridges where towers should fire across) makes the playable shape durable, and paved streets (Paths) keep access open and stay buildable; the older generators used a stone perimeter, which is also a quarry, and sand streets, which consume building land ([terrain catalogue](terrain-catalogue.md)).

Represent `homeOf`, playable ground, structural walls, roads, crop plots and objective masks separately in a layout. Choose a lattice or region partition suitable for the rectangle and team count. For exact supported groups, construct one domain and transform it; for three or five teams, allocate approximately even regions and measure the finished result. Reserve home building capacity with `Room`, then account for the swarm, worker ring, crop containment and street corners. The home area's tile count is only a rough initial budget.

Place water to sustain the garden and starter plots, and use `Growth` to check whether that water also makes streets or reserved expansion grass fertile. A dry building district with a contained irrigated plot is one possible solution. With terrain walls, put them on vertices with `wallVertices` (keeping streets and gates open), seal any coast with `cliffCoast`, and draw them after any beach pass, which would sand them. With stone walls, build beaches and derive final pure tiles before placing stone: a sand road of vertices that meets a coastal beach can spoil wall grass and create a breach. Check both sides of every gate and the torus seam.

Make district borders, street orientation, garden compartments and crossing locations vary within spacing and access limits. Change which expansion route is attractive, while preserving an opening for every colony. Add controls such as district room budget, crossing width and garden allocation, plus the applicable resource amounts. Scale ambient crops and deposits; leave the opening guarantee and structural perimeter deliberately fixed. At maximum abundance, validate that the common garden still has gathering edges and the home district still has building space.

Validate post-furnishing home capacity, protected-wall continuity, exits, target access and contact costs. Measure travel to the garden as well as nearest rival distance; a geometrically central garden can still favor one district. Test held-out seeds, rectangles and odd counts. In games, record whether AIs build beyond the swarm, whether crop spread closes the approaches, whether the first fighter arrives before rivals can establish food, and whether occupying the garden offers an advantage with counterplay. If the garden supplies no useful expansion, revise its purpose rather than keeping it as decorative symmetry.

## Compose mechanisms instead of copying recipes

| Design need | Shared code to inspect | Decision it supports |
| --- | --- | --- |
| Wrapped coordinates, distances and connectivity | `Grid`, `Topology` | The map is a torus. Choose cardinal or eight-neighbor connectivity explicitly; check seams as well as the image centre. |
| Shape, stroke, noisy border, region | `Geometry`, `Drawing`, `Morphology`, `Sketch`, `Wedge` | Budget transformed maximum extent and final pure tiles, not just the nominal radius. |
| Fair repetitions | `Orbits`, `LatticeNoise` | Preserve tile/corner orbits, including deposit amounts and colony footprints. |
| Organic structure | `HeightMap`, `Noise`, `Patterns`, `Points`, `Tessellation`, `GraphMaze`, `Biomes` | Let correlated fields shape coherent regions; avoid independent speckle that destroys building space. |
| Real geography or a picture | `WorldAtlas`, `Raster`, `Landmass` | Fit with a sea margin so the seam stays open, resample by majority, clean slivers and specks the beach pass would waste, and choose sites on the mainland by walking distance (`farthestSites`). |
| Natural starts balanced on a shared objective | `Points` (`farthestSites`, `closestWalk`), `Growth` (`waterDrySite`, `digPond`, `meanFertilityAround`), `Territories` (`growTerritories`) | Spread by walk within a walk band to the prize, keep the widest spread (`closestWalk`), water the starts that landed dry, and keep what you dig off the routes you balanced. |
| Renewable plots and growth containment | `Growth`, `Farmland`, `Planting`, `Resources` | Choose water and dry buffers before planting; stock only terrain that can sustain the intended role. On catalogue maps, `dressFarmSand` turns a farm's sand into barren rings and paved crossings. |
| Homes and finite building capacity | `Homes`, `Room`, `Territories`, `Settlements` | Measure complete footprints, fair regional capacity, and space for all starting workers. |
| Terrain groups beyond grass, sand and water | `Sketch` (`composeLooks`, `walkableTiles`), `Rock` (`rockVertices`, `quarryNear`, `quarryApproach`, `talusApron`, `quarryFault`), `HeightFieldLooks` (`deepenWater`, `distanceFromWet`, `layLevee`, `paveBeyondWater`, `homeGardens`, `partingBarrier`) | Keep mechanics canonical and looks separate; draw walls that block exactly their tiles, faces that hold stone, deep cores with shallows. See [the terrain palette](terrain-palette.md). |
| Fronts, shortcuts and defense | `Channels`, `Roads`, `Contact`, `Walls`, `Towers` | Measure actual entry costs and firing envelopes; preserve structural boundaries during repairs. Terrain walls: `wallVertices`, `cliffCoast`, `paveTiles`, `leakWithShut`, `wallTerrainMismatch`; stone walls: `sealCoasts`, `designedStone`, `wallStanding`; quarry faces at a terrain wall's foot: `plantQuarryFaces` (`Planting`). |
| Terrain looks and themed bases | `Climates` (`drawClimate`, `paintClimate`, `barrenBase`, `paveCore`, `deepSea`) | Design on one terrain per rule and paint a per-map climate last, so looks never change rules ([design on mechanics](terrain-catalogue.md#design-on-mechanics-paint-the-climate-last)). |
| Coupled placement preferences | `Solve`, `Rivers` | Construct hard requirements first; search cheap decisions over shared geometry/costs, then validate the finished world. See [search integration](../../../../docs/map-generators/constraint-search.md). |
| Ordered generation and start selection | `Pipeline`, `Terrain`, `BalancedStarts`, `StartQuality` | Select the composition appropriate to the map and score the finished colony. |

All modules are under [shared/](../../../../src/map/generator/shared/). Read implementations as well as declarations. `shared/legacy/` explains older generators but its area-grid representation does not compose directly with current tile masks; use the current toolkit for new designs.

A useful generator has a named options struct, a layout struct, a reconstructible `design()`, a short stage-oriented `generate()`, and a validator tied to its play contract. Keep pure design separate from `Game` mutation. Where a design lays beaches, run `layBeaches` before deriving final pure-grass masks and before drawing any catalogue terrain (it sands every land vertex beside water); a catalogue design need not lay any. Use `writeVertices` to bake the terrain, ideally from a `sketchTerrain(layout)` that is a pure function of the design, so the validator can rebuild it. Set a meaningful `context.stage` at potentially failing stages and return an actionable detail.

The normal ordering is layout → terrain → teams/settlements → kits and ambient resources → swarm clearance and reachable crop guarantees → required road reopening → cramped-start relief for non-default amounts → final validation. This is a composition, not a mandatory superclass. A landscape often places resources before searching for starts. A wall map must protect its structural deposits from every clearing stage. Explain any different order in terms of the invariant it preserves.

`designMismatch` only checks that reconstruction succeeded and dimensions agree; its name does **not** mean it compares all terrain to the layout. Add the actual checks the idea needs: homes retained, gates open, roads wide enough, ponds present, target access, room per region, dry forests, symmetry, or contact spread. A catalogue design can compare the finished terrain with its sketch vertex for vertex, which replaces every "designed stone present" check ([validate the terrain you designed](terrain-catalogue.md#validate-the-terrain-you-designed)). `walkFromFirstColony` establishes a route from the first colony's workers to each other colony; it does not prove route width, useful expansion, or long-term openness.

### Validate the route graph in more than one terrain state

For an arena that promises a circuit, global connectivity is insufficient: a chain
also connects every colony. Close all declared gates and reopen each separately,
checking that it joins its intended pair of regions (`checkGatePartition`). Include
water when the promise must survive swimming. A gate helper may virtually reopen
its supplied gate tiles, so independently verify that the actual openings are
walkable and safe from future crops.

Check both removable obstacles and future growth. Treat wheat and wood as cleared
when looking for unintended bypasses, and their possible spread as blocked when
proving permanent circulation. Eternal structural stone, terrain walls and ground crops cannot take (Paths, Barren, Rough, sand) can stop that spread;
buildings and current fertility are not permanent barriers. For reserved building
room, remove future crop tiles from the buildable mask **before** finding complete
footprints; checking only each footprint's anchor can overcount safe space.

Test the individual promises with deliberate damage. If every court promises all
three fruits, remove only oranges and leave the other fruit intact; a total-fruit
check should not pass. Similarly, block a declared entrance while leaving its walls
standing. These were real gaps found in Gauntlet's review, not extra guarantees
that every generator needs.

### Islands are a movement contract

For a swimming-required archipelago, validate two different graphs on the finished
map. Walking components must contain at most one estate even after harvestable
resources and buildings are removed; use eight neighbours and torus wrapping so
beaches, diagonal contacts and chains of neutral islands cannot hide a bridge.
Then use `groundUnitTiles(map, true)` to check actual swimming access to colony
workers and useful landing/building ground on expansion islands. A pool count is
not a movement proof. `firstColonyCutOff` establishes that at least one worker in
each colony is reachable, not that every worker is reachable.

Separate local opening access from travel between estates. A fort may need short
supply piers to its own external farms while every rival and neutral key remains
across open water. State that scope explicitly. Legal stone requires grass corners
and beaches leave walkable mixed tiles: a wall beside water does not automatically
make its harbour gate swimming-only. Check the shore detour before making that claim.
A cliff coast (`cliffCoast`) leaves no beach and no shore to swim from, but only along
the water it is told is sea: City states' Riverside creek mouth let swimmers in until the
home's own water was made wallable too. Prove the seal with `leakWithShut` and swimming
on, which names the first tile reached outside.
Bastion Keys instead proves separation between entire estates, with both gates
inside that movement contract.

### Fix the primitive at the right boundary

Before implementing a second flood, beach pass, wall rasteriser, coast seal, climate painter, clump grower, route clearer, kit frame, or swarm-clearance routine, search for the operation in `shared/` and its callers (terrain walls are in `Walls`, climates in `Climates`). A defect affecting multiple maps belongs in the helper when its contract is wrong. Add an explicit parameter when policies differ (for example a protected wall mask), instead of recognizing generator names inside a helper.

Demonstrate a bug with a retained request/seed or a focused fixture that fails on the old code. Cover the helper's actual invariant: wrapping, pure-terrain conversion, resource protection, complete footprints, finite frontier exhaustion, or stable tie-breaking. Run affected generators, including the caller that originally worked. Do not silently turn “repair” into punching a new shortcut through the map's intended hard boundary.

Extract on the second use even when the first user is an old generator. Generators rebuilt in parallel each grew their own parting check, so six now carry a local `partingGroup` beside the shared `partingBarrier`; when parallel agents need the same check, land it in `shared/` first. A shared check is also code to review: `quarryFault` skips face tiles that cannot hold stone, so three generators had to add the check that every face tile holds it. Central Quarry copied Continents' dry-start pond loop and its closest-walk check. Both became shared helpers (`waterDrySite` returns the dug corners in order so each caller keeps its own telemetry; `closestWalk`), with Continents moved onto them and its map fingerprints unchanged. A review of the new map then found a real bug in a shared primitive: the Biomes dry reserve dealt its fields to the first tiles in row order. Fixing it at the source (with a maintainer's agreement, since it changes another generator's maps) moved Continents' output by 0–0.6% of its tiles, so the fix was measured, not assumed. Compare old and new dumps tile by tile before calling a shared fix cosmetic or harmful.

Changing a shared primitive can alter many generators' maps. Identify all affected generator revisions, bump them for intentional output changes, and check the sim-diff report moves only those generators. Do not bump unrelated versions to hide an unexplained difference. If the primitive is also used by simulation, follow the repository's stronger simulation compatibility checks.

### Count changes to the world, not successful-looking calls

`Map::isResourceAllowed` checks placement terrain and occupancy by units/buildings;
it does **not** establish that a tile has no resource. A second sowing pass can
revisit an already-seeded tile, reset its stock and count it as another deposit.
Use a vacancy check when adding new deposits, and increment telemetry only for
actual additions. A Hungry Marches prototype reported 536 wheat tiles where the
finished map contained 507 because its density pass revisited its bank guarantees.
Add a small regression comparing summed placement telemetry with the finished
resource count; telemetry that merely repeats requested budgets will not catch it.

For resource controls, distinguish geometric capacity, seeded reachable capacity,
initial deposits and initial stored amount. Use the metric that matches the label's
promise. Collect existing counts during placement and validation rather than
adding expensive telemetry-only analysis.

When a local game study is large, budget its artifacts as well as its CPU. Final
saves and verbose logs can exhaust disk while games are running. Compress completed
saves and stream verbose logs through compression when useful; preserve exact
commands, parsed outcomes and binary identity. An interrupted log or failed final
save is an incomplete verification run, not evidence of an AI defeat or successful
playtest. Rerun affected jobs, and validate the final source's maps after later
placement changes rather than treating an earlier tournament as current evidence.

A performance run is also a reliability run: record failed generations beside
its timings. A largest-map benchmark found a Hungry Marches central-access failure
outside the earlier random sample. Preserve the failing request and fix its
geometry before timing the final version. When optimizing repeated design work,
compare against a frozen binary of the **same geometry**, including telemetry
on/off; a simultaneous layout change would hide an accidental output change.
Shared `DesignCache.h` can replay named-stream states and telemetry, but only use
it when its request key covers every input the design reads. For spatial culling,
prove a conservative bound that includes bends and shoreline distortions before
skipping expensive samples, then compare generated map bytes.

## Fairness mechanisms and their limits

[Orbits.h](../../../../src/map/generator/shared/Orbits.h) distinguishes:

- Point groups: half-turn for two colonies; quarter-turn for four on square maps; mirrors for four on rectangles; square dihedral symmetries for eight. Check the returned group rather than assuming arbitrary team counts work.
- Translation groups: whole-tile lattice shifts, useful on rectangular tori without a privileged centre. Supported group orders must divide the power-of-two tile count. `latticeSites` also handles other counts with approximately even rows; its `exact` flag matters.
- Wedge copies: separately rasterized rotational shapes agree approximately. Tile rounding, beaches, footprints and deposits can break equality. Validate tolerances rather than calling them exact.

For exact claims, transform terrain **vertices** and final **tiles** correctly, transform building anchors as footprints, use orbit-consistent resource selection, equalize deposit amounts, and run `orbitMismatch` on the completed world. A symmetric coastline with independently randomized kits is not a symmetric start. Translation-compatible noise needs the group's period (`latticePeriod`), otherwise a border warp breaks repetition. Randomizing a shared orbit field gives a different map each seed while preserving equal conditions within that map.

For asymmetric landscapes, [BalancedStarts.cpp](../../../../src/map/generator/shared/BalancedStarts.cpp) is an instructive bounded search. It floods distances from gathering neighbors, considers legal swarm footprints, shortlists at most 900 sites, re-scores after modeling the starting colony, and seeks a narrow resource-cost window containing mutually separated starts. This is a heuristic, not an optimal global placement solver, and its legacy placement model is not a drop-in replacement for arbitrary settlement geometry.

Use [StartQuality](../../../../src/map/generator/shared/StartQuality.h) to compare *finished* colonies on wheat/wood access, fertility, resource depth, room, and isolation. What those measurements are worth is fitted to real games ([FairnessModel.h](../../../../src/map/generator/shared/FairnessModel.h), [the method](../../../../docs/map-generators/fairness-model.md)) and is the same for every generator, so a concept that needs a different bar states it in `validateWorld` rather than by retuning the score. The score is the map's fairness: 1 when every colony is equally likely to win, 0 when one would take the map. It says nothing about whether the starts are any good, so keep absolute viability checks. Reachable wheat can coexist with no building space.

Also measure nearest rival, access to contested targets, number of fronts, expansion capacity and walking versus swimming/clearing costs. `Contact` supplies comparative costs; these are not time estimates. In particular its generic water-first `StepCosts` model is different from the report's engine swimming predicate when algae occupies water. Use the actual movement predicate for a gameplay reachability claim. Deal home sites with `dealStarts` before any home/kit/tower arrays are indexed by team: random assignment avoids persistent index bias but does not repair an unfair map.

## Controls and resource policy

Read [GeneratorControls](../../../../src/map/generator/core/GeneratorControls.h) and the chosen generator's metadata. There is one control definition shared by editor, lobby, validation and study tools. Stable option IDs express real meanings; do not overload the legacy descriptor with new fields. Use `toggle` for switches, `choice` for named alternatives, allowed-value lists for irregular domains, and `percentage` for ambient amounts. Use control accessors rather than treating numeric IDs or selection indices as array positions.

For each resource layer, write down its policy:

| Layer | Amount-control behavior |
| --- | --- |
| Ambient wheat, wood, stone, algae, fruit | Scale density, count, patch area or field threshold with its registered amount using `scaledCount` / `scaledShare`. Test measured response across seeds; eligibility saturation means exact linear final tile counts are not generally possible. |
| Starter kit / emergency reachable wheat and wood | May remain guaranteed at zero abundance. State this deliberately; a zero slider need not starve the opening. |
| Structural stone ridges / boundary walls | Keep the designed boundary invariant when ambient stone changes; explain the distinction in the control description and design. |
| Prize or objective deposits | Preserve the objective's location/role while scaling its resource amount, patch area or number of deposits with the applicable control. Prefer responsive prizes over additional fixed-resource exceptions. |

At 100%, the scaling helpers preserve their inputs. For existing maps, avoid extra default-path random draws that unnecessarily change established output. For a new map, ensure a control does something: the current scaffold declares wheat/wood amounts but only plants fixed starter kits, so those controls still need real ambient layers wired to them. A percentage used only as `amount > 0` provides presence/absence, not a useful abundance scale.

High amounts can remove legal starting sites or wall colonies into crops. Widen a bounded site search when sensible; run `reopenCrampedStarts` at non-default amounts, passing protected walls, or `openStartsBuriedByResources` when nothing in the design budgets a colony's room, so the defaults can bury one too. It uses `openCrampedStarts` to seek 16 reachable 4×4 anchors within 24 steps, then rechecks crop supply. These are emergency heuristic targets, not a promise of sixteen independent buildings or a substitute for deliberately roomy defaults. Inspect its result and validate your stronger map-specific minimum.

## Registration and style

From the repository root, `python3 tools/new_map_generator.py <id> "<Display name>"` scaffolds the header/source, registry entry, SCons source and translation keys. It mutates several files; inspect its output. The current scaffold is a starter design, not a finished implementation: add strategic variation, actual abundance scaling, site dealing where needed, design-specific validation and a `validateRequest` such as `designFailure<design>` when appropriate. Remove the generated TODO comments once the real design is described.

Take the next free legacy id and never reuse a retired one (the framework doc's catalog lists the retired numbers). The numeric id is a compatibility identity: seeds derive from the request seed and the stream names, so renumbering a generator during review changes no fingerprint, only the id column of its fingerprint rows. Several branches written in parallel will pick the same number; expect to renumber before merging.

Put a generator's own contract check (its supported shapes, its variants, what its validator refuses when the world is deliberately damaged) in `src/map/generator/MapGeneratorContracts.h` as one function per generator, registered in `catalogContracts()`; the shared-primitive fixtures stay in the toolkit and landscape check headers. A generator that refuses a shape must do so through its request check, which the defaults test consults before asserting that every generator handles a rectangle.

Keep review evidence out of the main history: previews and the written findings belong in `docs/artifacts/<generator>/` or the design doc, and the bulk archives, saved maps, replays, logs and one-off scripts on an `evidence/<generator>` branch that the docs link to. A design doc records what was measured and what it changed; a validation log that names one session's local directories does not survive its author.

For manual registration, use one definition in [GeneratorRegistry::builtins](../../../../src/map/generator/core/GeneratorRegistry.cpp), with a unique stable string and nonnegative numeric ID, display translation key, revision, availability, controls and callbacks. Preserve existing IDs and catalog order; add the new entry without reshuffling the rest, and keep editor-only Uniform last. Add the source in [buildsys/sources.py](../../../../buildsys/sources.py). Update translation keys/tables and translate scaffold placeholders through the repository workflow. Normal preferences/history consume the metadata; do not add duplicate UI range/default tables. Keep the old descriptor codec unchanged for new options.

Use PascalCase header/source names, `#pragma once`, the current SPDX header, named constants for design budgets, typed options, explicit dependencies, and small local helpers only for genuinely map-specific operations. Follow root `.clang-format` (tabs, Allman braces, 100 columns) on touched generator C++. Explain the map's play rationale, units and non-obvious heuristics in comments; keep current implementation comments accurate. Avoid Windows macro names `near`, `far`, and `small` as identifiers.

Randomness comes from `GenerationContext::stream`, `bounded`, and `shuffle` with stable names separating layout, homes, resources and decoration. Do not use wall-clock reseeding, `rand`, `std::hash`, global mutable noise caches, or gameplay RNG directly. [GenerationService](../../../../src/map/generator/core/GenerationService.cpp) initializes the map’s owner-specific RNG streams from the request seed for engine mutations. Generate into a fresh `Game`; discard a failed partially mutated world. Bounded retries use recorded derived seeds and fresh games. Same request/seed/revision repeatability is a per-platform contract here; generator floating-point output is not promised identical across platforms.

## Several generator branches at once

Generators written in parallel meet in the same handful of files: the registry, the SCons source list, the framework doc's catalog and toolkit tables, the defaults test's call list and the toolkit checks' `PASS` line. Merging them by line is how these files get broken: a union merge duplicates registry entries (the registry throws `Invalid generator registration` for a repeated id, and every CI job that loads it fails), splices two `puts` calls into one, drops a brace between two functions that shared a closing line, and a line-level dedupe silently deletes repeated code lines such as `for (const auto &d : definitions)`. Merge these files by key instead: one entry per generator in the registry and source list, catalog and toolkit rows keyed on their first cell, and the `PASS` line rebuilt from its fragments. Never run a line dedupe on a code header.

Stack the branches in one order, rebase each onto the previous one's tip, and normalize the shared files after every rebase; then a branch's fingerprints computed at the top of the stack are valid for every branch below it, because each branch's own sim-diff report already showed its shared-code changes leaving the other generators alone. Build and run the tests on each branch before pushing it: a stack that compiles at the top can still fail at a middle branch whose registry lost an entry. Keep build logs and other session files out of commits (`git add` the files you changed, not the tree).

Two branches will also generalise the same helper in two directions: one turns `placeTower` into a starting-building placement with coverage and stocked supplies, another into `placeBuilding` for any completed building. Neither is wrong, and a concatenating merge keeps both bodies and compiles neither. Merge them into one search with each caller's options on top (here `startingBuildingSite` under `placeTower`, `placeStartingBuilding` and `placeBuilding`), keep every caller's exact behaviour so the fingerprints hold, and say so in the header. The same goes for two branches that each revise a shared function: take the revising branch's version whole and re-apply the other branch's purely additive functions as a patch, rather than trusting the union.

Two branches will also name the same new helper differently in kind: one `cropSpreadEnvelope` returned a `Flood`, another the flood's steps. A union declares both and compiles neither (C++ cannot overload on the return type). Keep the richer one, give it the other's extra rule (here, stopping at tiles whose growth flag is off) and make the second branch's callers read the field they wanted.

Near-duplicates survive a batch even after the shared files merge: two ways to spread sites by walking distance (`farthestSites` over tiles, `farthestCells` over a cell graph), two patch planters (`plantPatchNear` returning a count, `growPatchesNear` returning the patches spent across pockets), two starting-access checks (`cropsBesideReach`, `startingAccessFailure`), three separation proofs (`coloniesApart`, `colonyLeak`, `checkGatePartition`). Each was written for its own domain and its own promise, and merging them is a fingerprint-changing refactor of its own, so list them in the framework doc's toolkit table with their differences stated and fold them when a third caller appears, not while the batch is merging.

## Verification that answers design questions

Use an optimized client for measurements and choose build concurrency for available memory. The following are entry points, not a claim they have been run for a new design:

```sh
scons release=1 server=0 map-generator-study engine-tests map-generator-golden-test
python3 test/run_tests.py --filter 'MapGeneratorDefaults/*'
python3 test/run_tests.py --filter 'CustomGameSetup/*'
build/src/MapGeneratorGoldenTest glob2-map-design-golden --print --only=<id>
build/src/MapGeneratorGoldenTest glob2-map-design-sweep --sweep
build/src/MapGeneratorGoldenTest glob2-map-design-perf --performance
scons release=1 server=0
build/src/glob2 map generators
build/src/glob2 map generators canals
build/src/glob2 map generate canals --seed 7 --width 128 --height 256 --teams 4 --output artifacts/map-design/canals-7.map --preview artifacts/map-design/canals-7.png --report-file artifacts/map-design/canals-7.json
```

Use disposable test profiles, following the [test README](../../../../test/README.md). No fingerprint rows are committed: the pull request's sim-diff workflow runs `MapGeneratorGoldenTest --print` on the merge base and the pull request and lists every row that moved. For intentional output changes, bump the affected generator's revision and check that only the expected generators' rows moved. A fingerprint proves reproducibility of a known snapshot, not quality. `--performance` times every generator at its own defaults (256x256, seed 42) and asserts identical bytes, outcomes and telemetry with the internal performance collector on and off; for a broader load, `MapGeneratorProfileFixture <profile-dir> <seed> <rounds> [generator-id...]` (see the [framework doc](../../../../docs/map-generators/map-generator-framework.md)) round-robins every generator at randomly drawn parameters, which is also a fixed-seed/round-count way to compare a shared primitive's before/after cost, and a target for an external sampling profiler.

The current `--sweep` is deliberately small: selected square sizes/counts, three or five seeds, passing an accepted cell if at least one succeeds. It skips invalid combinations and does not cover rectangles, all controls, 64-tile maps or every team count. Inspect success rates and supplement it. A cell surviving only one of five trials is a warning even though the harness passes.

Build a request matrix from the live catalog:

- Smallest/largest accepted sizes, both narrow rectangle orientations, one colony, odd counts, exact-symmetry counts and dense maximum counts; worker-count extremes.
- Every choice and toggle; control minima, defaults, maxima; coupled extremes such as small homes plus wide water, dense crops plus many workers, or maximum boundary thickness plus narrow gates.
- Resource zero/default/maximum individually and together, including dominant wheat versus wood. Verify guaranteed supply, ambient response, remaining building room and exits.
- Boundary-invalid relationships, malformed domains, unknown options, and failure diagnostics. Reject an impossible contract explicitly; do not silently normalize a service request or use validation to hide a common bad seed.
- Multiple training and held-out seed ranges, repeated fresh-process requests, and generation cost at maximum size. Retain all rejected seeds and distinguish invalid requests, placement failures, timeouts and successful-but-unplayable maps.

[tools/map_generator_study.py](../../../../tools/map_generator_study.py) obtains playable defaults from the registry or takes a JSON config list with `id`, numeric `method`, and `params`. Study `w`/`h` use exponents; the normal CLI uses tile counts. Obtain current keys and IDs from `MapGeneratorStudy --catalog`; do not hardcode another catalog in a new tool.

```sh
build/src/MapGeneratorStudy --catalog
python3 tools/map_generator_study.py --configs artifacts/map-design/configs.json --count 32 --start 20001 --workers 3 --output artifacts/map-design/study --label held-out
```

The study runner saves configs, repeatability rows and per-seed metrics, but does not make every failure a failing process exit; inspect CSV statuses and tails. Its 12-second external timeout is a measurement limit, not permission to introduce wall-clock decisions in generation or simulation. Tune against worst-colony room/access as well as averages and scores.

Use [report metric definitions](../../../../docs/map-generators/report-format.md) when interpreting JSON: 4×4 sites are overlapping anchors, terrain and underlying terrain differ, unreachable costs are `null`, static fertility is not yield per tick, and saved-game reports describe the current snapshot. Retain JSON beside `.map`: saved maps do not preserve the whole generation request. Preview mosaics across seeds/settings expose visual variety and seams; inspect actual game views for building access and fighting scale.

For fairness, use [the tournament](../../../../docs/map-generators/fairness-tournament.md) with selected generators and a bounded budget, for example starting from `python3 tools/map_fairness_tournament.py run smoke --help`. Full cyclic team rotations over an unchanged map separate start position from team-index effects when seats have equivalent roles. For deliberate role asymmetry, preserve roles and alliances and rotate comparable seats; see [asymmetric sieges](asymmetric-sieges.md). Use multiple map seeds and engine seeds. Read decisive-game results, cap share and uncertainty, not just pooled winners. The existing study documents examples where start scores missed dominant positions; regard that as a reason to measure expansion/fronts, not evidence that today's revised generator still has the same result.

Watch AI colonies through expansion and late resource growth; include each of the newer AIs (Nicowar, Cortex, Cabino, Maxima) rather than relying only on Nicowar's habits, and don't tune for the older Numbi, Castor or Warrush ([which AIs to play](tuning-playbook.md#which-ais-to-play)). Retain maps, settings, saves/replays and relevant tick observations. Human play should answer whether the intended choices happen, remain legible and offer counterplay. If tests pass but play stalls, revise the geometry/resource policy instead of merely loosening acceptance thresholds.

Store bulky study outputs in ignored `artifacts/` and attach or otherwise make evidence available to the reviewer; a local ignored path alone is not a review artifact. Document actual coverage and omissions. Changes confined to generation need generation/serialization evidence; changes to simulation additionally require the repository's save-continuity, replay/network-boundary and cross-platform per-tick checksum checks as applicable.

## Debugging, contracts and release housekeeping

Lessons from rebuilding, reviewing and profiling generators, grouped by the job they serve. Most date from the 2026-09-16/17 rebuilds.

### Seeing the tiles and proving the design

- **See the tiles, not the preview.** The native preview paints two pixels a tile and draws no buildings, so one-vertex sand lines, beaches, wall gaps and a garden's seal are hard to see. Use `--preview-scale 8` for a sharper preview; `glob2 map render MAP --output x.png` draws the full game view (up to `--render-max-pixels`, 4096 by default), the only way to judge whether a terrain wall reads, a climate looks right or decor renders; or dump the tiles the game sees (`map study ID ... --report terrain --output-dir DIR`) and render a close-up with [`scripts/render_terrain.py`](../scripts/render_terrain.py) around the colony that misbehaves. When a validator fails, print the first offending tile and its design labels to stderr for one run; that found a berm tile, not a courtyard one, behind The Glacis' "gardens open" failure within a minute.
- **A contract should survive a total reshuffle of the maps.** Write contract checks against the design's promises (the supported envelope, what the validator refuses, the guarantees every colony gets), not against fingerprints. When both constraint-solved generators had every map changed by a stream rename, all 17 contract suites still passed and only the map fingerprints moved, which is the split you want: fingerprints say *nothing changed*, contracts say *the design still holds*.
- **Contracts that exercise the promises.** A generator's contract (`src/map/generator/MapGeneratorContracts.h`) generates its envelope and both resource extremes, runs a couple of thousand `growResources` passes on the abundant map and requires the validator still to pass (containment), then damages the world and requires refusals: remove a wall stone on a map whose stone amount is 0 (on a terrain-walled map, turn its cliff or ridge vertices to grass, as Gauntlet's corruption 0 does, or one corner of a ford to water), plant a crop where the concept forbids one, clear a village's wheat. A damaged-world check needs a tile that is certainly the feature; search outward and keep the first tile the validator refuses rather than guessing a coordinate.
- **Sweep every shape before any tuning.** [`scripts/sweep_shapes.py`](../scripts/sweep_shapes.py) generates every map shape and colony count with three seeds and lists refusals. Run it after every geometry change: a refusal that is not "too many colonies" is a bug. When a home shrinks to fit a crowded map, scale its floors with it (never below a documented share) and try the other variants before refusing.
- **Look at the game view.** `glob2 map render FILE --output view.png` draws terrain transitions, sprites and buildings as play shows them ([CLI guide](../../../../docs/map-generators/cli.md#render-a-whole-game)); the preview's colours hide how a look reads (dry grass is bare brown ground in play).
- **Re-roll after every validator change.** A validator is code that can refuse correct maps: tightening Bajada's town-ring check made one roll in 2,000 fail a map whose ring was whole. Run the random-roll pass again after any validation change, not only after geometry changes.
- **Measure surviving features after destructive passes.** Beaches, gate clearing and decoration can erase or obstruct a feature that existed in the design mask. Count the final surviving landmark pieces, and keep ambient stone/fruit out of the entire promised junction reservation. Mutate a side tile while leaving the centre open to test a width promise; reduce wheat to a token deposit to test quantity rather than presence. Document when generation checks plot ownership but a standalone world validator checks only general access.
- **Separate opening checks from persistent invariants.** An initially unseeded inn court may legitimately grow over in a no-AI growth test; plot containment and dry structural wood must still hold. Validate opening access after all resource placement, and keep that check explicit rather than dependent on whether a telemetry pointer was supplied. Test collection on/off for identical generation behavior.
- **Geometry checks that pass on paper.** Three Central Quarry failures were checks that looked right:
  - *A room count over pieces.* The quarry's room counted free island tiles, but the tiles could lie in several unconnected pieces, and the outcrop stopped at 6 of 9 tiles in a small one. Count the largest connected component the feature will grow in, and seed it there.
  - *A ray stepping back.* A ray marched in half-tile steps and rounded to corners can step back onto the shape it left on a diagonal, so the "shore" was found half a tile from the island. Skip the source mask when looking for the far side.
  - *A slack without a margin.* A walk-spread tolerance of exactly twice the band refused maps whose swarm stood a few tiles off its designed site. Give checks on the finished map a margin for the swarm's placement.
- **Bound a flood only after proving the bound.** Every queried goal and reconstructed path must lie within it. Local building-room checks need a global fallback and a check for blocked external roots; compare ordered footprints and failure results against the original helper, and preserve map-byte comparisons when optimizing exact behaviour.

### Map fingerprints, revisions and shared code

- **Record one generator's rows in seconds.** `MapGeneratorGoldenTest <profile> --print --only=<id>` prints just that generator's fingerprint rows; all generators take about half an hour on a debug build. Record the rows of every generator a shared extraction touches before extracting, and check they stay byte for byte ([working practice](terrain-catalogue.md#working-practice)). The pull request's sim-diff report then confirms which generators' rows moved against the merge base.
- **A remote Linux build from a Mac checkout.** Syncing the working tree with `rsync` also copies the Mac's `config.h`, `config.log` and `options_cache.py`, which break a Linux configure; exclude them, and start the remote build with `nohup ... </dev/null >log 2>&1 &` so the SSH session can end. Re-sync and print both platforms' rows once more after the last change to the generator, including validator-only changes, since a row records whether each map passed.
- **Decide the sim version with the maintainer.** Online rooms group players, ratings and generation by sim version, and a native room's setup names a generator and seed, so a revision that changes maps can need a `SIM_REVISION` bump under `AGENTS.md` even though saves and replays embed their terrain. When it does, re-record the committed match record in the same change ([turn engine guide](../../../../docs/multiplayer/turn-engine.md#simulation-version)); the pull request's sim-diff verdict shows whether output moved.
- **The design is asked for three times.** The request check (`designFailure<design>`), `generate` and `validateWorld` each build the design from the request. Prefer the shared `cachedDesign` helper for a request-only design; check that its key covers every input the layout uses, and keep live-world validation outside the cache. A design that depends only on the request (every draw from a named stream) can be cached per thread keyed on the request, provided its telemetry is recorded into the layout and replayed into each context in order; Honeycomb isle's contract generates A, B and A again and requires the same map and records. With whole-map distance transforms replaced by local disc scans, generation became 3.5–4.4× faster with an identical 214-map corpus.
- **Refactor against a dump corpus, and know which order matters.** Karst towers' 700-line design became ten named stages with byte-identical output, checked by comparing `--report terrain` dumps of the old and new binaries on defaults at eight shapes and 72 random control rolls. Draws from named streams (`context.bounded`, `context.stream`) are independent per name, so stages may be reordered as far as their inputs allow, as long as each stream's own sequence is kept; `Map::setResource` draws from the map’s `ResourcePlacement` stream in call order, so the order of planting in `generate` may not change. A design retry loop is deterministic for the same reason: `validateWorld` replays it and consumes the same named draws. A cached design must leave the asking context as building it would have: replay its telemetry (`GenerationTelemetry::replay`) and wind on the streams it drew from, taken from the building context's `namedStreams()` rather than a hand-kept list.
- **Reuse the helper's intermediate result before adding another cache.** Gauntlet recomputed each farm's exterior-distance flood immediately after `layFarm` had computed it; an optional `edgeDepthOut` parameter now returns the existing field. Its regression compares terrain and every returned farm field with and without the output, including empty/full regions and a field crossing the torus seam. Check existing generators' map fingerprints when changing a shared helper, even when its default behaviour is meant to stay identical.
- **Removing a primitive.** When a rebuild orphans shared code (Bases, Compounds and Lots went with the premade bases), grep for every include and symbol, move what survives into the primitive that owns the concept (`wallStanding` into `Walls`), delete the files from `buildsys/sources.py`, rewrite their toolkit checks around what remains, remove definition hooks nothing uses (`startingWorkers`), their translation keys in every language, and their rows in the framework document's primitive table and in these references.

### Performance

- **Profile with a fixed output corpus.** `MapGeneratorProfileFixture <profile> <seed> <rounds> [generator-id...]` drives one generator in a loop for macOS `sample` (or `perf`); read the call tree by caller, since inlined design code shows up under `generate`. Before touching anything, record a corpus of a couple of hundred requests (defaults at every shape plus random controls and each control's extremes) with `MapGeneratorStudy ... dump=... result=...`: the `STUDY` fingerprint, a hash of the tile dump and the telemetry records. Prefer deterministic terrain dumps or native fingerprints; use saved `.map` hashes only after establishing that the saving path has stable metadata. Check the corpus after every change, and the map fingerprints of every generator a shared primitive touches. Include cold single-map runs of the old and new binaries: the fixture's request randomization can build a design before its per-generation timer starts. Under concurrent load, report CPU time separately from wall time; sample the generator process, not a wrapping `time` process.
- **Cost at the largest size, from the first working build.** Time the largest map with the most colonies next to a comparable generator (512x512 with 12 colonies: The Glacis 2.3 s, Forts 2.0 s) and remember the lobby rolls five candidates. A whole-map scan inside a per-feature loop (a pond placement checking every tile for neighbours) is the usual culprit; keep a list of placed features instead. `MapGeneratorProfileFixture ... GENERATOR --largest [--telemetry]` measures default controls at 512²/twelve teams without randomizing the request.
- **A cheaper request check, and memoised preferences.** A design whose failures are mostly seed-dependent doesn't need to be built for the lobby's request check: Central Quarry's `validateRequest` checks only what the request alone decides (colony count, whether the lake fits) and lets seed failures surface at generation, which removed a third of its cost. Its other hot spot was `farthestSites`' `prefer` callback, asked of the same candidates across every trial and preference level, each answer a flood. Memoising answers per tile and precomputing the cheap mask before the floods cut 512×512 generation from 10–16 s to about 3.7 s. A second profile, by source line, showed 78% of what remained was the design built twice; caching it per thread, computing square means for every tile at once with running sums (`meanFertilityField`), and stamping each lake's keep-out locally instead of a whole-map dilation gave 0.10 / 0.25 / 1.17 / 1.62 s at 128/4, 256/4, 512/8 and 512/12, with identical map fingerprints. Profile by caller line; function-level totals hid that most of the time was one call made twice. A third profile found the rest in whole-map work repeated per colony, and every fix was exact: bounded floods (`floodFrom` with a limit) where only "within N steps?" matters; `closestWalk` stopping each flood at the closest pair so far; `waterDrySite` measuring a pond's effect on a 61×61 window instead of rebuilding the map's growth field per pond; margins stamped round a route's tiles instead of dilating the map; territories drawn by one multi-source walk instead of balanced noisy growth. Central Quarry ended at 0.08 / 0.19 / 0.84 / 1.15 s, and the two shared helpers made Continents 2.3× faster at 512×512 with 8 colonies with its map fingerprints unchanged. A per-colony or per-feature call that touches every tile of a 512 map is the first thing to look for.
- **Prove small facts with small floods, and flood from the small side.** Hidden Oasis' profile (2026-09-17) had half of generation in whole-map walking floods, most proving facts about one plateau: "with this pinch shut, nobody reaches the basin" flooded the whole map from the mouth once per colony, in the design and again in the validator. Flooded *from the basin outwards* it costs the basin and the sealed end of the gorge, and a leak is any reached tile off the plateau. Bound every validator walk by the promise it checks (beyond the limit it fails either way), compute the one whole-map field the design needs once and reuse it, and do a second repair pass only where its check fails (every trail was cut twice; the second cut was needed on 2 of 2,000 maps).
- **`floodFrom` costs the map, not the flood.** It clears a whole-map step field, reserves a whole-map queue and scans a whole-map source mask on every call, and `tileMask` allocates another: a 48-step flood that reaches 4,000 tiles on a 512x512 map pays for 262,144. `reachFrom` (`Grid.h`) is the same flood, same tiles in the same order with their steps, at the cost of what it reaches. Using it for the site search's yield and catchment floods and every bounded check took a quarter off Hidden Oasis and 7% off Central Quarry with every map fingerprint unchanged. Reach for it whenever a flood has a limit or a sealed space.
- **Optimize the extent of a query, not the growth rules.** Portage Lakes' profile found whole-map fertility fields rebuilt for local bay candidates and complete floods used for single endpoint distances. A local terrain window needs the entire query radius, the growth probe reach and the pure-tile corner halo, with correct toroidal wrapping; use a full-map fallback when the window is not cheaper. An endpoint BFS must preserve the original neighbour model, source admission and distance limit, and reset every visited scratch entry before reuse. Fifteen paired requests retained identical reported metrics; the largest seed-1 benchmark fell from 18.90 to about 4 CPU seconds. Metrics do not replace fingerprints for byte-equivalence claims.
- **Stop a reachability proof when its goals are satisfied.** Faulted City's `arrangeBuildingGrid` flooded the whole map to prove access to nearby proposed buildings; an eight-neighbour search can stop once every building has a reached cardinal face. Preserve blocked-source and torus semantics, and compare against the exhaustive predicate on fragmented masks, seam crossings, empty/duplicate sources and diagonal-only access. Paired map bytes stayed identical and command times fell 10–17% (city evidence: PR #350).
- **Use unit-cost search for unit-cost questions.** Orchard Commons' access validator spent most of its time in repeated weighted searches, although every traversable step cost one; the shared eight-neighbour BFS preserved those distances more cheaply. Match passability, source tiles, diagonal rules and unreachable values before substituting algorithms, and verify saved-map bytes on a fixed corpus. Reprofile after the final terrain pass: additional noise fields and fertility work can change the cost even when the earlier hotspot stays fixed.
- **Hoist invariants and skip provably irrelevant analysis.** Who Ate the Map?'s profile found per-tile bite rotations and detached-island service analysis even when no secondary island met the minimum grass budget. Cache sine/cosine per bite without changing projection arithmetic; skip distance fields and footprint packing only after proving all secondary capacities must remain zero. Keep returned masks correctly sized and RNG/telemetry intact. About 12% off paired fixture CPU time with identical saved maps.
- **Exact pruning in shared geometry.** `warpCorners` measured tile-by-tile gaps between obstacles; skipping a tile already at least the best distance from the other obstacle's box is exact by the triangle inequality on the torus, and every generator's map fingerprints stayed the same. A bucketed pair search the profile did not blame gave nothing and was reverted: optimize what the samples show.
- **Bound expensive geometry without changing it.** Last Treeline evaluated shoreline trigonometry across the entire map; the sum of its radial terms gave a strict outer bound, so distant tiles could skip the angle calculation. Preserve comparisons and RNG calls, then compare saved-map fingerprints across seeds and dimensions.
- **Rollback can invalidate the loop doing the rollback.** In Portage Lakes, restoring a whole working layout after a failed optional bay freed the candidate vector being iterated. Iterate immutable candidate storage or a separate copy when the body can replace its owner. Retain the crowded seed as a repeated-generation regression; a release crash that disappears under a debugger is not evidence the bug is gone.
- **Separate output equivalence from timing evidence.** Freeze before/after binaries, alternate their order on identical requests, and retain repetitions and host-load limitations. Gauntlet's maps and reports matched exactly across 60 requests, while measured median improvements were only 2.8–5.8% under contention; a later, less-contended run was much faster, and comparing it to the earlier baseline would exaggerate the optimization. Inspect the profiler stack to ensure samples cover generation rather than startup or report writing.
- **Measure under load with interleaved CPU time.** When something else is using the machine, wall time and the fixture's means are useless (a 285 ms mean read 445 ms ten minutes later). Keep a copy of the old binary, run old and new alternately over the same dozen requests and compare `user` seconds from `/usr/bin/time -p`. For Hidden Oasis, hashing the saved `.map` files of that request set was stable between identical runs and proved each output-preserving step byte for byte; hash the terrain dump where a generator's files are not.

### Iteration speed

Each stage of the loop has its own cost; spend effort where it is.

- **Rebuild.** A generator's `.cpp` recompiles one object, and its header adds `GeneratorRegistry.cpp`; the link is about a second. On SCons most of an incremental build is its own fixed overhead (reading SConscripts and scanning dependencies, about 20 s on a laptop), not compilation.
- **Measure on a perf build.** `python3 tools/dev_build.py perf=1` generates several times faster than the `-O0` dev build (about 8× on a sweep). The study scripts (`control_study.py`, `refusal_sweep.py`, `final_map_stats.py`, `contact_sheet.py`, `sweep_shapes.py`) default to the newest client build, perf builds included, and note when that is a dev build, so rebuild the perf tier after a change before a large study. Their `--jobs` defaults to one per logical CPU, and the largest maps are queued first so they do not trail at the end.
- **Studies run in persistent workers.** `tools/map_generation_study.generate_map` sends each study to a `glob2 map study-worker` kept per thread, which skips process start and setup per map; results are identical to one process per map (`GLOB2_STUDY_WORKER=0` restores that, and older binaries without the command fall back automatically).
- **One process, every core.** `map study --candidates N` generates its candidates in parallel and chooses exactly as the serial search did; the map report measures its walking, swimming and clearing models, and each colony's field, in parallel; `map render` composes terrain pages on every core. `--compute-threads N` on any `map` command caps that width; the scripts pass each process its share of the machine so concurrent workers do not oversubscribe it.
- **Shared primitives skip budget accounting.** `generationCheckpoint()` charges the work budget of a scripted generator; native generation, which has none, tests one plain counter instead of a thread-local in every inner loop (a function call on macOS), and the floods, `costsFrom`, `buildAnchors`, `cropGrowthField` and `walkableTiles` take exact native paths with the same results. A new primitive should keep its checkpoints and add a native path only where a profile shows the loop.
- **A render is mostly terrain composition.** `map render` loads its artwork in parallel and composes on every core; a 256×256 map at 4096 pixels takes a few seconds on a perf build. Prefer `--preview` (well under a second) while iterating and render the game view to check walls, roads and terrain borders.

### Tooling and platform quirks

- **Tool aliases.** `MapGeneratorStudy` and the structured `map generate --set` rewrite some option names for older generators (`craters` becomes `lake-density`, `river` a width, `w`/`h`, `smooth`, `extra`, `island`, `beach`), so a control named `craters` could not be set from them. Choose control ids outside that list. `--preview-scale` accepts only 2, 4 or 8.
- **Do not let a failed command display yesterday's map.** Structured generation (`map study`, like `game run`) refuses an output directory that already contains a result. Check the process exit status before reading its JSON or rendering its dump; file existence alone can silently reuse old evidence. Use fresh output directories per revision, keep request/seed and binary identity with each panel, and require a complete status before launching its playtest. A gallery renderer must preserve rectangular aspect ratios and label differing tile scales; use tile dumps when native thumbnails omit the resource the design is about.
- **Freeze each tested build at a new path.** Use distinct executable filenames for review, games and before/after profiling, and record their source provenance. Do not overwrite an executable another test may launch; reusing an overwritten frozen path caused launch failures during Encircled Kingdom testing on macOS. A rebuild can also briefly remove the live executable between games in a shell loop, and every map launched during the relink fails to exec and reports nothing, which looks like a missing stage rather than a crash. Flush each result row, distinguish infrastructure failures (including disk exhaustion) from generator refusals, resume missing requests without replacing failed seeds, and compress completed logs and saves without deleting another task's artifacts.
- **Local study plumbing.** macOS `xargs -I{}` caps a substituted command at 255 bytes and fails silently on long generate commands; pass each job line whole with `tr '\n' '\0' < jobs.txt | xargs -0 -n 1 -P 7 bash -c`. Piping `glob2 map generate` into `head` ends it before the JSON report is written; redirect to a log instead. zsh does not split `$var` into words; use `$=var` or run the loop under bash. A terrain dump's first line is its size, not a row. macOS ships bash 3: no `declare -A`. BSD `grep -Z` decompresses instead of printing NUL-separated names; use Python to rewrite files by list.
- **Running one generator's tests.** `GLOB2_CONTRACTS=<id>[,<id>...]` (kebab-case generator names as listed in `catalogContracts()`) runs only those catalog contracts, minutes instead of half an hour. Generators listed in `separateContracts()` run as their own case, `catalog generation contracts: <name>`, each with the slow test budget; add a heavy generator there (and its test case in `MapGeneratorDefaultsTest.cpp`) rather than raising the catalog case's timeout, which once ran out on Linux after two of thirteen rebuilt generators. A default-generation case carries the generator's camelCase name in angle brackets (`--filter '*<ringWorld>*'`). `test/run_tests.py --filter` is an `fnmatch` glob, so `'*[map-generators]*'` is a character class matching nearly every test; select a tag with `--tag map-generators`.
- **Never `git stash` in a shared checkout.** Another session may have uncommitted work there, and the stash takes it too. Build a before/after baseline in a separate worktree (`git worktree add --detach DIR master`).
- **Windows macros bite only on MinGW.** A local named `near` compiled on macOS and Linux and failed only in the Windows CI job (`far` and `small` are the other usual ones). Grep a new generator for them before pushing.

### Translations

- **Design failure messages are translated.** The lobby shows `validateRequest` (the design's `failure`) through the string table, so every message a `design()` can return needs a key in every `content/data/texts.*.txt`, like control labels and choice names. Reuse existing messages where they fit ("Too many colonies for this map; use a bigger map or fewer colonies."). `test/test_translations.py` refuses a value identical to its English source in any catalog: "Bastions" is also French and Catalan, "Six" is French, "Desert" Catalan, so prefer labels that translate distinctly ("Four-pointed", not "4" or "Four"). Run `python3 content/data/check_translations.py` and the test before committing.
- **Have the translations reviewed.** Give a separate agent the strings with what each control does in the game, and ask it to check meaning, idiom, label length, script and consistency with the catalog. Bajada's "Stream reach" (how long the streams run) had been translated as a weapon's reach in 25 of 33 catalogs; "Passes" needs the mountain sense; a geology term players won't know ("bajada", "playa") should become the local word for alluvial fans or salt lakes rather than an English fallback the tests refuse. The scaffold writes English placeholders into every catalog, and `check_translations.py` counts a placeholder as translated only when it differs from English, so translate every file yourself first. Central Quarry's review of 32 catalogs then found a misspelled Greek noun; Basque grammar (an intransitive verb given an object); Slovenian using a rarer word for "map" than the rest of its file; French "Bois" for Woodland, already the catalog's word for the Wood resource, so the slider read as "Wood"; Spanish and Hungarian formal imperatives next to informal ones; Turkish and Persian words for "sand bar" that meant embankment or watery hill. Ask the reviewer to compare each new term with the file's existing keys for the same thing (`[Sandbars]`, `[Lake size]`, `[Wood]`) and with the register of the neighbouring messages.

## Internal telemetry is part of implementation and tuning

Follow [the telemetry contract](../../../../docs/map-generators/telemetry.md). Instrument decisions
where they occur, with typed scalar measurements, named choices and fallback events. Record both
a budget and its achieved amount; record the local colony/feature subject. The service retains
only the actual attempt's trace, including failures; validation reconstructions are uncollected.
JSON export is outside generation, and ordinary generation does not enable collection.

Keep the cost negligible: observe existing intermediates and return values, add small counters
to existing loops, guard telemetry-only feature summaries and dynamic keys, and never recompute
paths or scan grids for logging; guard any telemetry-only grid scan or whole-map search with
`context.telemetry.enabled()` (Rain shadow's home-stone scan once ran on every generation). Report
an unmeasurable value as -1 (a colony that cannot walk to a quarry) rather than dropping the colony
from a spread. Measure enabled/disabled generation costs in the all-generator
telemetry harness. Check identical serialized maps, RNG state, and repeatable records.

Before selecting defaults, collect reports over multiple seeds and coupled parameter extremes
with `tools/map_telemetry.py`. Inspect variant frequency, requested-versus-actual counts, calibration
values and fallback rates per attempted map. Join them to final resource access, room and contact
metrics, then revisit affected seeds in previews and games. Preserve sequence and subject when
keys repeat; absent observations and bounded/dropped traces are not zeros. Keep held-out seeds.

Retain permanent observations that explain a player-visible outcome or general regression.
Temporary hypothesis-specific coordinates, search traces and per-tile dumps belong in local debug
instrumentation and should be removed before finalizing. Promote only a useful bounded summary,
with stable names/types and documented units. Do not keep noisy debug output merely because the
collector has a record cap. See the telemetry guide for collection commands and analysis examples.

For larger batches, follow [distributed telemetry studies](distributed-telemetry.md):
plan individually identified samples, run them through the shared durable workers,
and reanalyze the complete returned reports with map-level statistical weighting.

## Recursive layouts

See [fractal geometry and worked examples](../../../../docs/map-generators/fractal-maps.md).
Use `RecursiveGeometry` for integer region trees and rectangular Hilbert paths, and
`HierarchicalCrossings` for coarse travel-benefit selection. Preserve stop reasons and
region IDs through design; check beaches, seam routes and final engine movement afterward.

For reusable start-site work, use `selectSeparatedSites` for bounded toroidal maximin
selection, `resourceFrontages` on an existing movement flood, and `arrangeBuildingGrid`
to check actual disjoint footprints plus access after every proposed building is placed.
`cropSpreadEnvelope` proves containment independently of present fertility or buildings.
Containment on a generated map is terrain only: the engine's saved no-growth flag is
forbidden there (it is for hand-made scenarios such as the tutorial), and the shared
structural check refuses any generated world that sets it. Keep economic thresholds and
module layouts in generators.
