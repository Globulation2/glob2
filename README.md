# PR #1220 integration evidence

Tested PR revision: `db3ead501efa9f10d3aca7652ff3596c96a314a9`.
Base and clean local baseline: `71a3a23ae82ea1a97c3c63f54e1d486c35419542`.
Master was fetched again before final review and remained at that revision.
The PR integrates current cold initialization, adaptive fields, movement classes,
and save layouts while changing building admission to four jobs per team.

## Results and provenance

| Check | Result |
| --- | --- |
| Clean-head focused native tests | 77 cases passed; no failures |
| Turn/network suite | 20 cases passed; no failures |
| Browser serial/threaded correctness | 24 cases passed in Chromium, Firefox and WebKit |
| Cross-platform simulation trace | All 18 browser traces equal the native 1,500-tick binary trace byte-for-byte |
| Golden match verification | All six browser traces equal native and the committed 843-row trace |
| Four-team whole game | All 4,096 world/entity trace ticks equal with one/four compute participants |
| Eleven-team whole game | All 2,048 world/entity trace ticks equal with one/four compute participants |
| CLI save/resume | All raw world hashes match, saving at 2,048 and resuming through 4,096 with four participants |
| Simulation version contract | Passed against origin/master; format/replay floor 186, sim revision 96, save floor 58 |

`final-head-build-provenance.json` records the clean tested commit. The final
focused suite covers quota isolation, no borrowing, team FIFO, mixed cold/warm
deadlines, all 512 pending jobs, 288-job binary/text saves, historical byte
counts (167 and 185), old saves, worker deadlines, replay gates and the golden match.
The full exploratory core/save/network runs preceded the clean-provenance rebuild;
the production source is the same. Their original producer identities and reports
are retained, rather than relabelled. `environment.json` identifies the executables,
dependency inputs and flags. `dependency-linkage.txt` retains native runtime linkage.

## Existing master failures

