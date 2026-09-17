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
| Natural starts balanced on a shared objective | `Points` (`farthestSites`, `closestWalk`), `Growth` (`waterDrySite`, `digPond`, `meanFertilityAround`), `Territories` (`growTerritories`) | Spread by walk within a walk band to the prize, keep the widest spread (`closestWalk`), water the starts that landed dry, and keep what you dig off the routes you balanced. |
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

Extract on the second use even when the first user is an old generator. Central Quarry copied Continents' dry-start pond loop and its closest-walk check. Both became shared helpers (`waterDrySite` returns the dug corners in order so each caller keeps its own telemetry; `closestWalk`), with Continents moved onto them and its golden rows unchanged. A review of the new map then found a real bug in a shared primitive: the Biomes dry reserve dealt its fields to the first tiles in row order. Fixing it at the source (with a maintainer's agreement, since it changes another generator's maps) moved Continents' output by 0–0.6% of its tiles, so the fix was measured, not assumed. Compare old and new dumps tile by tile before calling a shared fix cosmetic or harmful.

Changing a shared primitive can alter many golden maps. Identify all affected generator revisions; record intentional output changes and update those golden rows. Do not bump unrelated versions or force-update a baseline simply to hide an unexplained difference. If the primitive is also used by simulation, follow the repository's stronger simulation compatibility checks.

## Fairness mechanisms and their limits

[Orbits.h](../../../../src/map/generator/shared/Orbits.h) distinguishes:

- Point groups: half-turn for two colonies; quarter-turn for four on square maps; mirrors for four on rectangles; square dihedral symmetries for eight. Check the returned group rather than assuming arbitrary team counts work.
- Translation groups: whole-tile lattice shifts, useful on rectangular tori without a privileged centre. Supported group orders must divide the power-of-two tile count. `latticeSites` also handles other counts with approximately even rows; its `exact` flag matters.
- Wedge copies: separately rasterized rotational shapes agree approximately. Tile rounding, beaches, footprints and deposits can break equality. Validate tolerances rather than calling them exact.

For exact claims, transform undermap **corners** and final **tiles** correctly, transform building anchors as footprints, use orbit-consistent resource selection, equalize deposit amounts, and run `orbitMismatch` on the completed world. A symmetric coastline with independently randomized kits is not a symmetric start. Translation-compatible noise needs the group's period (`latticePeriod`), otherwise a border warp breaks repetition. Randomizing a shared orbit field gives a different map each seed while preserving equal conditions within that map.

For asymmetric landscapes, [BalancedStarts.cpp](../../../../src/map/generator/shared/BalancedStarts.cpp) is an instructive bounded search. It floods distances from gathering neighbors, considers legal swarm footprints, shortlists at most 900 sites, re-scores after modeling the starting colony, and seeks a narrow resource-cost window containing mutually separated starts. This is a heuristic, not an optimal global placement solver, and its legacy placement model is not a drop-in replacement for arbitrary settlement geometry.

Use [StartQuality](../../../../src/map/generator/shared/StartQuality.h) to compare *finished* colonies on wheat/wood access, fertility, resource depth, room, and isolation. What those measurements are worth is fitted to real games ([FairnessModel.h](../../../../src/map/generator/shared/FairnessModel.h), [the method](../../../../docs/map-generators/FAIRNESS_MODEL.md)) and is the same for every generator, so a concept that needs a different bar states it in `validateWorld` rather than by retuning the score. The score is the map's fairness: 1 when every colony is equally likely to win, 0 when one would take the map. It says nothing about whether the starts are any good, so keep absolute viability checks. Reachable wheat can coexist with no building space.

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

Two branches will also name the same new helper differently in kind: one `cropSpreadEnvelope` returned a `Flood`, another the flood's steps. A union declares both and compiles neither (C++ cannot overload on the return type). Keep the richer one, give it the other's extra rule (here, stopping at tiles whose growth flag is off) and make the second branch's callers read the field they wanted.

