# Faulted City implementation evidence

See [design and results](../../../docs/map-generators/FAULTED_CITY.md). These are
2026-09-19 measurements, not universal balance claims. Map id 58, revision 1.

## Retained artifacts

- Four `SEED-WIDTHxHEIGHT-TEAMS.map.gz` maps and matching PNG previews use default
  controls, four workers and the parameters in their names. Decompress with
  `gzip -dk FILE.gz` to open them in the game. JSON requests for the parameter study
  are in `requests-and-results.jsonl.gz`; its 3,320 rows retain the exact settings,
  outcome, final fairness, resource totals, terrain counts and compact design telemetry.
- `parameter-summary.json` includes every slider value across seeds 301–308,
  randomized and shape coverage, retries and plaza variants. The complete Cartesian
  product was not enumerated. All requests passed, with a maximum of six layout attempts.
- `games.json`, `reference-games.json`, and `game-results.json.gz` preserve all
  16 city games and four Hedgerow Country reference rotations. The compressed file
  contains commands, raw result objects and last per-team `GLOB2_MEASURE` records.
  City seeds are 101, 202, 303, 404; game seed 19, 45,000 ticks. Teams 0–3 use
  Nicowar, Cortex, Cabino, Maxima. Physical start = `(team - rotation) mod 4`.
- `202-final.game.gz` and `202-after-45000-ticks.png` show the developed city from
  seed 202, rotation zero. Streets remain clear and renewable crops stay in plots.
  Mean peak population is not a claim of sustained health: Nicowar and Maxima suffer
  late food/logistics attrition, including zero-population outcomes with few combat kills.
- `performance-pairs.json` contains 32 alternating Linux runs (two repeats of four
  seeds at each of two sizes, before/after the Room.cpp optimization). Each pair's
  complete map bytes have the same SHA256. The measured command includes loading and
  saving, but excludes JSON analysis. `profile-baseline.sample.txt` is the CPU sample.
  The three `profile-largest-*` logs compare telemetry off/on and Hedgerow Country
  on macOS ARM64, seeds 101–106, 512², twelve teams, default controls. Shared-host
  timings are noisy; these are not fixed performance thresholds.
- `compatibility-initial.map.gz` is the exact earlier tuned city used for both
  platforms' 4,096-tick, four-Cabino, game-seed-19 replay runs. The macOS ARM64 and
  Linux x86_64 replay/checksum archives can be checked against `compatibility.json`.
  Sidecars match byte for byte. Replay headers differ; order streams match from the
  recorded offset. No simulation code changed between this terrain revision and the
  final one. Final generation fingerprints also match across these platforms.
- Contract, toolkit and translation logs record targeted checks. The shared room
  check has 200 differential fixtures against exhaustive flooding plus focused
  cardinal/diagonal/empty/blocked/duplicate-source cases.

The raw exploratory studies, prior tuning games, full logs and sampling runs remain
under ignored `artifacts/faulted-city/` locally and in the dedicated Linux checkout
`/home/bradley/glob2-faulted-city/`. This bundle is the portable review subset.

## Reproduce

Run from the repository root. The scripts need Python 3 and an optimized client.
Use fresh output directories for tournament runs; structured commands do not overwrite
completed results.

```sh
scons release=1 server=0 -j6 build/src/glob2 map-generator-defaults-test \
  map-generator-golden-test map-generator-profile-fixture
build/src/MapGeneratorDefaultsTest faulted-city-check --faulted-city-only
build/src/MapGeneratorDefaultsTest faulted-city-toolkit --toolkit-only
python3 data/check_translations.py --strict
python3 test/test_translations.py

python3 test/fixtures/faulted-city/study.py --binary build/src/glob2 \
  --out artifacts/faulted-city/reproduced-study --mode random --count 2000 --jobs 8
python3 test/fixtures/faulted-city/study.py --binary build/src/glob2 \
  --out artifacts/faulted-city/reproduced-study --mode controls --jobs 8
python3 test/fixtures/faulted-city/study.py --binary build/src/glob2 \
  --out artifacts/faulted-city/reproduced-study --mode shapes --jobs 8
python3 test/fixtures/faulted-city/tournament.py \
  --out artifacts/faulted-city/reproduced-games
python3 test/fixtures/faulted-city/tournament.py --reference \
  --out artifacts/faulted-city/reproduced-reference

build/src/MapGeneratorProfileFixture faulted-city-profile 101 6 faulted-city --largest
build/src/MapGeneratorProfileFixture faulted-city-profile 101 6 faulted-city --largest --telemetry
build/src/MapGeneratorProfileFixture faulted-city-profile 101 6 hedgerow-country --largest
python3 test/fixtures/faulted-city/verify_evidence.py
```

Review involved repeated independent map/visual feedback rounds and a separate agent
editing/reviewing translations. Human playtesting, native-speaker approval and Windows
simulation comparison were not performed.