The exploratory core run reported six failures across 219 individual cases; the
save-format run reported three across 98 cases. A clean GCC 15 build of current
master reproduces all nine, recorded in `baseline-junit.xml` (seven cases) and
`baseline-extra-junit.xml` (two). `baseline-comparison.json` lists the cases.
Eight also failed in [master hosted run 39320985085](https://github.com/Globulation2/glob2/actions/runs/39320985085),
whose revision 495e39db8 has identical production sources to the current base.
The four GCC 13 shard logs are retained with timestamps.

The format-150 synthetic text fixture omits the required historical `updated`
routing flag (and other later layout differences). A debugger identifies the
missing `Game.mapRuntime.teams.0.swimClasses.0.resources.0.updated` read before
`Invalid saved routing flag`. It fails on clean master GCC 15, and also passed a
later PR rerun without code changes (`final-legacy-junit.xml`), demonstrating
fixture instability; this is not claimed as repaired. Keep master repairs separate.

## CLI continuation diagnosis

The former raw-hash mismatch was a format-header comparison error. The shipped
Holiday Island map is format 70; a saved/reloaded copy has header format 186.
`MapHeader::checkSum()` includes that format. Rotating `70 XOR 186` through the
15 header rotations in the four-team/four-player deep checksum yields 33030144,
exactly the constant XOR delta on every one of the 2,048 resumed deep hashes.
All team/unit/building sidecar records already match at every resumed tick, and
final routine checksums match. `header-checksum-diagnosis.json` retains this result.

`run_canonical_checks.py` first writes and loads a current-format initial save,
then runs uninterrupted, prefix and resumed games. Their raw hashes match with
no hash adjustment. `game-summary.json`, commands, saves and complete traces are
retained in `games.tar.gz`. The original failing script/output is retained too.

## Commands

Run from the repository root unless a recorded command specifies its cwd:

```sh
GLOB2_SDL3_PREFIX=/home/bradley/glob2-claude/build/sdl3/prefix python3 tools/dev_build.py -j24 engine-tests unit-tests build/linux/client/debug/dev-dev_fast-true-linker-auto/src/glob2
GLOB2_SDL3_PREFIX=/home/bradley/glob2-claude/build/sdl3/prefix python3 tools/dev_build.py -j4 engine-tests unit-tests build/linux/client/debug/dev-dev_fast-true-linker-auto/src/glob2
python3 test/run_tests.py --build-dir build/linux/client/debug/dev-dev_fast-true-linker-auto --update-fixtures --filter 'TurnEngineHarness/the committed*' --junit artifacts/pr1220/golden-junit.xml --artifacts artifacts/pr1220/golden
python3 test/run_tests.py --build-dir build/linux/client/debug/dev-dev_fast-true-linker-auto --filter 'GradientPipeline/*' --filter 'BuildingGradientInvalidation/*' --filter 'MapGradientInvalidation/*' --filter 'GradientPreparation/*' --filter 'ReplayStepCounter/*' --filter 'UnitCustomization/*' --filter 'JavaScriptCompatibility/*' -j6 --junit artifacts/pr1220/core-junit.xml --artifacts artifacts/pr1220/core
python3 test/run_tests.py --build-dir build/linux/client/debug/dev-dev_fast-true-linker-auto --tag save-format -j4 --timeout 300 --junit artifacts/pr1220/save-junit.xml --artifacts artifacts/pr1220/saves
python3 test/run_tests.py --build-dir build/linux/client/debug/dev-dev_fast-true-linker-auto --filter 'TurnEngineHarness/*' -j3 --timeout 300 --junit artifacts/pr1220/network-junit.xml --artifacts artifacts/pr1220/network
python3 test/run_tests.py --build-dir build/linux/client/debug/dev-dev_fast-true-linker-auto --filter 'GradientPipeline/*' --filter 'BuildingGradientInvalidation/*' --filter 'ReplayStepCounter/*' --filter 'JavaScriptCompatibility/Current simulation*' --filter 'TurnEngineHarness/the committed*' -j4 --timeout 300 --junit artifacts/pr1220/final-head-junit.xml --artifacts artifacts/pr1220/final-head
python3 test/run_tests.py --build-dir build/linux/client/debug/dev-dev_fast-true-linker-auto --binary engine --filter 'EntityRandomLifecycle/format 150 text area saves*' -j1 --junit artifacts/pr1220/final-legacy-junit.xml --artifacts artifacts/pr1220/final-legacy
python3 test/check_sim_revision.py --base origin/master
python3 artifacts/pr1220/run_game_checks.py
python3 artifacts/pr1220/run_canonical_checks.py
python3 test/run-browser-determinism.py build/linux/client/debug/dev-dev_fast-true-linker-auto/src/glob2 artifacts/pr1220/native-determinism
CCACHE=1 scons target=web release=1 -j20 web-tests build/emscripten/client/release/index.html
npm ci --prefix browser
cd browser
npx playwright test tests/determinism.spec.js --grep 'produces a complete per-tick simulation trace|verifies the committed match record' --workers=3 --reporter=list --output=../artifacts/pr1220/browser-results
```

Baseline commands/cwd are recorded in `baseline-command.json`; the second
explicit run selected `EntityRandomLifecycle/format 150 text area saves*` and
`SavegameSafety/autosave defaults*`, with -j2 and a 300-second timeout. It uses
the same prefix, compiler and fast flags. Initial setup failures and interrupted
builds are retained: native SDL discovery required selecting the existing pinned
prefix, a development wrapper rejected release/web_variant defaults, and builds
were restarted at higher parallelism. These are not passing validation claims.

## Coverage limits and acceptance

Linux x86_64 GCC 15 native and wasm32 in all three browser engines were executed.
These results do not establish current native macOS/Windows or Android/iOS
execution, sanitizer qualification, release packaging, weaker-device/rendered
frame timing, or final native release performance. Hosted PR checks at
[run 39336564530](https://github.com/Globulation2/glob2/actions/runs/39336564530)
were still running when local qualification finished; they are separate evidence.
The maintainer explicitly confirmed prior playtesting and comfort with the
pacing change. Earlier optimized-prototype measurements remain historical
[evidence](https://github.com/Globulation2/glob2/tree/9d037bf), not current
production performance measurements. No runtime dependencies were changed.

Archives retain saves, replays, checksum traces and test artifacts. Logs are
gzip-compressed. `sha256-manifest.json` covers all other published files.
