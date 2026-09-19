# Map generators

The map editor, custom-game lobby and study tools share generator control definitions,
including labels, ranges, steps and defaults. Uniform terrain is editor-only.
Normal lobby generation rolls a fresh random map; the preview snapshot is the map launched.

## Savannah

[Savannah design and validation](SAVANNAH.md) documents the open grassland generator,
its contained crop plots, controls, numerical budgets and supported request envelope.

## The Comb

[The Comb design and validation](COMB.md) describes its interlocking shores,
cross-channel military positions, contained mainland farms and controls.

## Development tools

Build the study executable with `scons release=1 map-generator-study`.
`build/src/glob2 --headless-catalog` exports current control definitions and production
execution capabilities. Bulk runners use [shared tournament execution](../tournaments.md);
`MapGeneratorStudy --catalog` remains a regression-compatible entry point.
Use `python3 tools/map_generator_study.py --help` for seeded generation studies.
Use the normal executable's [map CLI](CLI.md) to generate maps and PNG previews,
preview existing maps or saves, and configure generators with CLI settings or a config file.
The [JSON report reference](REPORT.md) documents the output schema and the meaning
of every metric, including fairness, resources, building room, and travel distances.
Use `python3 tools/map_fairness_tournament.py run smoke` to measure whether generated maps favour
some start positions in real games; see [Map fairness tournament](FAIRNESS_TOURNAMENT.md).
Study results and screenshots belong in ignored `artifacts/`, not in this directory.
Record investigation findings and validation results in the relevant pull request.

See [Map generator framework](MAP_GENERATOR_FRAMEWORK.md) for the shared toolkit a generator is
built from, the designed-generator pipeline, the generator catalog, resource placement and colony
fairness, and [Adding a generator](ADDING_A_GENERATOR.md) for the registry and module interfaces.
[Game rules for map design](GAME_RULES_FOR_MAP_DESIGN.md) collects the engine rules and play
principles every generator designs for, which the generators' comments refer to.
[The world atlas](WORLD_ATLAS.md) documents the real geography compiled in for the Continents
landscape, its sources and licences, and `tools/world_atlas.py` that regenerates it.

[Generator telemetry](TELEMETRY.md) records internal counts, variants, calibration and fallback
choices in CLI JSON reports. Use `tools/map_telemetry.py` to collect a bounded seed matrix and
analyze those observations alongside final-map metrics.

[Braided Delta](BRAIDED_DELTA.md) documents the braided river design, island room budgets,
controls, supported settings and validation contract.

## Recursive layouts

[Fractal maps and recursive geometry](FRACTAL_MAPS.md) documents reusable halves/thirds,
rectangular Hilbert paths, travel-benefit crossing selection, and examples for cities,
reservoirs and folded roads. Keep hierarchy and validate the finished movement graph.

[Lava shield](LAVA_SHIELD.md) documents the volcanic island, its broad coastal gaps
and narrow beach detours, scored towns, crater-rim prize and validation evidence.
Its [paired 504-request bulk study](LAVA_SHIELD_BULK_20260915.md) covers the
control range, smallest layouts, rectangles and dense colony counts.

[Honeycomb isle](HONEYCOMB_ISLE.md) documents the hexagon city on an island: street-sealed blocks,
the river and its bridges, wheat edges and ruins, how an urban-combat concept became it, and its
reliability, control-study, performance and AI-game evidence.

[Karst towers](KARST_TOWERS.md) documents river country among limestone towers: rivers placed between
the rows of homes, terraced paddies of wheat and flooded strips, gated home bowls, doline lakes and
sinkholes, and its growth-potential, control-study, reliability and AI-game evidence.

[Bajada](BAJADA.md) documents a desert mountain front: stone ranges with passes, rows of alluvial fans
whose streams split downhill, identical home fans with their towns on the dry shoulder, gravel and
dunes, and playa lakes, with its review rounds, control-study, reliability and AI-game evidence.

[Central Quarry](CENTRAL_QUARRY.md) documents natural country draining to one lake whose island holds
the only stone on the map: sand bars to a single landing, a sealed island garden, streams, starts
balanced on the walk to the stone, and its eight review rounds, reliability and AI-game evidence.

[Hidden Oasis](HIDDEN_OASIS.md) documents dry canyon country round a sandstone plateau that hides the
map's only algae: one winding gorge sealed by every colony's own level-1 tower, each on a ledge with
a box canyon for a back door, a basin to hold, and its three review rounds, 48 AI games, control
study, profiling and reliability evidence.

[The Gauntlet](GAUNTLET.md) documents a battle arena with two guarded home fronts,
a court circuit around a sealed lake, irrigated gardens and orchards, three layout
families, and its iterative review, rotation games, control studies and profiling evidence.
