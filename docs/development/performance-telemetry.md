# Engine performance telemetry

Performance collection is automatic. `GLOB2_TEAM_TIMELINE=1` exports it alongside gameplay
and AI statistics. It is local to an execution session: loading starts fresh timing coverage,
and no performance data enters saves, orders, RNG, simulation checksums, or AI decisions.
Performance counters are not serialized. Controller and scheduling continuation state
have their own save-format rules; timing collection does not change them.

## Meaning of the measurements

Wall durations are monotonic elapsed nanoseconds. Always-on collection does not read
thread CPU clocks. Descheduling and blocking inside an instrumented operation remain
in its duration. Rendering measures host submission and presentation wall time;
sampled GPU execution is a separate measurement where supported (see permanent
charts below). Physical display scanout is not measured. GPU timing is nonblocking,
and collection does not request additional world scans.

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
  render cadence. Normal-speed steps have a 33⅓ ms budget. Headless/uncapped runs have no
  budget classification. Mode/speed changes break interval chains and overrun streaks.
- Save queue, hash, and write measurements describe background work (or the existing
  synchronous fallback if thread creation fails). They must not be added to main-thread work.
  They are published under the writer's existing lock on the next submission or final wait.
  `saved_total`, `failed_total`, and `superseded_total` are session counters.

Scopes cover units/buildings/tasks, map updates, fog, scripts, construction projects,
statistics, orders/replay checksums, AI totals and observation/planning/gradient phases,
resource/building/area gradients and propagation, pathfinding, rendering passes,
loading/generation/site assignment/site relaxation/validation, and save/output work.
Per-player `ai.player` scopes identify player, team, implementation, and controller
generation where the synchronous polling API is instrumented. Scheduled worker decisions
use the explicit `ai_pipeline` metrics below; worker-local scopes are not merged into the
owner collector. Replay timings describe replay execution, not reconstructed original
AI decisions. Audio callback time is not instrumented. GPU execution timing is capability-dependent;
see the permanent rendering measurements below.
Startup timings include generation attempts performed while preparing the session.

## Cost and sampling

`PerformanceScopes.inc` defines stable scope names and sampling strides. Major phases and
expensive rebuilds are timed on every execution. Hot path-query/direction scopes count every
call and time one in 64 calls. The selected residue rotates between capture windows without
using gameplay RNG. This spreads sampling across periodic callers, but does not make the
estimate statistically unbiased for every workload.

Each scope retains fixed-size online moments: count, integer total/maximum, mean, M2,
and an integer duration histogram.
There is no event log, per-event allocation, string lookup, or sorting. Nesting is bounded at
64 timed scopes; player attribution retains at most 128 controller generations per session.
Overflow reports dropped records/samples and continues the game normally.

Inclusive durations overlap and must not be summed into a supposed engine total. `self_ns`
is available only when all instrumented child timing is exact. Uninstrumented work remains
in the parent's self time. Encountering a sampled descendant makes self time unavailable,
rather than subtracting an estimate from an exact duration.

Collection cannot be disabled through the environment or gameplay settings. Benchmark
comparisons must use a preceding executable with matching fixtures and build inputs. `GLOB2_PERF_BUILD_LABEL` optionally supplies a build identifier
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

A sample closes at the end of the iteration crossing a 512-tick boundary, or after one
second without a tick-based capture. Mode changes close the previous interval. Windows
continue during pause/stalls. A bounded compacting performance history is retained for the live/results charts;
explicit output streams the original interval records before compaction.

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

When the simulation runs on its own thread (`src/engine/sim/SimulationRunner.h`), that thread records
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

Forbidden-area escape and gradient debug rendering finish the relevant cached
fields. Existing refresh deadlines and use timestamps are preserved.

Saving finishes pending fields from their original snapshots without refreshing
them, then writes the existing complete-field representation. Loaded fields start
complete; no format or version change is needed. This trades some play-time work
for save-time work: include autosaves when evaluating latency. Retained queues and
water snapshots also add memory.

