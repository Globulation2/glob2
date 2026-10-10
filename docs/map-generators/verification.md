# Verify a generator change

Use contracts, map inspection and populated games for different questions: legal geometry, visible design, and economy/routes during play. A successful generation is not proof of human fun.

## Compatibility notes

- A `GeneratorDefinition::legacyId` is a stable compatibility identifier, not a display or sort
  order — `GeneratorRegistry::builtins()`'s constructor order is purely the product-facing
  catalog order. Once assigned, an id is never reused, including after a generator is retired.
- A generator's `revision` changes only when a change to `generate()` changes what a given seed
  actually produces; a pure refactor, comment or renumbering change leaves it untouched. Bumping
  it is how a reviewer knows a seed's output isn't expected to match a prior build byte-for-byte.
  Shared colony-placement changes count too: concrete terrain variants consume the engine RNG,
  so changing the number or order of terrain brush passes can change fingerprints even when
  the cleared footprint is identical. Generators that place resources afterward can also change
  resource amounts and positions. Refresh each affected generator's revision and each existing
  platform's golden rows from that platform's actual output.
- `GeneratorControl`s are validated by `GeneratorRegistry`'s constructor: every control's default
  must land on a valid step from its minimum, and (`allowedValues` aside) `(maximum - minimum)`
  must be evenly divisible by `step`. A `GeneratorControl::Kind::Toggle` control must be exactly 0 to 1 in
  steps of 1, with no allowed values, power-of-two formatting or terrain weight; the lobby and
  editor show it as a checkbox. Every control label needs matching entries in
  `data/texts.en.txt` (`[Label]` / `Label`) and `data/texts.keys.txt` (`[Label]`), the same as any
  other UI string.

## Verification tools

- `src/map/generator/MapGeneratorStudy.cpp` builds to `MapGeneratorStudy`, a CLI harness that invokes the real
  production generators directly: `MapGeneratorStudy <method> <seed> <profile-dir> [key=value...]
  [tuning] [headroom] [quality] [dump=path] [overlay=kind]`. `quality` reports `StartQualityReport`/
  `ColonyQuality` per colony; `dump=` writes a plain-text terrain/resource grid for direct
  inspection or scripted flood-fill checks; `overlay=` writes one measure per tile beside it
  (`growth`, the crop growth chance; `sites`, where a 4x4 building fits; `chop`, the cost from the
  nearest colony clearing crops on the way; `owner`, which colony that is); `--catalog` dumps every registered generator's
  controls as JSON, with each control's `kind` (`range` or `toggle`).
- `src/map/generator/MapGeneratorDefaultsTest.cpp` is the `MapGeneratorDefaults` suite of
  `glob2-engine-tests` (`python3 test/run_tests.py --filter 'MapGeneratorDefaults/*'`), asserting the
  registry's and every control's contract: discrete domains, shape bounds, topology, home
  footprints, exact worker counts, seed repeatability and RNG stream isolation, the
  lobby/editor UI's own control-editing behavior, and the shared toolkit's own guarantees:
  the point dispersion ends at a mutual best response checked against a brute-force score,
  the distance flood matches a Chebyshev oracle on the torus, with obstacles and repeated
  sources, and `src/map/generator/MapGeneratorToolkitChecks.h` checks every module of `shared/` on a map
  built by hand — floods and their limits, beaches and islands, patches and algae bands, the
  cheapest route, strokes and shape fills, fields and clumps, settlements and the colony walk, the crop guarantee through and around a
  wall, a buried colony's room, balanced starts, per-landmass scatter, lattice noise, the
  wedge frame and the context's shuffle — so a change to a module fails there before it shows
  up as a changed golden fingerprint downstream. `src/map/generator/MapGeneratorLandscapeChecks.h` does the same
  for the landscape modules: symmetry groups against their orbits, morphology and the distance
  transform against brute force, crop growth and dry zones, cost models and route opening, sites
  and cell graphs, pattern wavelengths and seamless stripes, wandering paths, the channel
  arithmetic against `towerReach`, building room, region homes and every biome kit.
