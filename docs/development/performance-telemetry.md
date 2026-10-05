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
- `pacing.sleep` and `pacing.network_sleep` time the host's wait between iterations.
  A wait counts as network sleep only while `Engine::waitingOnNetwork()`: in turn games
  while the relay's authorized horizon is used up, in legacy games while a peer's
  orders are missing. Jitter-buffer pacing in a turn game is ordinary `pacing.sleep`.
  Both the native loop and the in-game screen host time these waits; menus do not.
  Turn games also export [network telemetry](network-telemetry.md) (`GLOB2_NET_*`
  records and a per-match `ClientNetworkSummary`).
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

When the simulation runs on its own thread (`src/engine/sim/SimulationRunner`), that thread records
into a collector its runner owns (`PerformanceTelemetry::bindCollector`). Each client frame,
with the simulation parked, the main thread absorbs that window into the session collector
(`Collector::absorb`) and configures and captures the session there, as `advanceSession` does
in serial play; it absorbs once more after the thread stops. Records name the thread a scope
ran on: `thread=main`, `thread=simulation` or `thread=main+simulation`. Threaded sessions
budget presentation against 60 Hz instead of the serial render interval.

Run `scons -j8 release=1 server=0 tests`, then
`python3 test/run_tests.py --binary unit --filter 'PerformanceTelemetry/*'` and
`python3 test/run_tests.py --filter 'TeamStatsSave/*' --filter 'SavegameSafety/*'`. The dedicated collector
harness uses an injected clock; the save harness checks background completion/failure counts.

Validate performance against the preceding AI-only executable using identical fixtures and
orders, separately for output off/on. Retain per-tick checksum sidecars, compiler/build
commands, executable hashes, and raw timing results. Cross-platform execution comparisons
remain required before claiming cross-platform verification.