`gradient.building` measures initialization and search setup.
`gradient.building_resume` measures actual lazy extensions and completion, including
those nested in saving. Sum these two scopes to compare building-field construction,
but do not then add inclusive save timings to that total. Benchmark evidence belongs
under `artifacts/`, not in this guide.

`gradient.propagation.area` and `gradient.propagation.resource` wrap the
`propagateGradient` calls of forbidden/guard/clear area fields and synchronous
resource fields respectively, so that the shared `gradient.propagation` scope can be
split by caller.
Periodic pipeline fields propagate on workers and are not included.

### Building field statistics

Per-search counters, depth histograms and lifetime CSV exports have been retired.
The production depth predictor retains only reader-demand history. Use external
profilers and the shared executor diagnostics to measure execution.

## Optional compute executor diagnostics

`play`, `replay` and `game run` accept `--compute-telemetry off|waits|trace`.
The default is `off`. This option controls executor and producer pipeline diagnostic
collection independently of ordinary performance scopes and GPU/backend diagnostics.
Producer outcome counts (published/discarded work, proposals and throughput) remain ordinary
telemetry; this mode controls optional executor accounting and producer timing.
Mode changes drain required work before selecting worker and callback specializations.
Reconfiguring workers or diagnostics starts a fresh collection interval; exports cover
that interval or the time since the latest load/restart reset.
No diagnostic metadata is saved, replayed, hashed, or used to choose simulation results.

- `off` allocates no optional diagnostic sidecars or trace storage. Permanent
  wall-time and bounded dependency summaries still collect; optional event writes
  and diagnostic-clock calls remain absent.
- `waits` records submissions and owner joins under the existing executor mutex.
  Workers use the same specialization as `off`; optional worker callback records
  are unavailable, while permanent callback wall-time summaries still collect.
- `trace` also records scheduling, callbacks, suspension, external resolution,
  continuations, completion and retirement. Callback end is distinct from completion.
  A continuation retains its batch/job identity; repeated claim/start/end events identify
  successive invocations. Blocking fork/join and presentation events have separate
  `execution_kind` namespaces (0 deferred, 1 fork/join, 2 scene preparation, 3 graphics batches, 4 worker-only maintenance).
  Maintenance has no required join or simulation deadline; its events show worker occupancy.

The zero-delay AI placement policy still measures decision duration in all modes: it
uses that duration to choose owner or worker execution. Those clocks are scheduling
inputs, rather than optional diagnostics. Delayed AI callbacks do not read that clock
in `off` or `waits`. Optional gradient GPU diagnostics and backend calibration also
retain their own controls and clocks. Turning compute diagnostics off does not disable
those independent features.

Headless `result.json`, turn network session summaries, and the local
`GLOB2_COMPUTE_SUMMARY` JSON record emitted with `GLOB2_TEAM_TIMELINE=1` contain a
versioned `compute_telemetry` object (the timeline record contains that object directly).
Disabled exports contain `schema_version: 1`, `mode: "off"`, `available: false`.
Allocation failure disables collection or downgrades tracing to waits; `requested_mode`
identifies a downgrade. Legacy headless timing/counter fields are `null` when unavailable.
Pipeline join timing is derived from the common executor summaries; its population
now includes valid joins only, excluding empty/stale handles. AI deadline metrics also
come from these summaries. Raw pipeline diagnostic structs do not independently time joins.

Each producer/reason entry contains joined and incomplete batch counts, background wait
attempts, owner execution joins, and duration totals, maxima and fixed histograms.
Histogram bucket 0 is zero ns; bucket n>0 covers `[2^(n-1), 2^n-1]` ns, through bucket 64.
An unfinished batch under the completion lock counts as a wait attempt even if completion
occurs before the owner sleeps. Background wait duration includes completion notification,
scheduling and mutex reacquisition. It is elapsed time, not worker CPU time.
Join lock time measures acquisition; total join time ends after slot retirement/accounting,
before mutex release and exception delivery. Failed batches are counted before rethrowing.
Fork/join summaries also report the owner's work before its completion barrier.