- `src/map/generator/MapGeneratorGoldenTest.cpp` builds to `MapGeneratorGoldenTest` and keeps the revision
  rule honest. `test/map-generator-golden.txt` records, per platform, the fingerprint (terrain,
  resources and colony starts) every registered generator produces for three seeds at 256, one
  at 128 and 512, and one each with two and eight colonies, keyed by the generator's revision.
  `MapGeneratorGoldenTest <profile>` regenerates this platform's rows and fails on any map that
  changed at an unchanged revision or any generator whose revision moved without the table
  following; `--update` rewrites this platform's rows and refuses (without `--force`) to record
  a changed map under an unchanged revision; `--print` writes the rows to stdout, which is how
  a platform's rows are first bootstrapped from a CI log. Generation is deterministic per
  platform, not across platforms, so rows carry the platform they were made on. A platform with
  no rows reports and passes, so a new machine can run the check before its rows exist; CI
  passes `--require-rows`, which fails instead, so the table has to carry rows for every
  platform CI builds on (`linux-x86_64` beside the maintainers' `macos-arm64`). `--sweep` rolls
  every playable landscape at the colony counts and sizes the lobby offers, five seeds at 128
  and 256 and three at 512, prints the success rate per cell and fails any valid cell where no
  seed generated: that is what a player would see as a failed generation. CI runs all three.
- `src/map/generator/MapGeneratorProfileFixture.cpp` builds to `MapGeneratorProfileFixture
  <profile-dir> <seed> <rounds> [generator-id...]`, a load generator for external sampling profilers
  (macOS `sample`, Linux `perf record`): it round-robins every registered generator (or only the ids
  named) for `rounds` passes,
  drawing shared and generator-specific controls at random each attempt the same way
  `GenerationRequest::randomizeControls` does, and prints a per-generator attempt/success/timing
  table. It is not wired into CI and makes no coverage claim; point a profiler at its PID while it
  runs, or use its own timings for a quick before/after comparison at a fixed seed and round count.
- The normal client's [map CLI](cli.md) generates maps and PNG previews with
  `map generate`, loads maps/saves with `map preview`, and lists settings with
  `map generators`. It supports config files and CLI controls, and reuses the lobby/picker preview renderer.
  Compare a new generator with its nearest neighbours at 128, 256, and 512 tiles
  using the documented batch commands before showing it.
- `tools/new_map_generator.py <id> "<Display name>"` scaffolds a generator that builds, registers
  and passes its own validation: header and source in the designed shape (colonies on a lattice,
  a home and pond each, the kit and crop guarantee), the registry entry, the SConscript line, the
  next unused legacy id and its translation keys with English placeholders. Its golden rows come
  from `MapGeneratorGoldenTest <profile> --update`.
- The unit suite (`scons unit-tests && python3 test/run_tests.py --binary unit`) covers the
  rest of the engine and must stay green alongside all of the above.

Generators validate their own construction results rather than trusting the geometry to always
succeed: a moat must connect to land at both bridge ends, jagged outlines must leave legal
settlement footprints, and Fjord's core must keep every player peninsula connected (outside
lake-connected mode, where that's expected not to hold). Difficult small or crowded combinations
can still fail outright, but do so with a reproducible stage diagnostic, and are discarded by
`GenerationService`'s candidate sampling rather than surfaced to a player.

## Observing generator internals

`GenerationContext::telemetry` collects typed generator/helper observations when explicitly
enabled; `GenerationService` returns them in `GenerationResult` on success and failure.
It defaults off so ordinary generation and validation probes avoid collection overhead.
This is separate from `StartQuality` and final-map analysis. See [TELEMETRY.md](telemetry.md)
for stable keys, bounded retention, failure reports and bulk analysis.

Related: [map generators](README.md).
