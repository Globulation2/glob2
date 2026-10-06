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
timestamps. Already-settled targets are excluded before dispatch. This can still
perform unnecessary work and must be measured separately.
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

### Building-gradient work within a tick

Structured `--run-game --telemetry building-gradients` writes
`building-gradients-ticks.csv` and `building-gradients-events.csv` in its output
directory. This diagnostic is opt-in and does not enter simulation state, saves,
orders or checksums. It captures executed simulation ticks only; setup, AI polling,
saving and teardown are excluded. Tick durations exclude the diagnostic census
and CSV output, but include event-recording overhead. Use separate runs with the
diagnostic disabled for performance comparisons.

The tick census records buildings (including virtual flags), flags separately,
units, retained building fields and unfinished searches. Events identify building
gid, swim class, requesting phase, current topology generation, search snapshot
generation, start offset, elapsed and thread CPU nanoseconds, queue entries
processed and completion. CPU time excludes descheduling and is zero when the
platform cannot provide a thread CPU clock. Tick CPU time covers the simulation
thread only; rebuild CPU time excludes initialization workers, so use elapsed
time and executor metrics when comparing parallel initialization. Rebuild reasons distinguish missing fields, topology/dirty changes,
clearing refresh timers and stuck-unit refreshes. Propagation records only calls
that advance a search; cached reads do not appear. Full-completion callers distinguish
round-trip construction, forbidden escape and the full-field API. Buffer allocation
and reuse cover both building and round-trip fields; idle eviction and explicit
cache invalidation identify the field kind. Hiring-batch events distinguish
batches that advance queues from batches that only revisit settled cells. Round-trip timings include any parent completion, so do not add them
to building propagation as exclusive time.

Events are retained per tick, capped at 65536 with dropped-event counts, then
written at the tick boundary. Worker records use a mutex; all workers finish their
existing batch before publication. The diagnostic introduces clock reads and
storage overhead and is intended for finding workloads, not proving speedups.

```sh
python3 test/analyze_building_gradients.py artifacts/run-a artifacts/run-b \
  --output artifacts/building-gradient-summary.json
```

The analyzer reports tick-cost percentiles, distinct fields, rebuild causes,
repeated extensions, cache activity and the worst ticks. Its independent-field
cost bounds assume no dependencies, speculative work, dispatch cost or memory
bandwidth limits. They are optimistic opportunities, not measured speedups;
whole-tick elapsed ceilings are omitted when measured building work already overlaps.
CPU ceilings are reported only for serial building computation (`none`/`ai` modes),
using per-field CPU work and the simulation thread CPU budget. They do not describe
whole-engine throughput, which also includes AI polling and other work outside the tick.
Sharing a topology generation and phase does not prove fields can be scheduled
together: immobile blockers and caller-visible state can change within a phase.


### Scheduled building-gradient experiment

Two additional saved, opt-in policies require `building-gradient-pipeline`:
`building-gradient-hybrid` admits a background refresh only when at least four
living assigned workers use that movement class. Cold destinations retain the
existing synchronous lazy behavior. A change in staffing never moves a pending
publication deadline. This classifier uses simulation state, not timing or the
number of available cores.

`building-gradient-partial` resolves captured worker positions, target tiles and
adjacent steps privately, then publishes the field and its first unsettled cost
layer. Simulation queries resume against the captured immutable terrain costs.
Round-trip searches support unequal starting costs and retain their propagation
cap. Walking parents with allocated round-trip children still finish completely
before those children are seeded. Unexplored cells are never read as unreachable:
point-query adapters settle their inputs, full-field APIs finish their searches,
and audits complete detached copies. Overflow spooling and saving materialize
private results without advancing their publication deadlines.

These are separate policies and can be combined. Both remain off by default;
their performance must be measured against the original pipeline and the lazy
baseline using the same executable and workload.
Retained methodology and results: [building-gradient hybrid experiments](building-gradient-hybrid-results.md).

`building-gradient-pipeline` is off by default. Eligible cached walking or
round-trip refreshes request a coherent building/swim bundle. At the end of tick
T, the Map captures passability, destination metadata and the currently published
resource parents. A shared asynchronous executor builds the walking field and
already allocated round-trip children without accessing live simulation state.
At the beginning of T+D, the simulation waits for due work and publishes in stable
destination order. D is the saved `buildingGradientDelay` rule (2/4/8; default 4).
`--gradient-workers` controls the shared resource/building execution budget locally;
zero workers executes privately on the submitting thread with the same deadlines.