Producer attribution identifies the work awaited. Owner fallback duration can include earlier
deadlines from other producers. It is not the awaited producer's callback execution time.
A mixed batch has one `mixed` attribution. Save, reconfigure, teardown and verification joins
remain separate from tick deadline and immediate required joins.

Required joins with a tick must arrive in nondecreasing logical tick order within a session.
Lifecycle calls use `ComputeTelemetry::NoTick`. Producers explicitly map their local advance
boundaries onto the simulation tick, independently of executor deadline keys. Repeated waits
on a tick add elapsed time, while per-producer and global affected-tick counts increment once.
Advancing ticks are counted after successful simulation advancement, excluding pause and
network stalls. Current tick duration is included in snapshots without mutating the collector.
Frequency denominators are `required_batches_joined` and `advancing_ticks`; their numerators
are `required_batches_incomplete` and `background_wait_ticks`. A zero denominator has no
measured rate. Loading/restarting clears session summaries and streams after producers drain.

In `trace` mode, `deferred_callback_execution` contains full-interval duration
histograms by producer, and `ai_lane_callback_execution` splits AI callbacks by
controller lane (player index). These aggregates continue collecting after event
storage fills. They measure elapsed callback time, including descheduling, rather
than CPU time, queue delay or an external operation's completion. Each continuation
invocation is a separate sample. Blocking fork/join and presentation callbacks are
outside this deferred-callback population. The fields are `null` in other modes.
Hiring and team statistics have distinct `hiring` and `team_statistics` producers.

For slow-tick investigation, add `--benchmark-warmup 0 --benchmark-tick-profile`
and `--compute-telemetry waits` or `trace` to `game run`. `tick-timings.csv` retains
every measured tick's elapsed time; `slow-ticks.json` retains the 1024 slowest with
per-producer required-wait durations and owner performance-scope deltas. Tick IDs
refer to the counter before the step. Phase durations are inclusive and overlap;
sampled phases export observed time and their sampling stride. Collection and file
export happen outside timed steps, but can still perturb scheduling; `collection_ns`
reports collection cost. No detailed per-tick data is collected without the flag.
For long runs, 1024 retained ticks may cover less than the slowest one percent.
Use initial saves and the exported tick IDs to repeat a problematic interval; a
bounded full-session event trace may have already overflowed by a late stall.

For owner-thread stalls outside executor joins, `--owner-tick-profile` with
`--benchmark-warmup` exports `owner-ticks.jsonl`. Each row contains tick wall time,
owner-thread CPU time where supported, and inclusive/exclusive times for orders,
the simulation step, statistics, world capture, checksum collection/submission
and AI admission. `--owner-profile-min-ms` defaults to 20; zero exports every tick.
`--owner-profile-from-tick` skips earlier ticks. Export is outside the timed step.
Nested phase times overlap; exclusive times are additive. Owner CPU time excludes
workers and time blocked or unscheduled, so a wall/CPU gap alone does not identify
which condition caused it. These extra clocks perturb timing and are intended
for attribution rather than throughput comparisons.

`game run --ai-decision-profile` additionally exports slow callbacks to
`ai-decisions.jsonl`, keyed by AI, player, observation tick, publication tick and
poll sequence. `--ai-profile-min-ms N` selects the elapsed threshold (default 20,
0 records every callback); `--ai-profile-from-tick N` skips earlier observations.
Each record contains callback wall time and, where `CLOCK_THREAD_CPUTIME_ID` is
available, that worker's CPU time. CPU time excludes time spent blocked or
unscheduled. Selected Maxima and Cortex functions also export call counts,
inclusive/exclusive wall times and workload counters. Maxima scopes distinguish
strategy, farming, clearing, defense, offense, reconnaissance and building
management from placement and food-ledger work. Residual-bound queries
are counted without individual clocks because they are very small. Function
timing adds overhead: use these runs for attribution, not throughput comparisons.
No profile data influences orders, RNG, saves or simulation state.

