# Implementing and verifying the design

Source anchors: [adding a generator](../../../../docs/map-generators/ADDING_A_GENERATOR.md), [framework](../../../../docs/map-generators/MAP_GENERATOR_FRAMEWORK.md), [development conventions](../../../../docs/development-notes.md). Consult these for full interfaces; the guidance below connects them to design decisions.

## Worked design: a city around contested gardens

This is a proposed application of the toolkit, not an existing generator or a validated balance recipe. Suppose the user wants walled city districts with a fertile common garden: a safe opening, then fights over expansion space and food.

Start with the decisions. Each colony must establish its economy within a district, but a growing colony should benefit from leaving it. Give the common garden enough usable grass and accessible crop edges to reward occupation. If all surplus is behind one permanent tower-covered gate, the map encourages waiting rather than contest; provide a flank or another relevant objective. A stone perimeter makes the playable shape durable, while internal sand streets keep access open but consume building land.

Represent `homeOf`, playable ground, structural walls, roads, crop plots and objective masks separately in a layout. Choose a lattice or region partition suitable for the rectangle and team count. For exact supported groups, construct one domain and transform it; for three or five teams, allocate approximately even regions and measure the finished result. Reserve home building capacity with `Room`, then account for the swarm, worker ring, crop containment and street corners. The home area's tile count is only a rough initial budget.

Place water to sustain the garden and starter plots, and use `Growth` to check whether that water also makes streets or reserved expansion grass fertile. A dry building district with a contained irrigated plot is one possible solution. Build beaches and derive final pure tiles before placing stone: an undermap sand road that meets a coastal beach can spoil wall grass and create a breach. Check both sides of every gate and the torus seam.

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
| Renewable plots and growth containment | `Growth`, `Farmland`, `Planting`, `Resources` | Choose water and dry buffers before planting; stock only terrain that can sustain the intended role. |
| Homes and finite building capacity | `Homes`, `Room`, `Territories`, `Settlements` | Measure complete footprints, fair regional capacity, and space for all starting workers. |
| Fronts, shortcuts and defense | `Channels`, `Roads`, `Contact`, `Walls`, `Towers` | Measure actual entry costs and firing envelopes; preserve structural boundaries during repairs. |
| Ordered generation and start selection | `Pipeline`, `Terrain`, `BalancedStarts`, `StartQuality` | Select the composition appropriate to the map and score the finished colony. |

All modules are under [shared/](../../../../src/map/generator/shared/). Read implementations as well as declarations. `shared/legacy/` explains older generators but its area-grid representation does not compose directly with current tile masks; use the current toolkit for new designs.

A useful generator has a named options struct, a layout struct, a reconstructible `design()`, a short stage-oriented `generate()`, and a validator tied to its play contract. Keep pure design separate from `Game` mutation. Run `layBeaches` before deriving final pure-grass masks; use `writeUndermap` to bake the terrain. Set a meaningful `context.stage` at potentially failing stages and return an actionable detail.

The normal ordering is layout → terrain → teams/settlements → kits and ambient resources → swarm clearance and reachable crop guarantees → required road reopening → cramped-start relief for non-default amounts → final validation. This is a composition, not a mandatory superclass. A landscape often places resources before searching for starts. A wall map must protect its structural deposits from every clearing stage. Explain any different order in terms of the invariant it preserves.

`designMismatch` only checks that reconstruction succeeded and dimensions agree; its name does **not** mean it compares all terrain to the layout. Add the actual checks the idea needs: homes retained, gates open, roads wide enough, ponds present, target access, room per region, dry forests, symmetry, or contact spread. `walkFromFirstColony` establishes a route from the first colony's workers to each other colony; it does not prove route width, useful expansion, or long-term openness.

### Fix the primitive at the right boundary

Before implementing a second flood, beach pass, clump grower, route clearer, kit frame, or swarm-clearance routine, search for the operation in `shared/` and its callers. A defect affecting multiple maps belongs in the helper when its contract is wrong. Add an explicit parameter when policies differ (for example a protected wall mask), instead of recognizing generator names inside a helper.

Demonstrate a bug with a retained request/seed or a focused fixture that fails on the old code. Cover the helper's actual invariant: wrapping, pure-terrain conversion, resource protection, complete footprints, finite frontier exhaustion, or stable tie-breaking. Run affected generators, including the caller that originally worked. Do not silently turn “repair” into punching a new shortcut through the map's intended hard boundary.

