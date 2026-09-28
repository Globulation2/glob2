# Lazy building gradient validation evidence

**Single-pass field initialization:** [incremental optimization evidence](field-initialization/README.md) includes AC timings, initialization/fill profiles, and cell-by-cell comparison against the previous initializer.

**API cleanup and remaining costs:** [the follow-up report](api-profile/README.md) covers implementation 7d2dcaf1c, public query boundaries, renewed checksum/save validation, 52 measurement runs, and phase/allocation profiling. The earlier controlled timing study remains the baseline performance evidence.

**Updated timing evidence:** the [162-run controlled study](timing-study/README.md) supersedes the small-sample CPU estimates and unmeasured-save-latency status below. It finds 2–8% net CPU savings across seven workloads and measures the synchronous-save tradeoff. The earlier raw measurements remain intact for comparison.

Implementation: [d50968d06](https://github.com/Globulation2/glob2/commit/d50968d06), based on [1c49596f5](https://github.com/Globulation2/glob2/commit/1c49596f5195188e7f078768f610454f7068d7c6). This branch contains review evidence only and is not intended to merge into master.

The opt-in implementation shares one bucket-expansion kernel between eager and resumable searches. Building initialization and seed scanning remain eager. Round-trip construction, forbidden-area escape, debug display and serialization finish cached fields. Frozen obstacles/water costs and existing cache refresh/eviction timing preserve routing semantics; the save format remains unchanged.

## Correctness

- Native macOS arm64 release client/server builds pass. PathGradientHarness passes its independent heap oracle: the existing 1,526 eager cases / 2,032,787 exact cell comparisons retain digest 3281260807785418242, and new lazy cases cover all seven swim classes, paused terrain, reordered queries, equal-cost movement, disconnected/capped paths, independent queues and rebuilds.
- Building invalidation, immobile-unit and save-safety harnesses pass in both modes. The save suite includes four binary/text and human/AI continuations checking RNG and 700 simulation steps, plus corruption and atomic-save cases.
- Four full games compare to the original baseline: **65,442 ticks in each mode**, with byte-identical per-tick checksum sidecars and initial/final saves in all eight runs. See [validation.json](validation.json).
- An additional 1,024-tick continuation from an original save matches simulation checksums in every run. **There is one existing telemetry caveat below**, so this report does not claim every continuation save is byte-identical. See [continuation.json](continuation.json).
- CI is configured to run the oracle on Linux/Windows, lazy invalidation and immobile-unit tests on Linux, and lazy save continuation on both. This local evidence does not establish cross-platform full-game checksum equivalence or interactive play review.

### Existing telemetry caveat

One lazy continuation's final save differed only in a historical Maxima diagnostic value (`state.policy_bids_6.desired_warriors`, sample tick 16384) and the save SHA1. The same executable matched all save bytes on repetition, and simulation checksums matched on every run. The original code's [telemetry adapter](https://github.com/Globulation2/glob2/blob/1c49596f5195188e7f078768f610454f7068d7c6/src/ai/AITelemetryAdapters.cpp#L930) reads `policy_bids[6]`, although [PolicyCount is six](https://github.com/Globulation2/glob2/blob/1c49596f5195188e7f078768f610454f7068d7c6/src/ai/maxima/AIMaxima.h#L323). This out-of-bounds read exists on the baseline and is outside the gradient change. The differing save and baseline are retained, and the repeat matches the baseline hash; no normalization masks the difference. Fixing this diagnostic bug is separate follow-up work.

## CPU measurements on the cleaned implementation

| Case | Eager CPU seconds | Lazy CPU seconds | CPU reduction | Extra peak RSS MiB |
| --- | ---: | ---: | ---: | ---: |
| arena128-2 | 3.198 | 3.118 | +2.5% | 0.47 |
| arena256-2 | 9.858 | 10.720 | -8.7% | 1.17 |
| arena512-4 | 53.261 | 46.675 | +12.4% | 6.83 |
| lakes256-4 | 17.663 | 15.121 | +14.4% | 3.39 |

Positive percentages mean less CPU time; the negative arena256-2 result is a regression in this sample. Do not replace it with the earlier prototype's more favorable result. Each row averages two runs per mode in eager/lazy/lazy/eager order using the same binary. CPU is child-process user + system time from `wait4`; RSS is the difference of mean per-process peak RSS. Renderer/autosaves and explicit save/checksum telemetry are off; team-timeline telemetry and normal engine performance scopes are on in both modes. Results/tick counts matched across modes. The raw commands, binary hash, per-scope timings, process metrics and game results are in [measurements.json](measurements.json).

Cases use game seed 19 and target 16,384 ticks (arena128-2 ends at 16,290). Arenas use generator 15/map seed 42; lakes uses generator 4/map seed 97. Two-player cases use Nicowar/Warrush; four-player cases add Cortex/Maxima. See [cases.json](cases.json) and the archived generated maps.

### Direct prototype comparison on arena256-2

After the first fresh sweep showed a regression, the original immutable prototype and cleaned binary were interleaved on the same case. These are all runs, in execution order:

| Binary | Mode | CPU seconds |
| --- | --- | ---: |
| prototype | eager | 10.707 |
| prototype | lazy | 11.022 |
| clean | lazy | 12.918 |
| clean | eager | 10.991 |
| clean | eager | 9.669 |
| clean | lazy | 10.016 |
| prototype | lazy | 9.133 |
| prototype | eager | 9.269 |

These measurements also vary substantially, including between repeated runs of the same binary/mode. They do not support a confident attribution of the slowdown to cleanup or a universal speedup claim. Larger samples under controlled conditions are needed before enabling this globally. The flag remains off by default. Raw commands/results: [prototype-comparison.json](prototype-comparison.json). The earlier uncommitted prototype is preserved in [prototype-source.tar.gz](prototype-source.tar.gz), with its source and executable hashes.

Save-time latency is not benchmarked: serialization completes pending fields, which can concentrate work at autosave. Retained queues and water snapshots add memory. Timing results are CPU measurements, not FPS measurements.

## Reproduction

Build the implementation commit from the source repository:

```sh
CCACHE=1 scons -j6 release=1 server=0 path-gradient-test building-gradient-invalidation-test immobile-unit-gradient-test savegame-safety-test build/darwin/client/release/src/glob2
CCACHE=1 scons -j6 release=1 server=1
```

Adjust the native toolchain directory for other platforms. [tests.json](tests.json) lists exact local test commands; mode selects `GLOB2_LAZY_BUILDING_GRADIENTS=0/1`. The save safety runner must use its own disposable profile (do not override its profile directory with a shared `GLOB2_USER_DIR`). [logs.tar.gz](logs.tar.gz) retains build, test and run logs. Source/compiler hashes are in [source-metadata.json](source-metadata.json); [manifest.json](manifest.json) identifies the implementation, executable and fixture archives.

Download/extract the four case archives to a fixtures directory. Each contains the generated map, generation command, case settings, checksum sidecar, initial save and final save. The outputs are a single canonical copy whose hashes matched the original baseline and both modes; comparison records retain every run's hashes. From the source checkout:

```sh
python3 /path/to/evidence/reproduce.py --binary build/darwin/client/release/src/glob2 --fixtures /path/to/fixtures --case arena128-2 --mode lazy --output artifacts/lazy-reproduction
python3 /path/to/evidence/benchmark.py --binary build/darwin/client/release/src/glob2 --fixtures /path/to/fixtures --output artifacts/lazy-benchmark
```

Use a fresh output directory per run. Repeat the first command for each case and mode. The benchmark script runs all four cases in the order described above. Checksums and saves require identical telemetry flags; team-timeline collection updates saved diagnostic samples.

For the old-save continuation, load `arena512-4/final.game`, run to tick 17408 with `--telemetry checksums --save final`, and compare against `continuation.tar.gz`; see the caveat above. This archive includes the nonmatching diagnostic run and baseline; the repeat matches the baseline files exactly.