For an exact callback stack sample, `--ai-profile-gate P:T` holds player P's
observation-T callback before its body and writes `ai-profile-ready.json` in the
output directory. Attach a sampler, then create `ai-profile-release` there. The
worker is named `aipPtT` on macOS/Linux where supported. On body completion it
writes `ai-profile-done.json` and waits for `ai-profile-finish`, so that thread
executes no subsequent callbacks during collection. The body record excludes
both artificial pauses and is exported regardless of threshold. Gate runs change
wall-clock scheduling and must not be mixed into natural latency statistics.
The initial wait times out with an error after 60 seconds; the completion wait
ends after 120 seconds. The sink outlives all decision jobs during teardown.

Trace event arrays share a default 16 MiB budget per executor, split among simulation owner, graphics owner, workers
and an external completion stream. The budget covers event storage; bounded summary and
stream metadata are additional. Configuration allocates all arrays. Full arrays drop new
events without blocking or allocating; `trace_lost_events` and `trace_incomplete` expose loss.
Scheduler/external writes use existing executor locks; fork/join callback starts use their
thread's single-writer stream. Summary snapshots copy synchronized counters. Full event
snapshots require drained required work and idle scene/graphics presentation; they never inspect a running
writer. Headless and final timeline exports satisfy that boundary. Network snapshots omit
event arrays (`trace_included: false`) and retain loss counters.

Analyze a full export with:

```sh
python3 tools/compute_trace.py artifacts/run/result.json --output artifacts/run/compute-analysis.json
```

The reader preserves missing evidence as `null`, reports truncation, and derives callback
elapsed time, lane predecessor delay, time queued after eligibility, external resolution
after callback return, and completion-to-join latency. These intervals describe observed
elapsed delays; eligible queue time does not identify which competing producer occupied
workers. Inspect overlapping events to investigate that cause.

Worker-only maintenance can span a snapshot: its event writes still use the scheduler
mutex, and `trace_open_work` marks an open interval. A session reset discards the
previous maintenance interval; an epoch check suppresses its returning events, while
`trace_coverage_gap` marks that missing prefix until the next configuration/reset.
Both flags contribute to `trace_incomplete`, independently of dropped-event counts.

## Permanent engine performance charts

Live statistics and the results screen include **Game engine performance**. Collection
is mandatory; the former `GLOB2_PERF_DISABLE` environment variable has no effect.
Export still follows the existing team-timeline stream, so ordinary play does not
create additional files. Loading or starting another match starts a new local
performance session; performance history is not saved or reconstructed by replays.
The completed session remains available while viewing its results.

All computation charts use **monotonic wall time**, including owner advancement,
executor callbacks, per-player AI callbacks, software rendering and save stages.
Measurements include time spent blocked or descheduled within each scope. Compare
owner execution, required joins and background waits to interpret busy-machine
results. No thread CPU-clock reads are performed by always-on telemetry. Reserved
CPU export fields remain unavailable (`cpu_samples=0`, CPU durations `na`), and
descriptors identify `cpu_source=unavailable`; wall durations are never labelled CPU
time. Existing explicitly requested diagnostic profilers retain their own contracts.

The live sidebar provides session-wide TPS, tick duration and wait frequency, with
**Open performance charts** opening a dedicated responsive dialog. That dialog and
the results screen share a curated dashboard: achieved/requested TPS, mean/p95 tick
duration and budget, exposed waits per advancing tick, affected ticks and overruns,
frame intervals and budget, jitter, and CPU-side rendering wall time versus sampled
GPU execution. Two columns collapse to one on small screens or larger text. The
whole-session/last-five-game-minutes range applies to every chart and task-cost row;
hover or tap selects one logical game time across charts; **Follow latest interval**
clears that selection. Readouts explicitly identify latest versus selected intervals.
Shading marks uncapped or mixed tick-target intervals. Budget lines follow each
window; absent or mixed budgets have gaps. Gold markers identify interruptions and
mode changes; gold baseline spans identify compacted intervals. Costs use complete
stored windows intersecting the selected range, including the full boundary window.
Task costs are grouped by owner, worker callbacks, and rendering/supporting work,
with ms per advancing tick and total ms; inclusive rows are not a bottleneck ranking.
**All metrics and slow ticks** retains the detailed metric explorer.