Structured tournament runs include requested final-save serialization before the
performance final export. All records pass through verified worker log artifacts
and the [offline tournament readers](../tools/tournaments.md#gameplay-ai-and-performance-telemetry).

## Lazy building gradients

Building fields propagate lazily in all game entry points. There is no runtime
mode switch; point queries extend the retained search only as far as needed.

Initialization and seed scanning remain eager. Ordinary buildings initialize each
cell directly in one pass; flags first paint their goal region and then apply their
special obstacle rules. Each building/swim-class field retains its bucket queue
and, for weighted swimming, a snapshot of water costs. Obstacles remain frozen in
the initialized field. Queries finish a whole cost layer, including equal-cost
neighbors needed for movement sidesteps. An unknown
cell requires exhausting the search before it can be reported as unreachable.
The public `buildingGradient` API always completes the field before returning an
array. Point-distance and movement queries resolve internally; callers do not
receive partial arrays or need a separate resolve call. Preparing a field applies
the existing refresh/use policy, while reading a prepared field only extends it.
The queues share the eager solver's expansion kernel and are freed with their
field, including the existing idle-field eviction; no separate cache is added.

Round-trip construction, forbidden-area escape and gradient debug rendering finish
the relevant cached fields. Reading an already-cached round-trip field does not
force completion. Existing refresh deadlines and use timestamps are preserved.

Saving finishes pending fields from their original snapshots without refreshing
them, then writes the existing complete-field representation. Loaded fields start
complete; no format or version change is needed. This trades some play-time work
for save-time work: include autosaves when evaluating latency. Retained queues and
water snapshots also add memory.

`gradient.building` measures initialization and search setup.
`gradient.building_resume` measures actual lazy extensions and completion, including
those nested in round-trip construction or saving. Sum these two scopes to compare
building-field construction, but do not then add inclusive round-trip/save timings
to that total. Benchmark evidence belongs under `artifacts/`, not in this guide.

## Parallel AI polling and experimental map computation

Structured `--run-game` accepts `--compute-threads N` (1–64 execution threads,
including the submitting thread) and `--compute-experiments MODE`. Modes are
`none`, `areas`, `initialize`, `hiring`, `ai`, and `all`. AI polling is the
default mode. The default thread count is the smaller of four, the available
hardware threads, and the number of AI controllers, with a minimum of one.
Other compute modes remain experimental opt-ins. Specify one thread for a
serial AI reference.

Area jobs rebuild allocated forbidden/guard/clear fields at existing structural
refresh boundaries. Initialization jobs use 4096-cell chunks on maps of at least
16384 cells. Goal painting and forbidden-border detection retain their serial
ordering. Hiring jobs advance existing frozen building searches in separate swim
classes before candidate evaluation; they neither refresh caches nor change use
timestamps. This can perform unnecessary work and must be measured separately.
Periodic one-field-per-tick refresh remains unchanged. The `areas`,
`initialize`, and `hiring` modes leave AI polling unchanged.

The `ai` experiment polls eligible AI controllers in a blocking batch after the
GUI sync step. It binds their telemetry on the main thread, waits for every
controller to return one order, then submits those orders in player order before
the network update and simulation step. No AI work continues past the barrier.
Paused games keep the serial order path. `all` also includes AI polling.
Worker-local implicit performance scopes are not merged
into the main-thread collector; use batch metrics and process-level timing for
threaded comparisons. Short or uneven AI workloads may cost more in dispatch
and barrier overhead than they save in parallel work.

Ordinary GUI games and legacy `--nox` runs use parallel AI polling by default
when multiple controllers and CPUs are available. `--ai-threads N` overrides
the thread count (1–64, including the main thread). Structured `--run-game`
uses `--compute-threads N`; `--compute-experiments none` disables AI batching.

A game-owned executor uses persistent workers, main-thread participation and a
barrier before simulation resumes. Nested jobs run inline. Eager propagation
scratch is owned by executor slot; lazy searches retain their own queues. Thread
creation failure and the serial browser fallback use serial execution; threaded
browser builds use the same executor as native builds. Thread count and
performance counters are not saved or included in simulation checksums.

`result.json` reports actual `compute_threads`, selected `compute_experiments`,
`compute_batches`, `compute_jobs`, `compute_parallel_batches`, `compute_batch_ns`,
`compute_wait_ns`, and `compute_active_elapsed_ns`. The last value sums active
elapsed time across executor slots; it is **not CPU time**. `hiring_prepasses`
counts candidate-scan hooks and `hiring_popped_entries` counts queue entries
advanced there (including stale entries). `setup_ns` ends immediately before
`Engine::run`; `run_ns` measures that call, including its normal finalization.
Worker-local implicit scope timings are not merged into main-thread scope totals;
use the explicit batch metrics and process CPU measurements for comparisons.

On macOS/Linux, prepare retained fixtures and run paired measurements:

```sh
python3 test/prepare_parallel_compute.py BASELINE artifacts/compute-corpus --quick
python3 test/benchmark_parallel_compute.py BASELINE CANDIDATE \
  artifacts/compute-corpus/windows.json --output artifacts/compute-timing
python3 test/benchmark_parallel_compute.py BASELINE CANDIDATE \
  artifacts/compute-corpus/windows.json --output artifacts/compute-verification --verify
```

Omit `--quick` for the full two-seed land/water, 2/4-team corpus. Preparation keeps
early/middle/late checkpoints; `completion.json` instead measures games from their
initial state to an end condition or 90000-tick cap. Do not count a capped game as
a completed game, or changed termination ticks as computation speedup. A full
campaign can take considerable time and disk space. Timed runs omit optional
exports and saves; built-in scope collection remains enabled in both binaries.

The runner retains commands, executable/input hashes, logs, results, per-process
peak resident memory, wall time, and user-plus-system CPU time from `wait4`.
It runs one warm-up and five measured repetitions with rotated/reversed ordering.
Compare each experiment's serial execution, its threaded execution, and the
unchanged baseline. Aggregate ratios do not replace per-scenario CPU and small-map
regression checks. Timing thresholds are deliberately not CI assertions.


### Delayed periodic gradients

All games use two background workers and an eight-tick publication delay by
default. Structured headless runs accept `--gradient-workers N --gradient-delay D`.
`N` counts **background workers** (0–16); the simulation thread is additional.
`D` is the fixed publication delay (1–16 ticks, default 8). Zero workers computes
synchronously but retains exactly the same publication schedule, providing the
determinism and timing control for each delay. Different delays may produce
different games. A loaded game's delay cannot change while jobs are pending.

The pipeline seeds one allocated resource, guard or clear field at the original
end-of-tick round-robin boundary. It snapshots weighted terrain inputs, propagates
in private storage, and publishes before the teams step at the fixed deadline.
A synchronous refresh supersedes older pending results for that field. Increasing
worker count does not increase the number of scheduled fields. Buffers are bounded
by the delay; workers block on condition variables when idle.

Saving waits for private computation without publishing early, then stores each
pending field, destination, supersession flag and remaining deadline. Loading
restores that queue; worker count is local execution configuration and is not
saved. Format 120 adds this state while keeping the save compatibility floor at
58. Older saves start with an empty queue. The current scoped-invalidation policy raises the replay floor to 123 and
network protocol to 46; version-120 saves remain supported. Platforms without worker
threads use the same delayed serial schedule. Routing decisions may use periodic
fields eight ticks older than before; synchronous refreshes still take effect
immediately.

`result.json` includes actual worker count, delay, jobs, published/discarded jobs,
maximum pending buffers, deadline wait nanoseconds, and summed propagation elapsed
nanoseconds. The latter is **not CPU time**. Whole-process user+system CPU must be
measured externally. Timed runs drain outstanding work before stopping the timer;
finishing work does not publish it early.

`test/benchmark_gradient_pipeline.py` accepts the same scenario manifests as
`test/benchmark_parallel_compute.py`. For example:

```sh
python3 test/benchmark_gradient_pipeline.py /path/to/glob2 scenarios.json \
  --output artifacts/pipeline-verify --workers 0 1 2 4 8 --delays 1 3 8 --verify
python3 test/benchmark_gradient_pipeline.py /path/to/glob2 scenarios.json \
  --output artifacts/pipeline-timing --workers 0 1 2 4 8 --delays 1 3 8 --repeats 5
```

Verification compares exact per-tick traces, outcomes and scheduling counters
within each delay. Timing omits traces and retains commands, input/binary hashes,
per-child wall/CPU/RSS measurements, warmups and repeated medians. Compare each
worker count with its zero-worker control before comparing against legacy
scheduling. A shorter game caused by changed decisions is not evidence of faster
ticks. Retain fixed-tick windows as well as full-game measurements, and record
machine contention when interpreting results.

### Synchronous field preparation

Field preparation runs synchronously at submission; only propagation runs on
these background workers. Preparation includes reading current resources, terrain,
occupancy, team areas, fog and market availability into the job-owned seed buffer.
Do not attribute the entire resource/area-gradient scope to background CPU, or
add its inclusive time to propagation time. The `initialize` compute experiment
can split the seeding loop into blocking chunks; compare it against one-thread
`initialize` with identical AI settings. Comparing it directly against default
`ai` mode also changes where AI work runs.

Resource preparation resolves terrain passability once per call and specializes
market/visibility policy outside the cell loop. Clearing preparation similarly
resolves clearability and terrain crop policy once, without per-cell coordinate
conversion. These are local lookup tables, not persistent seed caches: every
preparation still reads the current map, and clearing/resource goal precedence
is preserved. Positive swim classes share initial passability but retain their
existing movement costs during propagation.

`GradientPreparation` in the engine test registry compares seed buffers against
the original scalar predicates for every resource and swim class, terrain types,
team masks, fog buffers, market stock and clearing/farming precedence. It also
checks mutations, map resizing, integration with guard crowding, and concurrent
lazy resource-field requests. Run it with:

```sh
python3 test/run_tests.py --binary engine --filter 'GradientPreparation/*'
```

Field equality complements, but does not replace, per-tick continuation and
save/resume verification at every pending-job deadline phase.

For preparation experiments, measure submitting-thread CPU, all-thread process
CPU, complete-tick elapsed time, worker waits and peak memory separately. Include
cache construction, mutation bookkeeping, patches and buffer copies. Repeated
copies of one unchanged field are only a microbenchmark; use rotating owned
buffers, retained game states and dense/high-mutation controls as well. Keep
correctness exports out of timing runs, alternate baseline/candidate order, and
retain commands, source and executable hashes, fixture hashes and raw samples
under `artifacts/`. Saturated-host latency results cannot establish a production
speedup even when the seed kernel uses less thread CPU time.
