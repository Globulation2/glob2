# Map generators

The map editor, custom-game lobby and study tools share generator control definitions,
including labels, ranges, steps and defaults. Uniform terrain is editor-only.
Normal lobby generation rolls a fresh random map; the preview snapshot is the map launched.

## Development tools

Build the study executable with `scons release=1 map-generator-study`.
`build/src/MapGeneratorStudy --catalog` exports the current control definitions.
Use `python3 tools/map_generator_study.py --help` for seeded generation studies.
Use `python3 tools/map_fairness_tournament.py run smoke` to measure whether generated maps favour
some start positions in real games; see [Map fairness tournament](FAIRNESS_TOURNAMENT.md).
Study results and screenshots belong in ignored `artifacts/`, not in this directory.
Record investigation findings and validation results in the relevant pull request.

See [Map generator framework](MAP_GENERATOR_FRAMEWORK.md) for the shared toolkit a generator is
built from, the designed-generator pipeline, the generator catalog, resource placement and colony
fairness, and [Adding a generator](ADDING_A_GENERATOR.md) for the registry and module interfaces.
[Game rules for map design](GAME_RULES_FOR_MAP_DESIGN.md) collects the engine rules and play
principles every generator designs for, which the generators' comments refer to.
