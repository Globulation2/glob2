# Savannah implementation evidence

Savannah is generator ID 33, revision 1. It uses existing artwork and adds reusable
bounded site jitter and contained-plot stamping, planting and validation operations.
See SAVANNAH.md for the design, numerical budgets, heuristics and failure policy.
No simulation, save-format, replay or network version changes were made.

The **current source includes a light playtest tune** after the initial implementation
matrix. See `playtest-round1/SUMMARY.md` for matched games, the rejected food-inn
regression, final crop geometry, Linux evidence and focused post-tune checks. The
`final-*` directories below are the reproducible **pre-tune baseline**, not a claim
that all 401 cases were rerun after the later crop adjustment.

A subsequent bulk audit generated **872/872 valid requests** on the tuned source:
130 pilot and 742 fresh-seed full cases, including every discrete control value,
the smallest supported square and both rectangle orientations. See
`bulk/SUMMARY.md` for the failure denominator, 55 optional pond-omission maps,
high-abundance saturation, static access limits and eight saved sample maps.

## Initial implementation verification

- Optimized macOS arm64 build passed. Linux was added in the later playtest round.
- 401/401 baseline map cases passed: 128 training, 128 held-out, 145 parameter cases.
- Training seeds 1–32 and fresh held-out seeds 3001–3032 each used 128x128/4 teams,
  256x256/4, 256x128/5 and 128x256/3. Parameter manifests record every request.
- Separate Savannah sweep: 46/46 generations, zero failing combinations.
- Defaults harness passed, including final validation after 4096 unattended resource
  growth calls, extreme resources, rectangular maps, crowding rejection and shared helpers.
- CustomGameSetupHarness passed, including option preference round trips.
- Golden update added eight macOS Savannah rows; every existing generator hash stayed unchanged.
- All-generator telemetry checks: 96 cases, 96 generated, zero semantic failures.
  Production maps with matching names were also byte-identical with telemetry off/on.
- Production save/reload retained terrain, resources, fertility, space, movement,
  quality metrics and preview pixels (final-reload-check.json).

### Held-out access and omissions

Distances are walking gather steps; room is the weakest home's reachable 4x4 anchor
count from report telemetry. The generator separately requires usable 8x8 footprints.

| Size / colonies | Worst wheat | Worst wood | Minimum room | Maps omitting a pond / 32 |
| --- | ---: | ---: | ---: | ---: |
| 128x128 / 4 | 10 | 11 | 1304 | 3 |
| 256x256 / 4 | 9 | 11 | 1402 | 0 |
| 256x128 / 5 | 9 | 11 | 1243 | 0 |
| 128x256 / 3 | 9 | 11 | 1357 | 0 |

The three small-map omissions retained one neutral pond instead of the target two.
Optional clumps and high-abundance deposits may saturate; reports retain requested,
placed and omitted counts. There are no unrestricted resource or route repairs.
Process times averaged roughly 0.55/2.32/1.19/1.01 seconds for the settings above,
including report work under concurrent load; these are not controlled benchmarks.

## Initial populated games and remaining limits

Six completed mirror games used the same 128x128 two-colony seed-29 map, game seed19,
two cyclic seat rotations, and a 60000-tick cap (or engine termination).

| AI | Births | Starvation deaths | Combat damage | Combat deaths |
| --- | ---: | ---: | ---: | ---: |
| Nicowar | 119 | 71 | 38227 | 5 |
| Numbi | 100 | 0 | 13722 | 9 |
| Maxima | 204 | 96 | 21737 | 18 |

Counts aggregate two games. Durations differ: Nicowar ended at35938/60000 ticks;
Numbi at60000/60000; Maxima at37762/33538. All expanded and engaged in combat.
Starvation remains a balance concern for Nicowar and Maxima, including during siege.
An earlier smaller wheat plot calibration motivated the documented food enlargement;
raw cumulative deaths do not establish improvement when game duration/combat differ.
Human play has not been performed. Pond attractiveness, useful flanking and translation
quality need human review. Linux/Windows checksum equivalence was not tested.
An optional additional mixed-AI game was interrupted during wrap-up and is excluded
from completed evidence. Final games retain saves and replays, but not per-tick checksum
traces. No engine behavior changed for existing generators.

## Reproduction

Run from the repository root after applying source.patch to the recorded base commit.
The matrix runner expects a new output directory and tools/map_telemetry.py from the repo.

```sh
scons release=1 server=0 -j2 build/src/glob2 map-generator-defaults-test map-generator-golden-test map-generator-study
build/src/MapGeneratorDefaultsTest glob2-savannah-final
build/src/MapGeneratorGoldenTest glob2-savannah-golden --require-rows
build/src/MapGeneratorGoldenTest glob2-savannah-sweep --sweep 21/31
build/src/MapGeneratorGoldenTest glob2-savannah-telemetry --telemetry
python3 artifacts/savannah/run_matrix.py artifacts/savannah/recheck-training training
python3 artifacts/savannah/run_matrix.py artifacts/savannah/recheck-heldout heldout
python3 artifacts/savannah/run_matrix.py artifacts/savannah/recheck-parameters parameters
```

Exact CLI commands for every map and game are retained in manifests. Preview and game
scripts are included. `playtest-round1/` contains the later build/check logs and
game commands. Existing golden rows were checked by reviewing the post-tune fixture
diff; the command above verifies the resulting fixture.

## Package contents and provenance

The baseline generation reports are final-training/, final-heldout/ and
final-parameters/. Other earlier local directories without final- are calibration runs.
The current focused evidence is `playtest-round1/` and `bulk/`. The archive includes
the baseline reports, previews, maps, six baseline games' saves/replays and logs,
the light playtest's matched games and Linux save/replay, the pilot and full bulk
archives with eight regenerated sample maps, analysis scripts/results, the design document,
source.patch and provenance.json. Baseline release-game logs/results are included for
tuning context; large preliminary checksum traces remain in the local artifacts tree.
Profile directories and the interrupted mixed game are excluded. manifest.json contains
SHA-256 hashes for each packaged file (excluding itself). Unpack at repository root to
restore artifacts/savannah paths. The source patch includes untracked new source files.