First construction stays synchronous and demand-driven. Repeated requests do not
move a pending deadline. Invalidation after capture stays dirty for the next
refresh; invalid lifetime or eviction tokens discard results. Cold children
allocated after capture retain their synchronous results until a later bundle.
The experiment retains independent access metadata for each swim-cost class;
later invalidation does not clear published metadata before its replacement arrives.

Build buffers and shared snapshots have a 64 MiB queued RAM budget, excluding
published caches, worker scratch and small scheduling metadata. Destinations are
admitted in stable order. A job that would exceed the budget builds synchronously
at submission and retains its completed fields in an anonymous temporary spool.
It still publishes at the original T+D deadline. Spools are shared within each
submission wave, so file handles do not grow with the number of overflow jobs.
Private files disappear when their results retire; saves contain the actual
fields and deadlines rather than machine-local paths. Restored overflow results
remain spooled, and resident jobs retain their original reservations. Saving
finishes private jobs without publishing early.

`result.json` includes snapshot time, background elapsed and thread CPU time,
coalescing, publications/discards, synchronous fallback,
deadline waits and peak queue reservations. Full-field background work can exceed
the old lazy search work; evaluate CPU and memory alongside engine elapsed time.

`--telemetry building-gradient-timing` retains lightweight per-tick durations,
deadline waits and queue occupancy in memory and writes
`building-gradient-timing.csv` after the run. It performs no fresh-field auditing
or per-tick file writes. Use this mode for tick-tail distributions in timing runs.

`--telemetry building-gradient-impact` writes decisions, outcomes and tick census
CSVs prefixed `building-gradient-impact`. Movement, fetch ranking and hiring use
shared scoring/tie-breaking evaluators. Each audited decision compares published
fields with a fresh private build from current state, holding published resource
parents constant. Audit work does not consume RNG, update ages, tallies or call
lists, or publish fields. Equal-cost legal directions are reported separately from worse steps, stale
failures and stopping at an obsolete goal. Resource comparisons include type, market
identity and predicted tile. Actual map harvests and market acquisitions have
separate outcome kinds, emitted at the real acquisition. Market outcomes include
the source building's identity; carrying-state transitions are not treated as map
harvests. Weighted movement-cost distributions include only decisions with both
costs available, with missing-cost denominators reported separately. Forbidden
escape and resource-parent goal stops use the same rules as live movement.

Hiring episodes track both stale rejection of fresh-eligible candidates and decisions
where the fresh evaluator would hire but live execution hires nobody. Episodes end
when hired, unavailable or demand disappears; unfinished or unavailable episodes
are censored, with separate reasons for disappearing demand, unavailable
candidates and the observation horizon. Decision and outcome records include unit and building lifetime
identities so recycled numeric IDs cannot join unrelated episodes. Outcome event
indices distinguish events before and after an intervention within the same tick. Trips report elapsed ticks, distance, reversals,
deliveries and abandonment. Tick census records unfilled staffing, construction
completions, hunger and deaths. CSV scores/rejection reasons and denominators must
accompany discrepancy counts. Fresh-field discrepancies also exist with the
experiment off: compare baseline audit rates, rather than attributing all stale
cache decisions to the additional publication delay.

The report separates complete-trip duration from elapsed observations of trips
already underway in the starting checkpoint. Those initial trips have unknown
start times and contribute lower bounds, while their deliveries still count
toward throughput. Read completed durations alongside abandonment, unfinished
trips and missed-hiring censor reasons.

Delivery events count trips, not accepted resource quantities. For economy
throughput, also use the existing `team-timeline` measurements: subtract the
starting checkpoint's cumulative `delivered_*` counters from the final counters.
These include capacity clamping and building delivery multipliers. Keep per-resource
totals and starvation deaths alongside trip counts; companion runs must use the
same checkpoint, rules and AI settings and finish with matching simulation state.
The existing `harvested_*` counters include market pickups and are gathered-resource
totals. Use impact outcome kinds when separating map harvesting from markets.