Changing a shared primitive can alter many golden maps. Identify all affected generator revisions; record intentional output changes and update those golden rows. Do not bump unrelated versions or force-update a baseline simply to hide an unexplained difference. If the primitive is also used by simulation, follow the repository's stronger simulation compatibility checks.

## Fairness mechanisms and their limits

[Orbits.h](../../../../src/map/generator/shared/Orbits.h) distinguishes:

- Point groups: half-turn for two colonies; quarter-turn for four on square maps; mirrors for four on rectangles; square dihedral symmetries for eight. Check the returned group rather than assuming arbitrary team counts work.
- Translation groups: whole-tile lattice shifts, useful on rectangular tori without a privileged centre. Supported group orders must divide the power-of-two tile count. `latticeSites` also handles other counts with approximately even rows; its `exact` flag matters.
- Wedge copies: separately rasterized rotational shapes agree approximately. Tile rounding, beaches, footprints and deposits can break equality. Validate tolerances rather than calling them exact.

For exact claims, transform undermap **corners** and final **tiles** correctly, transform building anchors as footprints, use orbit-consistent resource selection, equalize deposit amounts, and run `orbitMismatch` on the completed world. A symmetric coastline with independently randomized kits is not a symmetric start. Translation-compatible noise needs the group's period (`latticePeriod`), otherwise a border warp breaks repetition. Randomizing a shared orbit field gives a different map each seed while preserving equal conditions within that map.

For asymmetric landscapes, [BalancedStarts.cpp](../../../../src/map/generator/shared/BalancedStarts.cpp) is an instructive bounded search. It floods distances from gathering neighbors, considers legal swarm footprints, shortlists at most 900 sites, re-scores after modeling the starting colony, and seeks a narrow resource-cost window containing mutually separated starts. This is a heuristic, not an optimal global placement solver, and its legacy placement model is not a drop-in replacement for arbitrary settlement geometry.

Use [StartQuality](../../../../src/map/generator/shared/StartQuality.h) to compare *finished* colonies on wheat/wood access, fertility, resource depth, room, and isolation. Its score is `worst * fairness^exponent`, with fairness `worst / best` when best is positive. Adjust weights/scales to express the concept, retaining absolute viability checks. A higher isolation reward can prefer a dull or unreachable start; reachable wheat can coexist with no building space. Do not tune the score solely to make your preferred screenshot win.

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

High amounts can remove legal starting sites or wall colonies into crops. Widen a bounded site search when sensible; run `reopenCrampedStarts` or the height-field equivalent at non-default amounts, passing protected walls. It uses `openCrampedStarts` to seek 16 reachable 4×4 anchors within 24 steps, then rechecks crop supply. These are emergency heuristic targets, not a promise of sixteen independent buildings or a substitute for deliberately roomy defaults. Inspect its result and validate your stronger map-specific minimum.

## Registration and style

From the repository root, `python3 tools/new_map_generator.py <id> "<Display name>"` scaffolds the header/source, registry entry, SCons source and translation keys. It mutates several files; inspect its output. The current scaffold is a starter design, not a finished implementation: add strategic variation, actual abundance scaling, site dealing where needed, design-specific validation and a `validateRequest` such as `designFailure<design>` when appropriate. Remove the generated TODO comments once the real design is described.