Near-duplicates survive a batch even after the shared files merge: two ways to spread sites by walking distance (`farthestSites` over tiles, `farthestCells` over a cell graph), two patch planters (`plantPatchNear` returning a count, `growPatchesNear` returning the patches spent across pockets), two starting-access checks (`cropsBesideReach`, `startingAccessFailure`), three separation proofs (`coloniesApart`, `colonyLeak`, `checkGatePartition`). Each was written for its own domain and its own promise, and merging them is a fingerprint-changing refactor of its own, so list them in the framework doc's toolkit table with their differences stated and fold them when a third caller appears, not while the batch is merging.

## Verification that answers design questions

Use an optimized client for measurements and choose build concurrency for available memory. The following are entry points, not a claim they have been run for a new design:

```sh
scons release=1 server=0 map-generator-study map-generator-defaults-test map-generator-golden-test custom-setup-test
build/src/MapGeneratorDefaultsTest glob2-map-design-contracts
build/src/CustomGameSetupHarness
build/src/MapGeneratorGoldenTest glob2-map-design-golden --require-rows
build/src/MapGeneratorGoldenTest glob2-map-design-sweep --sweep
build/src/MapGeneratorGoldenTest glob2-map-design-perf --performance
scons release=1 server=0
build/src/glob2 --list-map-generators
build/src/glob2 --list-map-generators canals
build/src/glob2 --generate-map canals --seed 7 --width 128 --height 256 --teams 4 --output artifacts/map-design/canals-7.map --preview artifacts/map-design/canals-7.png --json artifacts/map-design/canals-7.json
```

Use disposable test profiles, following the [test README](../../../../test/README.md). For intentional output changes, bump the affected revision and run `MapGeneratorGoldenTest <disposable-profile> --update`, then inspect the diff and rerun comparison. A golden hash proves reproducibility of a known snapshot, not quality. Without `--require-rows`, a platform with no baseline rows can report that fact and pass; obtain the affected platform's rows from an actual run rather than treating that as coverage. `--performance` times every generator at its own defaults (256x256, seed 42) and asserts identical bytes, outcomes and telemetry with the internal performance collector on and off; for a broader load, `MapGeneratorProfileFixture <profile-dir> <seed> <rounds> [generator-id...]` (see the [framework doc](../../../../docs/map-generators/MAP_GENERATOR_FRAMEWORK.md)) round-robins every generator at randomly drawn parameters, which is also a fixed-seed/round-count way to compare a shared primitive's before/after cost, and a target for an external sampling profiler.

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

