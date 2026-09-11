# Map generators

The map editor, custom-game lobby and study tools share generator control definitions,
including labels, ranges, steps and defaults. Uniform terrain is editor-only.
Normal lobby generation rolls a fresh random map; the preview snapshot is the map launched.

## Development tools

Build the study executable with `scons release=1 map-generator-study`.
`build/src/MapGeneratorStudy --catalog` exports the current control definitions.
Use `python3 tools/map_generator_study.py --help` for seeded generation studies.
Study results and screenshots belong in ignored `artifacts/`, not in this directory.
Record investigation findings and validation results in the relevant pull request.

See [Map generator framework](MAP_GENERATOR_FRAMEWORK.md) for how the shared modules, generator
catalog, resource placement and colony fairness currently work, and
[Adding a generator](ADDING_A_GENERATOR.md) for the registry and module interfaces.