Take the next free legacy id and never reuse a retired one (the framework doc's catalog lists the retired numbers). The numeric id is a compatibility identity: seeds derive from the request seed and the stream names, so renumbering a generator during review changes no fingerprint, only the id column of its golden rows. Several branches written in parallel will pick the same number; expect to renumber before merging.

Put a generator's own contract check (its supported shapes, its variants, what its validator refuses when the world is deliberately damaged) in `test/MapGeneratorContracts.h` as one function per generator, called from `generatorContracts()`; the shared-primitive fixtures stay in the toolkit and landscape check headers. A generator that refuses a shape must do so through its request check, which the defaults test consults before asserting that every generator handles a rectangle.

Keep review evidence out of the main history: previews and the written findings belong in `docs/artifacts/<generator>/` or the design doc, and the bulk archives, saved maps, replays, logs and one-off scripts on an `evidence/<generator>` branch that the docs link to. A design doc records what was measured and what it changed; a validation log that names one session's local directories does not survive its author.

For manual registration, use one definition in [GeneratorRegistry::builtins](../../../../src/map/generator/core/GeneratorRegistry.cpp), with a unique stable string and nonnegative numeric ID, display translation key, revision, availability, controls and callbacks. Preserve existing IDs and catalog order; add the new entry without reshuffling the rest, and keep editor-only Uniform last. Add the source in [src/SConscript](../../../../src/SConscript). Update translation keys/tables and translate scaffold placeholders through the repository workflow. Normal preferences/history consume the metadata; do not add duplicate UI range/default tables. Keep the old descriptor codec unchanged for new options.

Use PascalCase header/source names, `#pragma once`, the current SPDX header, named constants for design budgets, typed options, explicit dependencies, and small local helpers only for genuinely map-specific operations. Follow root `.clang-format` (tabs, Allman braces, 100 columns) on touched generator C++. Explain the map's play rationale, units and non-obvious heuristics in comments; keep current implementation comments accurate. Avoid Windows macro names `near`, `far`, and `small` as identifiers.

Randomness comes from `GenerationContext::stream`, `bounded`, and `shuffle` with stable names separating layout, homes, resources and decoration. Do not use wall-clock reseeding, `rand`, `std::hash`, global mutable noise caches, or gameplay RNG directly. [GenerationService](../../../../src/map/generator/core/GenerationService.cpp) bridges engine mutations by seeding/restoring its RNG. Generate into a fresh `Game`; discard a failed partially mutated world. Bounded retries use recorded derived seeds and fresh games. Same request/seed/revision repeatability is a per-platform contract here; generator floating-point output is not promised identical across platforms.

## Several generator branches at once

Generators written in parallel meet in the same handful of files: the registry, the SCons source list, the golden table, the framework doc's catalog and toolkit tables, the defaults test's call list and the toolkit checks' `PASS` line. Merging them by line is how these files get broken: a union merge duplicates registry entries (the registry throws `Invalid generator registration` for a repeated id, and every CI job that loads it fails), splices two `puts` calls into one, drops a brace between two functions that shared a closing line, and a line-level dedupe silently deletes repeated code lines such as `for (const auto &d : definitions)`. Merge these files by key instead: one entry per generator in the registry and source list, the golden table as a three-way merge of rows keyed on platform, id, size, colonies and seed (the revision is not part of the key, so a regenerated row replaces its predecessor rather than sitting beside it), catalog and toolkit rows keyed on their first cell, and the `PASS` line rebuilt from its fragments. Never run a line dedupe on a code header.

Stack the branches in one order, rebase each onto the previous one's tip, and normalize the shared files after every rebase; then a branch's fingerprints computed at the top of the stack are valid for every branch below it, because each branch's own golden table already showed its shared-code changes leaving the other generators alone. One Linux build at the top therefore supplies the Linux rows for every generator in the stack. Build and run the tests on each branch before pushing it: a stack that compiles at the top can still fail at a middle branch whose registry lost an entry. Keep build logs and other session files out of commits (`git add` the files you changed, not the tree).

Two branches will also generalise the same helper in two directions: one turns `placeTower` into a starting-building placement with coverage and stocked supplies, another into `placeBuilding` for any completed building. Neither is wrong, and a concatenating merge keeps both bodies and compiles neither. Merge them into one search with each caller's options on top (here `startingBuildingSite` under `placeTower`, `placeStartingBuilding` and `placeBuilding`), keep every caller's exact behaviour so the fingerprints hold, and say so in the header. The same goes for two branches that each revise a shared function: take the revising branch's version whole and re-apply the other branch's purely additive functions as a patch, rather than trusting the union.

Near-duplicates survive a batch even after the shared files merge: two ways to spread sites by walking distance (`farthestSites` over tiles, `farthestCells` over a cell graph), two patch planters (`plantPatchNear` returning a count, `growPatchesNear` returning the patches spent across pockets), two starting-access checks (`cropsBesideReach`, `startingAccessFailure`), three separation proofs (`coloniesApart`, `colonyLeak`, `checkGatePartition`). Each was written for its own domain and its own promise, and merging them is a fingerprint-changing refactor of its own, so list them in the framework doc's toolkit table with their differences stated and fold them when a third caller appears, not while the batch is merging.

## Verification that answers design questions

Use an optimized client for measurements and choose build concurrency for available memory. The following are entry points, not a claim they have been run for a new design:

```sh
scons release=1 server=0 map-generator-study map-generator-defaults-test map-generator-golden-test custom-setup-test
build/src/MapGeneratorDefaultsTest glob2-map-design-contracts
build/src/CustomGameSetupHarness
build/src/MapGeneratorGoldenTest glob2-map-design-golden --require-rows
build/src/MapGeneratorGoldenTest glob2-map-design-sweep --sweep
scons release=1 server=0
build/src/glob2 --list-map-generators
build/src/glob2 --list-map-generators canals
build/src/glob2 --generate-map canals --seed 7 --width 128 --height 256 --teams 4 --output artifacts/map-design/canals-7.map --preview artifacts/map-design/canals-7.png --json artifacts/map-design/canals-7.json
```

Use disposable test profiles, following the [test README](../../../../test/README.md). For intentional output changes, bump the affected revision and run `MapGeneratorGoldenTest <disposable-profile> --update`, then inspect the diff and rerun comparison. A golden hash proves reproducibility of a known snapshot, not quality. Without `--require-rows`, a platform with no baseline rows can report that fact and pass; obtain the affected platform's rows from an actual run rather than treating that as coverage.

The current golden `--sweep` is deliberately small: selected square sizes/counts, three or five seeds, passing an accepted cell if at least one succeeds. It skips invalid combinations and does not cover rectangles, all controls, 64-tile maps or every team count. Inspect success rates and supplement it. A cell surviving only one of five trials is a warning even though the harness passes.

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

Use [report metric definitions](../../../../docs/map-generators/REPORT.md) when interpreting JSON: 4×4 sites are overlapping anchors, terrain and underlying terrain differ, unreachable costs are `null`, static fertility is not yield per tick, and saved-game reports describe the current snapshot. Retain JSON beside `.map`: saved maps do not preserve the whole generation request. Preview mosaics across seeds/settings expose visual variety and seams; inspect actual game views for building access and fighting scale.

For fairness, use [the tournament](../../../../docs/map-generators/FAIRNESS_TOURNAMENT.md) with selected generators and a bounded budget, for example starting from `python3 tools/map_fairness_tournament.py run smoke --help`. Full cyclic team rotations over an unchanged map separate start position from team-index effects. Use multiple map seeds and engine seeds. Read decisive-game results, cap share and uncertainty, not just pooled winners. The existing study documents examples where start scores missed dominant positions; regard that as a reason to measure expansion/fronts, not evidence that today's revised generator still has the same result.

Watch AI colonies through expansion and late resource growth; include the AIs players are expected to use, rather than relying only on Nicowar's habits. Retain maps, settings, saves/replays and relevant tick observations. Human play should answer whether the intended choices happen, remain legible and offer counterplay. If tests pass but play stalls, revise the geometry/resource policy instead of merely loosening acceptance thresholds.

Store bulky study outputs in ignored `artifacts/` and attach or otherwise make evidence available to the reviewer; a local ignored path alone is not a review artifact. Document actual coverage and omissions. Changes confined to generation need generation/serialization evidence; changes to simulation additionally require the repository's save-continuity, replay/network-boundary and cross-platform per-tick checksum checks as applicable.

## Internal telemetry is part of implementation and tuning

Follow [the telemetry contract](../../../../docs/map-generators/TELEMETRY.md). Instrument decisions
where they occur, with typed scalar measurements, named choices and fallback events. Record both
a budget and its achieved amount; record the local colony/feature subject. The service retains
only the actual attempt's trace, including failures; validation reconstructions are uncollected.
JSON export is outside generation, and ordinary generation does not enable collection.

Keep the cost negligible: observe existing intermediates and return values, add small counters
to existing loops, guard telemetry-only feature summaries and dynamic keys, and never recompute
paths or scan grids for logging. Measure enabled/disabled generation costs in the all-generator
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

See [fractal geometry and worked examples](../../../../docs/map-generators/FRACTAL_MAPS.md).
Use `RecursiveGeometry` for integer region trees and rectangular Hilbert paths, and
`HierarchicalCrossings` for coarse travel-benefit selection. Preserve stop reasons and
region IDs through design; check beaches, seam routes and final engine movement afterward.

For reusable start-site work, use `selectSeparatedSites` for bounded toroidal maximin
selection, `resourceFrontages` on an existing movement flood, and `arrangeBuildingGrid`
to check actual disjoint footprints plus access after every proposed building is placed.
`preventResourceGrowth` protects tile masks with the engine’s existing serialized flag.
`cropSpreadEnvelope` honors these flags and proves containment independently of present
fertility or buildings. Keep economic thresholds and module layouts in generators.