`test/analyze_building_gradient_impact.py` summarizes distributions and retains
the first 20 changed decisions per category. `test/benchmark_building_pipeline.py`
runs worker/audit determinism checks, balanced quiet timing rounds, audits and
paired 512-tick counterfactual continuations. The latter replay the same checkpoint
to a recorded decision and substitute fresh fields in only one continuation;
unfinished outcomes remain censored. Audit runs are separate from speed tests.
Normal continuation outcomes can be reused from the uninterrupted audit, bounded
to the intervention's 512-tick horizon, alongside an independently replayed normal
checksum trace. This avoids repeating observational oracle work for that side.
Cases at one delay share an uninterrupted normal reference covering the latest
intervention plus 512 ticks. If that exceeds the original audit horizon, the
reference also collects outcomes, so both branches have the same observation
window. Reusing a shorter audit must never shorten only the normal branch.
For late decisions, `--counterfactual-checkpoints` first creates a checkpoint at
the preceding tick boundary. It verifies the checkpoint prefix and all resumed
normal team/unit/building records against the uninterrupted continuation, then
forks the normal and fresh runs from identical saved bytes. The recorded decision
must match every field except the collector's restarted event counter; ambiguous
matches or a divergent resumed state fall back to the original workload
checkpoint and replay the full prefix. Malformed traces still abort the run.
Decision rows before the intervention must also match. An optional
`--counterfactual-checkpoint-binary` can use a corrected save writer while
retaining the measured binary as the uninterrupted reference. Record both
binary hashes; accept the optimization only after the complete resumed state
and pre-decision observations match that reference. Save-header metadata is excluded from continuation comparisons,
as in the save-continuation verifier. Observed trip duration, distance and heading
can be left censored at this common checkpoint; retain that scope with the case.
Paired cases join unit and building lifetimes and the decision's event boundary;
a delivery preceding the intervention, or a replacement trip after abandonment,
cannot complete the affected trip. A later hire after the original demand or
candidate disappears does not turn a censored opportunity into an exact delay.
`--workload-jobs` can run independent audits
or workload continuations concurrently; timing rounds always run one process at a time.
For separate diagnostic processes, `--counterfactual-delay 2|4|8` restricts each
process to one delay. Distinct delays in a workload merge progress under a file
lock with atomic replacement; never run two writers for the same workload/delay.
`--counterfactual-event T:E` selects one decision group, including its affected
categories. Use separate output roots to run these groups concurrently. The full
inventory still determines the normal reference horizon; merge completed groups
by delay, category and case identity, checking coverage against the original
inventory before reporting results.
Diagnostic summaries retain case rows and distinct intervention counts separately,
both-side completion and censor denominators, and observed duration/distance/
reversal differences. These selected cases do not establish population averages.
Generated evidence belongs under ignored `artifacts/`.
The driver checkpoints completed execution metadata before analysis; independent
audit summaries run in separate processes so Python CSV parsing does not serialize
workloads. Analysis and compression can resume without repeating the game run.

Use `test/report_building_pipeline.py --heavy-workload NAME` repeatedly to declare
a comparable seeded-match cohort alongside `--resource-volumes`. The report gives
equal-weight paired seed means, pooled resource totals, ranges and exploratory
95% Student-t intervals; timing intervals use paired benchmark rounds. Seeds must
be distinct, and mixed AI or differently sized workloads should stay separate.
Decision events and trips within a match are correlated and do not increase the
independent sample count. Small cohorts can leave wide intervals: an inconclusive
average neither establishes a regression nor demonstrates the desired safety margin.
Retained first-discrepancy cases explain mechanisms, rather than estimate average
population effects.

### Building-gradient instrumentation control

Headless `--building-gradient-instrumentation off` disables building-scheduler
clock/CPU measurements, diagnostic counters and diagnostic peak-queue scans. It is
local execution configuration: admission accounting and fixed publication deadlines
remain active. Omit building-gradient timing, event and impact telemetry in this
mode; conflicting combinations are rejected. Default `on` preserves existing
measurement behavior. To isolate overhead, pair on/off runs of the same binary
and configuration, and separately compare pipeline-on/off with instrumentation
disabled. Verify matching simulation traces before drawing performance conclusions.