The catalog exposes existing scopes, rendering counters and executor summaries;
its overview includes tick wall cost, achieved/requested TPS, presented FPS,
frame jitter, wait frequency and budget misses. Rates default to game minutes
(1,800 simulation ticks) and can use elapsed real minutes instead. Paused windows
have no game-minute denominator. Missing coverage and unsupported measurements remain
unavailable. For wait means, totals, calls and per-tick costs, an advancing interval
with owner-advancement coverage and no wait events displays zero; event percentiles
remain unavailable without samples. This presentation rule does not invent samples
or change exports.
Inclusive wall scopes overlap; do not sum parents and children. Exclusive wall
views require complete child coverage.

Windows close after 512 ticks or one second, on execution-mode changes, and at
finalization. A fixed history allocation of at most 12 MiB preserves the entire
session by merging the adjacent windows with the finest combined coverage, breaking
ties by age. Retained windows occupy stationary slots: compaction moves only small
logical-order indices and reuses the merged-away slot. Capture copies the current
metrics directly into that slot, without an intermediate full-window copy. Counts, totals, integer histograms and parallel variance moments merge
without averaging percentiles. Hover readouts identify merged windows and execution
modes. Window `boundaries` is a bit mask: 1 renderer/device generation change,
2 presentation gap over two seconds, 4 suspension, 8 resume. A long gap is observed
elapsed time, not proof that the application was backgrounded. The 64 slowest owner ticks retain wall cost, required-join costs and phase
deltas (orders, units, buildings, map, statistics, AI, world capture, checksums).
The owner collector, simulation mailbox, simulation collector, shared worker sink,
executor summaries and causal sidecars fit the remaining 4 MiB allowance, including
the retained decorative menu executor and bounded chart/GPU presentation storage.
Timing banks are allocated before workers start: the serial menu needs only one,
while the foreground executor reserves banks for its configured participants.
Explicit event tracing retains its separate diagnostic allocation.
GPU query/readback storage and chart projections are bounded. The release-build
collection-overhead acceptance target is 2%; fast-build tests do not establish it.

When profiling collection overhead, compare identical release fixtures with native
process CPU measurements as well as instruction profiles. Callgrind instruction
counts exclude execution inside kernel system calls. Process CPU measurements in
explicit benchmark runs are separate from the game's wall-time telemetry. Measure
export overhead separately from collection.

Permanent executor summaries operate independently of `--compute-telemetry`.
Callback aggregates publish outside the scheduler mutex through four fixed timing
shards. Participant indices use independent shards up to four participants; larger
pools share stripes, keeping the storage bound independent of thread count. A shard
can delay timing publication, but it never holds the scheduler mutex while doing so.
Callback samples publish before completion becomes visible. Interval absorption
locks timing shards only, without parking workers or acquiring the scheduler mutex.
Each shard tracks its active metric IDs, so absorption/reset visits only populated
metrics. The owner uses one due decision for shard absorption and interval closure;
measurement frequency and the one-second/512-tick cadence are unchanged.
Implicit worker collectors are suppressed because their scopes are not absorbed by
the owner. Callback durations belong to the next interval that absorbs their shard;
shards are handed off independently, so a callback racing a capture boundary may
appear in the following interval. The simulation mailbox transfers samples and its
completed-tick watermark under the same lock, including the initial loaded tick.
Finalization drains outstanding jobs even with optional diagnostics disabled,
without publishing future simulation outcomes. Final totals remain complete. Required-join
snapshots use relaxed atomic counters
without acquiring the scheduler mutex. Detailed optional traces retain their own
existing diagnostic-lock contract.