Watch AI colonies through expansion and late resource growth; include each of the newer AIs (Nicowar, Cortex, Cabino, Maxima) rather than relying only on Nicowar's habits, and don't tune for the older Numbi, Castor or Warrush ([which AIs to play](tuning-playbook.md#which-ais-to-play)). Retain maps, settings, saves/replays and relevant tick observations. Human play should answer whether the intended choices happen, remain legible and offer counterplay. If tests pass but play stalls, revise the geometry/resource policy instead of merely loosening acceptance thresholds.

Store bulky study outputs in ignored `artifacts/` and attach or otherwise make evidence available to the reviewer; a local ignored path alone is not a review artifact. Document actual coverage and omissions. Changes confined to generation need generation/serialization evidence; changes to simulation additionally require the repository's save-continuity, replay/network-boundary and cross-platform per-tick checksum checks as applicable.

## Debugging, contracts and release housekeeping

Lessons from rebuilding three generators in one branch (2026-09-16):

- **See the tiles, not the preview.** The native preview paints undermap corners at two pixels a tile and draws no buildings, so one-corner sand lines, beaches, wall gaps and a garden's seal are hard to see. Use `--preview-scale 8` for a sharper preview, or dump the tiles the game sees (`--generate-map --generator ID ... --report terrain --output-dir DIR`) and render a close-up with [`scripts/render_terrain.py`](../scripts/render_terrain.py) around the colony that misbehaves. When a validator fails, print the first offending tile and its design labels to stderr for one run; that found a berm tile, not a courtyard one, behind The Glacis' "gardens open" failure within a minute.
- **Sweep every shape before any tuning.** [`scripts/sweep_shapes.sh`](../scripts/sweep_shapes.sh) generates every map shape and colony count with three seeds and lists refusals. Run it after every geometry change: a refusal that is not "too many colonies" is a bug. When a home shrinks to fit a crowded map, scale its floors with it (never below a documented share) and try the other variants (bastion counts, the arc-less garden) before refusing.
- **Contracts that exercise the promises.** A generator's contract (`test/MapGeneratorContracts.h`) generates its envelope, both resource extremes, then runs a couple of thousand `growResources` passes on the abundant map and requires the validator still to pass (containment), then damages the world and requires refusals: remove a wall stone on a map whose stone amount is 0 (so the only stone is designed wall), plant a crop where the concept forbids one, clear a village's wheat. A damaged-world check needs a tile that is certainly the feature; search outward and keep the first tile the validator refuses rather than guessing a coordinate.
- **Design failure messages are translated.** The lobby shows `validateRequest` (the design's `failure`) through the string table, so every message a `design()` can return needs a key in every `data/texts.*.txt`, like control labels and choice names. Reuse existing messages where they fit ("Too many colonies for this map; use a bigger map or fewer colonies."). `test/test_translations.py` refuses a value identical to its English source in any catalog: "Bastions" is also French and Catalan, "Six" is French, "Desert" Catalan, so prefer labels that translate distinctly ("Four-pointed", not "4" or "Four"). Run `python3 data/check_translations.py` and the test before committing.
- **Golden rows for an unreleased revision.** Changing output again at a revision this branch already bumped makes `MapGeneratorGoldenTest --update` refuse ("changed without a revision bump"); pass `--force` for a revision not yet released. Regenerate rows on every platform that has them: build the branch on a Linux host (a tarball of `git ls-files` and the host's own scons), run `--update` there, copy only its `linux-x86_64` lines into the local table, and confirm with `--require-rows` that only your generators' rows changed.
- **Removing a primitive.** When a rebuild orphans shared code (Bases, Compounds and Lots went with the premade bases), grep for every include and symbol, move what survives into the primitive that owns the concept (`wallStanding` into `Walls`), delete the files from `src/SConscript`, rewrite their toolkit checks around what remains, remove definition hooks nothing uses (`startingWorkers`), their translation keys in every language, and their rows in the framework document's primitive table and in these references.
- **Profile with a fixed output corpus.** `MapGeneratorProfileFixture <profile> <seed> <rounds> [generator-id...]` drives one generator in a loop for macOS `sample` (or `perf`); read the call tree by caller, since inlined design code shows up under `generate`. Before touching anything, record a corpus of a couple of hundred requests (defaults at every shape plus random controls and each control's extremes) with `MapGeneratorStudy ... dump=... result=...`: the `STUDY` fingerprint, a hash of the tile dump and the telemetry records. Saved `.map` files differ byte for byte between identical runs, so hash the dump, not the file. Check the corpus after every change, and the golden rows of every generator a shared primitive touches. Time before and after with cold single-map runs of the old and new binaries (a copy of the old `MapGeneratorStudy` built from the saved sources), not with the fixture: its request randomization already builds the design before the timer starts.
- **The design is asked for three times.** The request check (`designFailure<design>`), `generate` and `validateWorld` each build the design from the request. A design that depends only on the request (every draw from a named stream) can be cached per thread keyed on the request, provided its telemetry is recorded into the layout and replayed into each context in order; Honeycomb isle's contract generates A, B and A again and requires the same map and records. With whole-map distance transforms replaced by local disc scans (a feature's margin over a few thousand tiles, not a transform per colony), generation became 3.5–4.4× faster with an identical 214-map corpus.
- **A cheaper request check, and memoised preferences.** A design whose failures are mostly seed-dependent (no room for these colonies on this seed) doesn't need to be built for the lobby's request check. Central Quarry's `validateRequest` checks only what the request alone decides (colony count, whether the lake fits, from a shared `lakeSizeFor`) and lets seed failures surface at generation, which removed a third of its cost. Its other hot spot was `farthestSites`' `prefer` callback: the search asks it of the same candidates across every trial, target and preference level, and each answer was a flood. Memoising answers per tile and precomputing the cheap mask (fertility over a square) before the floods cut 512×512 generation from 10–16 s to about 3.7 s. A second profile, by source line, then showed 78% of what remained was the design built twice (for `generate` and again for `validateWorld`). Caching it per thread as Karst towers does (telemetry replayed, named streams wound on), computing the square means for every tile at once with running sums (`meanFertilityField`) instead of a 21×21 loop per candidate, and marking each lake's keep-out with a local stamp instead of a whole-map dilation per lake gave 0.10 / 0.25 / 1.17 / 1.62 s at 128/4, 256/4, 512/8 and 512/12, faster than Continents, with identical golden rows and the `--telemetry` check passing. Profile by caller line (`sample` output parsed for the generator's own `file:line` frames); function-level totals hid that most of the time was one call made twice. A third profile found the rest in whole-map work repeated per colony, and every fix was exact:
  - bounded floods (`floodFrom` with a limit) where only "within N steps?" matters;
  - `closestWalk` stopping each flood at the closest pair so far, after one whole flood proves every site is reachable;
  - `waterDrySite` measuring a pond's effect on a 61×61 window around the site (the mean reads 10 tiles, the growth kernel 15, a beach one corner), instead of rebuilding the whole map's growth field per pond;
  - margins stamped round a route's tiles instead of dilating the map;
  - territories drawn by one multi-source walk instead of balanced noisy growth.
  
  Central Quarry ended at 0.08 / 0.19 / 0.84 / 1.15 s. The two shared helpers made Continents 2.3× faster at 512×512 with 8 colonies (1.81 s to 0.78 s) with its golden rows unchanged. A per-colony or per-feature call that touches every tile of a 512 map is the first thing to look for.
- **Exact pruning in shared geometry.** `warpCorners` measured tile-by-tile gaps between obstacles; skipping a tile already at least the best distance from the other obstacle's box is exact by the triangle inequality on the torus, and every generator's golden rows stayed the same. A bucketed pair search that the profile did not blame gave nothing and was reverted: optimize what the samples show.
- **Refactor against a dump corpus, and know which order matters.** Karst towers' 700-line design became ten named stages with byte-identical output, checked by comparing `--report terrain` dumps of the old and new binaries on defaults at eight shapes and 72 random control rolls. Draws from named streams (`context.bounded`, `context.stream`) are independent per name, so stages may be reordered as far as their inputs allow, as long as each stream's own sequence is kept; `Map::setResource` draws from the synchronised gameplay stream in call order, so the order of planting in `generate` may not change. A design retry loop (shrink and try again) is deterministic for the same reason: `validateWorld` replays it from the request and consumes the same named draws. A cached design must leave the asking context as building it would have: replay its telemetry (`GenerationTelemetry::replay`) and wind on the streams it drew from, taken from the building context's `namedStreams()` rather than a hand-kept list that a new draw can silently miss.
- **Geometry checks that pass on paper.** Three Central Quarry failures were checks that looked right:
  - *A room count over pieces.* The quarry's room counted free island tiles, but the tiles could lie in several unconnected pieces, and the outcrop stopped at 6 of 9 tiles in a small one. Count the largest connected component the feature will grow in, and seed it there.
  - *A ray stepping back.* A ray marched in half-tile steps and rounded to corners can step back onto the shape it left on a diagonal, so the "shore" was found half a tile from the island. Skip the source mask when looking for the far side.
  - *A slack without a margin.* A walk-spread tolerance of exactly twice the band refused maps whose swarm stood a few tiles off its designed site. Give checks on the finished map a margin for the swarm's placement.
- **Tool aliases.** `MapGeneratorStudy` and the structured `--generate-map --param` rewrite some option names for older generators (`craters` becomes `lake-density`, `river` a width, `w`/`h`, `smooth`, `extra`, `island`, `beach`), so a control named `craters` could not be set from them ("Unknown option lake-density"). Choose control ids outside that list. `--preview-scale` accepts only 2, 4 or 8.
- **Local study plumbing.** macOS `xargs -I{}` caps a substituted command at 255 bytes and fails silently on long generate commands; pass each job line whole with `tr '\n' '\0' < jobs.txt | xargs -0 -n 1 -P 7 bash -c`. Piping `glob2 --generate-map` into `head` ends it before the JSON report is written; redirect to a log instead. zsh does not split `$var` into words (`set -- $wh` and `$starts` arguments stay one word); use `$=var` or run the loop under bash. A terrain dump's first line is its size, not a row. macOS ships bash 3: no `declare -A`, so write the loop plainly. BSD `grep -Z` decompresses instead of printing NUL-separated names; use Python to rewrite files by list. Don't rebuild `glob2` while a batch is running against it: every map launched during the relink fails to exec and reports nothing, which looks like a missing stage rather than a crash.
- **Windows macros bite only on MinGW.** A local named `near` compiled on macOS and Linux and failed only in the Windows CI job (`far` and `small` are the other usual ones). Grep a new generator for them before pushing.
- **A remote Linux build from a Mac checkout.** Syncing the working tree with `rsync` also copies the Mac's `config.h`, `config.log` and `options_cache.py`, which break a Linux configure; exclude them, and start the remote build with `nohup ... </dev/null >log 2>&1 &` so the SSH session can end. Re-sync and regenerate both platforms' rows once more after the last change to the generator, including validator-only changes, since a row records whether each map passed.
- **Linux golden rows for a new generator.** CI's `--require-rows` passes when the Linux table simply has no rows for a new generator, and its row-printing step runs only on failure, so CI never hands you the rows. Build the golden test on a Linux host from a tarball of `git ls-files`, run `MapGeneratorGoldenTest <profile> --update` there, check that the only Linux lines that changed are the new generator's (sort and diff the host's Linux rows against the local ones), and copy just those rows in.
- **Re-roll after every validator change.** A validator is code that can refuse correct maps: tightening Bajada's town-ring check made one roll in 2,000 fail a map whose ring was whole. Run the random-roll pass again after any validation change, not only after geometry changes.
- **Have the translations reviewed.** Give a separate agent the strings with what each control does in the game, and ask it to check meaning, idiom, label length, script and consistency with the catalog. Bajada's "Stream reach" (how long the streams run) had been translated as range or scope, a weapon's reach, in 25 of 33 catalogs; "Passes" needs the mountain sense, and a geology term the players won't know ("bajada", "playa") should become the local word for alluvial fans or salt lakes rather than an English fallback the tests refuse. The scaffold writes English placeholders into every catalog, and `check_translations.py` counts a placeholder as translated only when it differs from English, so translate every file yourself first. Central Quarry's review of 32 catalogs then found:
  - a misspelled Greek noun;
  - Basque grammar (an intransitive verb given an object);
  - Slovenian using a rarer word for "map" than the rest of its file;
  - French "Bois" for Woodland, which is already the catalog's word for the Wood resource, so the slider read as "Wood";
  - Spanish and Hungarian formal imperatives next to informal ones in the neighbouring messages;
  - Turkish and Persian words for "sand bar" that meant embankment or watery hill.

  Ask the reviewer to compare each new term with the file's existing keys for the same thing (`[Sandbars]`, `[Lake size]`, `[Wood]`) and with the register of the neighbouring messages.
- **Cost at the largest size.** Time the largest map with the most colonies next to a comparable generator (512x512 with 12 colonies: The Glacis 2.3 s, Forts 2.0 s) and remember the lobby rolls five candidates. A whole-map scan inside a per-feature loop (a pond placement checking every tile for neighbours) is the usual culprit; keep a list of placed features instead.

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
`cropSpreadEnvelope` proves containment independently of present fertility or buildings.
Containment on a generated map is terrain only: the engine's saved no-growth flag is
forbidden there (it is for hand-made scenarios such as the tutorial), and the shared
structural check refuses any generated world that sets it. Keep economic thresholds and
module layouts in generators.
