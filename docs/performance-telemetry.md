# Engine performance telemetry

Performance collection is automatic. `GLOB2_TEAM_TIMELINE=1` exports it alongside gameplay
and AI statistics. It is local to an execution session: loading starts fresh timing coverage,
and no performance data enters saves, orders, RNG, simulation checksums, or AI decisions.
No HUD values or pacing rules change. AI samples are written in fixed-size binary batches
with byte-for-byte equivalence to the previous scalar writes; the save format is unchanged.

## Meaning of the measurements

All durations are monotonic elapsed nanoseconds, not process CPU time. Descheduling and
blocking inside an instrumented operation remain in its duration. Rendering measures CPU-side
submission and the presentation call; it does not measure GPU execution or physical display
scanout. No GPU queries, synchronization, or additional world scans are introduced.

- `loop` measures complete main-loop iterations, including pacing.
- `loop.work` excludes intentional sleep, network sleep, and the presentation call.
- `simulation.tick` measures an executed simulation step; rendered frames and ticks need not
  have the same cadence.
- `pacing.presentation_interval` measures time between presentation returns. Its population
  standard deviation is frame jitter. `pacing.interval_change` measures absolute differences
  between consecutive intervals; its mean is mean absolute interval change.
- `budgets` records observations, overruns, accumulated/worst excess, and longest overrun
  streak. Work uses the current step budget; presentation uses the step budget multiplied by
  render cadence. Normal-speed steps have a 40 ms budget. Headless/uncapped runs have no
  budget classification. Mode/speed changes break interval chains and overrun streaks.
- Save queue, hash, and write measurements describe background work (or the existing
  synchronous fallback if thread creation fails). They must not be added to main-thread work.
  They are published under the writer's existing lock on the next submission or final wait.
  `saved_total`, `failed_total`, and `superseded_total` are session counters.

Scopes cover units/buildings/tasks, map updates, fog, scripts, construction projects,
statistics, orders/replay checksums, AI totals and observation/planning/gradient phases,
resource/building/round-trip/area gradients and propagation, pathfinding, rendering passes,
loading/generation/site assignment/site relaxation/validation, and save/output work.
Per-player AI totals identify player, team, implementation, and controller generation.
Replay timings describe replay execution, not reconstructed original AI decisions.
AI subphase scopes are aggregate timings across players; the complete per-player AI cost is
reported by `ai.player`. Audio callback and GPU execution times are not instrumented.
Startup timings include generation attempts performed while preparing the session.

## Cost and sampling

`PerformanceScopes.inc` defines stable scope names and sampling strides. Major phases and
expensive rebuilds are timed on every execution. Hot path-query/direction scopes count every
call and time one in 64 calls. The selected residue rotates between capture windows without
using gameplay RNG. This spreads sampling across periodic callers, but does not make the
estimate statistically unbiased for every workload.

Each scope retains fixed-size online moments: count, integer total/maximum, mean, and M2.
There is no event log, per-event allocation, string lookup, or sorting. Nesting is bounded at
64 timed scopes; player attribution retains at most 128 controller generations per session.
Overflow reports dropped records/samples and continues the game normally.

Inclusive durations overlap and must not be summed into a supposed engine total. `self_ns`
is available only when all instrumented child timing is exact. Uninstrumented work remains
in the parent's self time. Encountering a sampled descendant makes self time unavailable,
rather than subtracting an estimate from an exact duration.

`GLOB2_PERF_DISABLE=1` disables collection for benchmark comparisons. It is a diagnostic
control, not a gameplay option. `GLOB2_PERF_BUILD_LABEL` optionally supplies a build identifier
in exports; the benchmark artifacts should also retain the executable hash and build command.

## Export contract

Existing records remain unchanged. New records use key/value fields and nanosecond units:

- `GLOB2_PERF_SESSION`: process-local session identity, clock, save-format version, platform/compiler,
  pointer width, renderer, dimensions, player/team counts, optional build label.
- `GLOB2_PERF_SCHEMA`: stable scope name, sampling stride, population-variance convention.
- `GLOB2_PERF_SAMPLE`: interval tick and elapsed-time ranges, execution mode and separate work/presentation budgets, scope,
  thread attribution, call/sample counts, mean, standard deviation, maximum and totals.
- `GLOB2_PERF_FINAL`: session aggregates, following the final partial sample and the existing
  autosave drain. Cumulative mode is `all`; compare mode-specific sample records when speed
  changes matter.

A sample closes at the end of the iteration crossing a 512-tick boundary, or after five
seconds without a tick-based capture. Mode changes close the previous interval. Windows
continue during pause/stalls. No unbounded performance history is retained; explicit output
streams interval records, while collection without output retains only aggregates.

Exact scopes export `total_ns`. Sampled scopes export `observed_total_ns` and
`estimated_total_ns = sample_mean * call_count`. Their maxima and standard deviations describe
observed samples only. A scope with calls but no timed sample exports `na`; an unexecuted
scope has no sample record and remains identifiable through the schema. Absence is not zero.

Performance formatting runs only when output is enabled. Interval-output cost is charged to
the following interval; final output includes the final interval-formatting cost but cannot
include the time spent printing its own final records. Gameplay/AI export work is separately
timed. Parsing quoted metadata requires quote-aware key/value parsing (for example `shlex`).

## Extending and verifying

Add a descriptor and place `PERF_SCOPE_TIME(Name)` around an existing operation or batch.
Use a named `PerformanceTelemetry::Scope` and `stop()` where only part of a function belongs
to the scope. Keep timers outside entity/cell inner loops. Background threads must publish
explicit aggregates through existing synchronization; the implicit collector is thread-local.

Run `scons -j8 release=1 server=0 performance-telemetry-test team-stats-save-test savegame-safety-test`
and `build/libgag/src/PerformanceTelemetryHarness`. Run the save/statistics harnesses through
`test/run-savegame-safety-tests.py` as documented in the test README. The dedicated collector
harness uses an injected clock; the save harness checks background completion/failure counts.

Validate performance against the preceding AI-only executable using identical fixtures and
orders, separately for output off/on. Retain per-tick checksum sidecars, compiler/build
commands, executable hashes, and raw timing results. Cross-platform execution comparisons
remain required before claiming cross-platform verification.

Local verification results and limits are recorded in [the validation report](performance-telemetry-validation.md).
