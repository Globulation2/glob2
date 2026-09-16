# Map generators

The map editor, custom-game lobby and study tools share generator control definitions,
including labels, ranges, steps and defaults. Uniform terrain is editor-only.
Normal lobby generation rolls a fresh random map; the preview snapshot is the map launched.

## Savannah

[Savannah design and validation](SAVANNAH.md) documents the open grassland generator,
its contained crop plots, controls, numerical budgets and supported request envelope.

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
