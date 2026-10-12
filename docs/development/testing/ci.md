# Ci verification

Focused regression scenarios and commands. Start with the [native test guide](../../../test/README.md) for building, isolation and runner selection.

## CI coverage ownership

Primary GCC 13 runs the complete applicable native suite. Secondary platforms
use `python3 test/run_tests.py --coverage-profile compatibility` with the reviewed
suite inventory in `test/ci-compatibility.json`; full nightly/release verification
uses `--coverage-profile full`. Map-generator contracts run in their dedicated lane.
Add new compatibility suites when introducing save, replay, scheduling, scripting
or platform boundaries. The selector fails closed for shared/unknown inputs.

`--write-inventory PATH` retains exact eligible and assigned cases, excluded cases,
platform, profile and shard ownership alongside JUnit evidence. CI audits shard
inventories for missing and duplicated cases. Empty compatibility selection fails
rather than reporting a successful empty suite. Portable primary regressions need
not be repeated on every compiler; platform, renderer and thread-count repeats
must have a named compatibility purpose.

The [development reference](../verification.md#tiered-pull-request-coverage-rollout)
records draft behavior, expansion labels, release-only checks and activation gates.

## Native coverage workflow

Build and measure the regular native tier with a matching Clang/LLVM toolchain:

```sh
python3 test/run_coverage.py --quick --timeout 900 -j4
python3 -m unittest discover -s test -p test_run_coverage.py -v
```

Use an optimized coverage build for the full tier, including expensive generator
registry contracts and custom-game previews:

```sh
python3 test/run_coverage.py --optimization 1 --timeout 1800 -j4
```

Native macOS binaries hold a scoped user-initiated activity while tests run,
preventing App Nap from throttling long background cases. The activity ends
when the test binary exits.

Each optimization level uses a separate default build directory. Keep reports
from different optimization levels separate. The manifest records the flags and
selection; the explicit timeout accommodates instrumented integration runs. Add `--fullscreen` only on a display that supports mode
switches. `--no-display` selects a headless subset and is recorded in the report.
Versioned Linux tools can be selected with `--cc clang-18 --cxx clang++-18
--llvm-profdata llvm-profdata-18 --llvm-cov llvm-cov-18`.

Each run gets a fresh directory under ignored `artifacts/native-coverage/`, with
build/test logs, JUnit, compiler/tool versions, source revision, selection,
profiles, full coverage JSON, weighted implementation summaries and HTML.
CI gives the instrumented suite a 90-minute job budget while retaining the
900-second per-case timeout. It passes `--stream-logs` so command progress and
diagnostics remain visible in the job log even when artifact upload cannot finish.
Local runs keep file-only output unless this option is requested.

CI passes `--discard-merged-profiles` to remove redundant raw profiles only after
the binary's tests, profile merge, JSON export and HTML generation succeed. The
merged profile and all reports and test evidence remain; failed runs retain raw
profiles for diagnosis. Local runs keep raw profiles by default. The manifest
records profile retention and the bytes removed for each completed report.
Engine and unit profiles are merged and exported separately: the engine report
is the implementation baseline, and the unit report supplements it. Never
average their percentages or merge independently linked copies of the same
source. Multiplayer (`src/net` and network-tagged cases), external
libraries and test implementations are excluded from implementation totals.
Unlinked/platform-specific sources are listed as unmeasured, rather than assigned
zero coverage. Header coverage remains in the file inventory, apart from the
implementation area totals. Coverage-tool diagnostics fail the run so a damaged
export cannot appear successful.

The added behavior coverage focuses on the following native boundaries:

| Cases | Behaviors protected |
| --- | --- |
| `AIDecisionCoverage`, `CastorContinuation` | Seeded AI orders, pause neutrality, saved continuation, simulation checksums and RNG state |
| `CortexNetCoverage`, `CortexPolicyCoverage`, `CortexActionCoverage` | Integer model arithmetic, malformed models, eligibility and thresholds, worker budgets, and orders applied by the engine |
| `LegacyScriptCoverage`, `USLCoverage` | Parsing failures, legacy and painted area waits, counts, flags and suspension, summons and alliances, recursion, thread yields, garbage collection and runtime errors |
| `GUIOrderCoverage`, `GUIInteractionCoverage` | Queued requests, clamps, deduplication, field reconciliation, replay input, desktop menu interactions and unit information |
| `EditorActionCoverage` | Action dispatch, unit/building editing, matching controls and save/load persistence |
| `BrushCatalog` | Editor brush catalogue contents, imported terrain order, experiment locks and map-header enabling, resource placement validity, catalogue action round trips, imported-name collisions and opaque map-matching swatches |
| `EditorDockLayout` | The editor dock at 1024x480, 1024x600, 1280x720 and 1920x1080: controls inside the dock without overlap, unique keys, the last card reachable by scrolling, clicks at the dock edge, live cards after definition imports, locked experiments enabled from the dock, palette navigation, search and the object inspector |
| `SurfaceCoverage` | Alpha grids, cropped/scaled blits, clip boundaries and progress-bar pixels |

Use uncovered functions and branch annotations to choose the next scenario by
consequence: saves and deterministic decisions first, then authoritative orders,
script execution and editable state, followed by rendering and diagnostics.
Line coverage alone does not establish save continuity, equivalent execution on
another platform, or playable game behavior. The Linux CI coverage artifact
uses the regular tier; slow integration and cross-platform checks remain separate.

The slow `[map-generators]` tier reports default repeatability, rectangular-map
and rejection checks separately for each registered generator. Registry stress,
landscape, framework and editor-default checks also have independent timeouts
and logs; a timeout must identify its case rather than hide the whole catalog.

Use `python3 test/test_cli_smoke.py --binary <client> --artifacts artifacts/cli --junit artifacts/cli.xml`
for real executable contracts: argument validation, map image/report workflows,
headless worker parity, experimental catalog generation, embedded-catalog reopening
without installed definitions, and saved continuation. `test/run_coverage.py --with-cli`
builds the instrumented client and exports these profiles separately under `client/`;
never merge its counts with independently linked engine or unit reports.

Native CLI platform evidence can be compared with
`python3 test/check_cli_evidence.py <artifact-root> --require-platform linux --require-platform windows`.
It compares all 64 complete tick records, including aggregate and entity checksums.
The browser saved-match smoke checks resize, menu cancellation and resumed ticks;
Android smoke also exercises Settings input and verifies application profile
files survive background/resume and a fresh-process relaunch.