The scheduler retains only coarse causal timestamps and scalar dependency state.
At batch retirement, the owner copies at most 64 compact causal observations while
the batch is protected, then aggregates queue, lane and external durations after
releasing the scheduler mutex. Those causal distributions belong to the retirement
window; callback durations still publish at callback completion. Histograms and
variance updates never run under the scheduler mutex for permanent telemetry.
Job callback wall durations, player lanes, gradient subtypes, required waits, incomplete
deadlines and unique affected ticks are available with diagnostics `off`.
Turrets have their own producer and tick-deadline joins. Save/teardown joins do not
count as required tick waits. A tick can wait for several producers, so per-producer
wait-tick counts must not be summed into a global affected-tick count.

Causal metadata retains at most 64 job records per live batch and one completion
timestamp per lane. Callback totals remain complete beyond that bound; missing
causal detail increments `compute.causal_unavailable`. Retirement retains no growing
predecessor chain. Queue delay starts at lane eligibility; external suspension ends
at resolution, and continuation invocations remain part of the same job. Required
joins expose the last prerequisite's callback, lane, queue and external durations
when evidence exists. These describe its observed lifetime, including time before
the join: they are not additive portions of the exposed stall, a counterfactual
speedup estimate, or proof of which competing task occupied workers. The additive
owner wall accounting separates required lock acquisition, owner execution,
background waits, and time outside those joins.

### Rendering and GPU timing

Render telemetry retains packets, batches, upload bytes, tracked resource bytes,
submission/presentation/capture wall time, capture completions, latency,
and drops. Tracked resource memory is not total GPU/driver memory. Frame intervals
and consecutive interval changes describe presentation returns, not display scanout.
Headless sessions do not fabricate rendering measurements. Capture latency is a
frame-level counter delta: its mean divides accumulated latency by completed
captures, and individual-capture percentiles are unavailable. GPU descriptors
identify elapsed-query intervals separately from timestamp-query render-pass sums.

GL/GLES uses optional nonblocking elapsed-time queries where the timer extension and
entry points exist. WebGPU requests its optional timestamp-query feature and sums
sampled render-pass durations; its `render.gpu.render_passes` scope excludes copies
and is distinct from GL's `render.gpu.execution` interval. SDL GPU retains CPU-side
measurements and reports GPU execution timing unavailable when its pinned public API
provides no query support. No runtime dependency changes are needed.

GPU sampling selects one in 16 submissions/frames with a rotating residue. Each
backend retains at most 32 outstanding slots. Polling never waits for completion;
full rings skip measurements, disjoint results and device resets invalidate them,
and finalization reports unresolved queries without waiting for them. WebGPU also
bounds each sample to 32 render passes; larger samples are marked invalid rather
than reporting a truncated duration. Late results update the original retained
window by timestamp, including a compacted window.

### Performance export version 2

Existing `GLOB2_PERF_*` records and fields retain their meaning. Schema records add
units, category, source and aggregation information. Duration samples add
approximate histogram upper-bound percentiles and a quoted
comma-separated integer histogram (bucket zero is zero; bucket n covers
`[2^(n-1), 2^n-1]`). Counter/gauge scopes use `total_value`, `mean_value` and
`max_value` with their declared units rather than pretending bytes are nanoseconds.
Reserved CPU fields are unavailable in new wall-time records. Readers still preserve
CPU fields in historical exports without reinterpreting them as wall time.

`GLOB2_PERF_RENDERER` records backend generations and timing capabilities.
`GLOB2_PERF_GPU_RESULT` carries individual frame/submission identity, original elapsed
timestamp, GPU scope and duration. These raw GPU observations overlap interval/final
aggregates; choose one population, never sum them together. A result arriving after
its interval was exported is available through the raw result and final total;
previously streamed intervals are not rewritten. `GLOB2_PERF_SLOW_TICK` contains the
bounded slow-tick evidence. Existing offline readers preserve these record families,
exact integers and nulls in the same JSONL/CSV outputs as gameplay measurements.
Compare builds, hosts, rendering backends and execution modes separately.
