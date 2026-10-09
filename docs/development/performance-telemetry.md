# Engine performance telemetry

Performance collection is automatic. `GLOB2_TEAM_TIMELINE=1` exports it alongside gameplay
and AI statistics. It is local to an execution session: loading starts fresh timing coverage,
and no performance data enters saves, orders, RNG, simulation checksums, or AI decisions.
Performance counters are not serialized. Controller and scheduling continuation state
have their own save-format rules; timing collection does not change them.

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
AI decisions. Audio callback and GPU execution times are not instrumented.
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

`--telemetry gradient-stats` (environment `GLOB2_GRADIENT_STATS=1`) gives the map a
diagnostic `BuildingGradientStats` (`src/map/gradient/BuildingGradientStats.h`). It only
reads simulation state: checksums, RNG, saves and replays are identical with it on or
off, and without it every hook is a null-pointer test. Each building walking-field
lifetime ends with one row in `gradient-stats.csv` in the output directory: a rebuild
(`reason` = `null`, `dirty`, `generation`, `clearing`, `stuck`, `scheduled` for a
scheduled field's publication, or `other`), a `drop` (`Building::resetPathfindGradients`),
an idle `evict` or the run's `end`. Unless `GLOB2_BUILDING_DEPTH` says otherwise, the
statistics publish scheduled fields with only their seeds settled (`lazy` depth), so each
lifetime's settled depth is what its readers needed; the depth moves CPU, never results.
A row describes the lifetime that just ended (`age`, `prev_complete`, `prev_settled_cost`,
`prev_settled_tiles`, `prev_popped`, `prev_queries` = resolve calls, `prev_extensions` =
calls that expanded the search, and `popped_at_depth_0..31`, popped entries per 80-cost
band of eight land tiles with the last band open-ended) together with how that
lifetime began (`lifetime_reason`). The final columns are the
inputs a depth prediction could read in O(1) when that lifetime started: map
`width` and `height`, the building's `level`, `is_site`, `construction_state`
(`none`, `new`, `upgrade`, `repair`), `progress` (the delivered/needed material
quartile 0-3 on construction sites, empty otherwise), and its team's live
`team_units` and `team_buildings`. The last three columns are the depth model's
inputs at the moment the lifetime's depth was decided: `staged` is 1 when a
scheduled refresh was staged for it (the context is then read at staging, not at
publication), `previous_hint` is the reader-required depth of the lifetime before it
(`settledCostHint`) and `serving_settled` is how deep readers of the field it
replaced required it by then, each -1 when unknown. All are empty when the start was not
seen, for example for a field restored from a save. The historical column names
`serving_settled` and `previous_hint` now record reader-required depth, excluding
worker preparation and serialization completion. `prev_settled_cost` still
records the actual prepared search frontier.

`result.json` then has a `building_gradient` object: `rebuilds` by reason,
`dirty_with_generation` (dirty rebuilds whose topology generation had also moved),
`events`, `popped_total`, `popped_unknown_lifetime`, `popped_by_lifetime_reason` and
`clearing_goal_gone`.
[The building-field depth model](../building-gradient-depth-model.md) is fitted
from these rows.

The statistics stay compiled into release builds and are gated at run time, as
`team-timeline` is: tournament workers run ordinary release binaries. With the flag
off, the hooks reduce to null-pointer tests and a few always-on search counters.
Pinned to four reserved cores on an x86_64 host, busy Oazis (11 Maxima, seed 19)
measured no difference beyond run-to-run noise (about 1%), at 4096 and at 12288
ticks, between master, the branch with the flag off, and the branch with it on.

## Scheduled AI decisions and shared computation

All shipped controllers borrow immutable engine snapshots for decisions. The simulation
owner captures the required component union once per poll tick; unchanged components are
shared, and mutable component buffers are reused only after their consumer leases end.
Map arrays are tracked per 16x16 chunk, and a reused buffer copies only the chunks that
changed since it was last filled. `GLOB2_SNAPSHOT_VERIFY=1` makes every capture byte-compare
its arrays and entity lists with the live game and throw on a mismatch; use it in test runs
after touching map writers, not in timing runs.
Unit membership uses flat offset/count ranges. Controllers keep private strategy state and
bounded query caches, rather than retaining a whole observation between polls. Published
resource fields preserve the map's existing refresh age. An absent field is initialized
from frozen inputs and enrolled by the simulation owner, without refreshing an existing
published field.

The engine polls each eligible controller at most once per logical tick. A tick's
decisions form one deferred batch on the map's compute executor, one job per controller
on that controller's lane: a controller's jobs run in FIFO order without overlap, while
different controllers may run concurrently with each other and with map computation.
One match-wide delay of 0–8 ticks sets the order's deadline, which is also the
batch's due tick: workers run deferred batches earliest due first. At the deadline
the owner waits for the batch (it runs no shared jobs itself), then publishes the
complete batch in player order. A slow worker cannot postpone an order's
logical execution tick. Paused games submit no decisions and advance no deadline clock.
Delay zero still uses captured inputs and the same scheduler: its batch is submitted and
joined inside the tick. Waking a worker costs more than a few microseconds of
decision work, so a delay-zero batch is shared with the workers only when it has more
than one decision and the smoothed decision work of recent batches is at least 100 µs;
otherwise the owner decides it inline when it dispatches, outside the executor. That
choice changes which thread runs a decision, never its result.

Orders carry observed target incarnations. The execution boundary rejects a missing or
replaced target and queues immutable accepted/rejected feedback for a later decision.
Controller replacement joins its work before destruction. Saving drains computation
without publishing future commands early, and retains deadlines, queued commands,
receipts, RNG and controller continuation state. Worker counts remain local execution
configuration. Changing the match delay can change strategy, replay orders and game feel;
changing only worker count must preserve execution at the same delay.

All sessions use `--compute-threads auto|N`, where N is a positive unsigned
integer counting total executor participants, including the simulation owner.
`auto` (the default) uses all reported logical CPU threads, falling back to one
if detection returns zero. There is no separate limit of 64 for explicit counts.
The background pool has N−1 workers. AI, periodic/building gradients, resource
growth and presentation share this pool. `--compute-threads 1` is the serial
compute control. Thread creation failure and platforms without threads also leave
no workers; the owner runs deferred batches at their joins.

`--ai-threads`, `--gradient-workers` and `--compute-experiments` have been removed;
use `--compute-threads auto|N`. The optional area, initialization and hiring compute
paths and their environment selector have also been removed. Structural area
refreshes and live field initialization retain their serial production order.

Structured `--run-game` accepts `--ai-order-delay D` for a new match (default 8
ticks). Delays are simulation rules, separate from local thread configuration.
A loaded match's configured AI delay cannot be overridden.

`compute_requested_threads` records the CLI sizing as a string (`auto` or the
explicit count), `compute_resolved_threads` records the resolved participant count,
`compute_threads` records the actual count including serial fallback, and
`compute_workers` records actual background workers. Automatic sizing should be
measured separately from fixed counts; using every logical CPU is not a speedup claim.

`result.json` retains the map executor's `compute_threads`,
`compute_batches`, `compute_jobs`, `compute_parallel_batches`, `compute_batch_ns`,
`compute_deferred_batches`, `compute_deferred_jobs`, `compute_owner_jobs`,
`compute_worker_jobs`, `compute_join_wait_ns`, `compute_wait_ns`, and
`compute_active_elapsed_ns`. The deferred, owner, worker and join wait figures cover
every deferred batch (AI decisions, periodic and building gradients) as the executor
saw them. `compute_owner_jobs` is zero whenever the executor has workers, since the
owner only waits at joins; `compute_join_wait_ns` is that waiting. The
`ai_pipeline` object reports the AI scheduler's own view of its work.
`building_gradient_jobs`, `building_gradient_published` and
`building_gradient_discarded` count scheduled building walking fields admitted,
installed at their deadline and dropped (superseded or destination gone);
`building_gradient_max_pending` is the deepest queue and `building_gradient_wait_ns`
the owner's time joining jobs at their deadlines; `building_gradient_pending` is the
number still in flight when the run ended. `building_gradient_synchronous` counts
building walking fields built on the owner: cold fields, queue overflow and stuck
units' rebuilds when no refresh can be queued. An area paint or a team-wide reset
keeps a building's walking fields serving and refreshes them on schedule instead
of dropping them. `building_gradient_synchronous_by_reason` splits that count:
`cold_*` by what last dropped the field (`new`, `idle` eviction, the building's
`own` reset, a `team`-wide reset, an `area` paint), then `overflow`, `inactive`
(a map without a game, as in the editor) and `other`.
`setup_ns` ends before `Engine::run`; `run_ns` includes that call's finalization.

The nested `ai_pipeline` object reports session counters:

| Metric | Meaning |
| --- | --- |
| `captures` | Distinct snapshot capture boundaries. |
| `extraction_ns` | Owner elapsed extraction time, excluding the separately measured preparation subset. |
| `preparation_ns` | Owner elapsed time deriving building checks and preparing the growth field. |
| `bytes_copied` | Component payload actually copied across captures: for map arrays only the 16x16 chunks changed since the reused buffer was last filled, plus whole entity, team and published resource-plane payloads. |
| `component_reuses` | Unchanged component or resource-plane reuse events. |
| `allocations` | Instrumented component/payload pool-object, vector-growth and resource-plane allocation events. |
| `computation_ns` | Summed elapsed time executing decision callbacks, across inline or worker execution. |
| `submitted`, `delivered` | Admitted polls and deadline publications, including null outputs. |
| `deadline_misses` | Due batches that were not finished when the owner reached their deadline; the owner then executes or waits for the remainder. |
| `deadline_wait_ns` | Elapsed time the owner spent completing those missed batches, including jobs it ran itself. |
| `maximum_pending` | Largest number of queued decision outputs across all controllers. |
| `shared_batches` | Decision batches handed to the compute workers rather than run by the owner alone. |
| `snapshot_allocated_buffers`, `snapshot_reusable_buffers`, `snapshot_leased_buffers` | Allocated pool buffers, buffers with only their pool reference, and buffers with other references. These count component/plane epochs, not whole-world generations. |
| `snapshot_retained_bytes`, `snapshot_capacity_bytes`, `snapshot_leased_bytes` | Accounted pooled objects plus payload capacity, payload capacity alone, and the subset referenced outside its pool. |
| `snapshot_peak_*` | Session high-water values for the corresponding snapshot buffer/byte counters. |
| `controller_query_vector_bytes`, `controller_query_vector_peak_bytes` | Current sum of published controller query-vector capacities and its session high-water value. |
| `controller_query_vector_samples` | Controllers with an available published capacity sample; unavailable controllers contribute no value. |

Elapsed work across threads is not process CPU time. Preparation and extraction partition
owner capture work; decision and deadline-wait durations can overlap other work, so do not
sum all counters into a total. `bytes_copied` is cumulative traffic, and `allocations` is
not an exhaustive allocator trace. Use process user-plus-system CPU and peak RSS alongside
the explicit metrics. Worker-local implicit scopes are not merged into owner totals.

The asynchronous AI migration requires at least ten paired zero-delay runs against
master, with uncertainty reported for full-match CPU and elapsed-time ratios. Both
regressions must remain within 5%. Separate deadline misses and waits from average
tick time, and compare gameplay at delays 0, 1, 4 and 8 independently of throughput.
The current production delay is eight ticks; retain that value when comparing
thread counts for this configuration cleanup.

Headless benchmark mode also exports `benchmark_tick_histogram`: fixed
logarithmic nanosecond buckets with exclusive upper bounds (the final `null`
bound denotes overflow). It samples complete advancing engine steps after
warmup, including AI deadline waits. Stalled calls do not create tick samples.
Use these separate instrumented runs for tick-time distributions; ordinary
paired timing runs retain the default loop and do not pay histogram sampling.

Snapshot pools have seventeen slots per component/plane: the longest consumer horizon
(sixteen-tick map-gradient jobs; eight-tick AI decisions and building gradients lease the
same captures) plus the
store's latest capture. Slots allocate on
demand and retain reusable capacity. Lease counters include the store's latest snapshot
and references from pooled components, rather than counting only workers. Shared catalog
and terrain payloads are counted once. Byte accounting includes nested vector and growth
capacities, but excludes registry/configuration heaps, string heaps, map nodes, allocator
overhead and shared-pointer control blocks. A consumer holds a pooled buffer through its
plain shared pointer; the pool reuses a buffer once that pointer is the only one left, after
an acquire fence that orders the consumer's reads before the owner's next write. Memory
metrics and their session peaks are computed when telemetry is queried, not on the capture
path. These measurements are not a complete heap census.

Controller capacities are sampled on the controller's decision lane and published with
its output. They include reported retained query vectors, including private resource
initializations and projection scratch, while excluding temporary query allocations,
node heaps and legacy raw-array caches. Cortex placement/water/wheat and Nicowar
defense scratch are controller-owned and included. Maxima's immutable shared
neighborhood geometry is excluded from the private-vector metric; its payload is
approximately 36 bytes per map cell, shared among controllers using the same dimensions.
Availability is explicit. Snapshot and controller
categories can share payloads; do not infer total resident memory by adding them.

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

Verification requires compatible executables expected to produce identical orders and
save bytes. When delay or save format changes, compare worker counts within the same
delay and format separately; cross-version byte equality is not the acceptance test.

The runner retains commands, executable/input hashes, logs, results, per-process
peak resident memory, wall time, and user-plus-system CPU time from `wait4`.
It runs one warm-up and five measured repetitions by default, with rotated/reversed
ordering; use `--repeats 10` for ten paired measurement rounds. Explicit counts
run on both binaries; `auto` runs separately on the candidate. The summary includes
wall and CPU ratios against the base at the same explicit count.
Compare one and several compute threads, automatic sizing,
and the baseline at the same delay. Aggregate ratios do not replace per-scenario CPU and small-map
regression checks. Timing thresholds are deliberately not CI assertions.


### Delayed periodic gradients

All games use the shared compute executor and an eight-tick publication delay by
default. Structured headless runs accept `--gradient-delay D` (1–16 ticks, default
8). `--compute-threads 1` provides owner-only
computation with exactly the same publication schedule, providing the
determinism and timing control for each delay. Different delays may produce
different games. A loaded game's delay cannot change while jobs are pending.

The owner reserves one allocated material, market, guard or clear field at the
completed-tick round-robin boundary. AI and gradients capture the union of their
required immutable components once; each job leases only its own projection. A
shared-executor job seeds and propagates the field in private storage. It never
reads live map arrays, supplier stock or mutable seed caches. Market observations
include material availability after reservations, and guard jobs capture alliances
and, when crowd balancing is enabled, units. Publication stays before the teams
step at the existing fixed deadline. Immediate on-demand and forbidden/building
fields keep their synchronous paths.

The mutable material seed cache remains on synchronous paths. Periodic material
jobs use caches private to each executor thread, updated from immutable snapshot
chunk versions. Templates reuse base fields and material/forbidden bitsets. Fog
and supplier availability are applied from each request's snapshot. Out-of-order
snapshot ticks are supported by comparing exact chunk versions. Cached templates
hold no snapshot leases, and changing worlds or registries invalidates them.
Small maps, over-budget inputs and allocation failures use the direct kernels.
Optional seed cache payloads share a 64 MiB budget across executor slots;
reconfiguration discards them. Guard seeding uses compact terrain lookups.
Include warmed-cache baselines, snapshot capture/copying, peak memory and total CPU
in performance comparisons; moving work off the owner does not itself establish a speedup.

`--compute-threads` sizes the shared executor, including the owner. Periodic and
building jobs always submit to it in normal sessions. With no workers, the owner
computes at the join; publication retains its deadline. `gradient_workers`
reports available shared background threads and `compute_threads` reports total
executor size.
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
maximum pending buffers, deadline wait nanoseconds, and summed seeding-plus-propagation elapsed
nanoseconds. `gradient_preparation_ns` records worker seed time collected at joins;
snapshot capture remains part of the snapshot metrics. The latter is **not CPU time**. Whole-process user+system CPU must be
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
add its inclusive time to propagation time. Live seeding retains its serial order;
private snapshot jobs prepare their buffers on the shared executor.

Resource preparation shares two derived base fields for walking and swimming.
Each request copies a base into its owned buffer and patches resource goals,
current fog and market availability, and the requesting team's forbidden cells.
Positive swim classes share initial passability but retain their existing movement
costs during propagation. Clearing and guard fields keep their separate kernels;
their goal/blocker precedence differs from resource fields.

`GradientRuntime` owns the resource seed cache. Resource types, terrain,
buildings, immobile units and forbidden masks notify it through explicit map
mutation methods. Public tile/resource views are const; reads and amount-only
changes do not invalidate seeds. A deduplicated dirty queue and membership
bitsets avoid rescanning unchanged cells. Resource-goal membership excludes
immobile-occupied cells and refreshes on resource or immobile changes, avoiding
a live occupancy check for each natural goal. Load/reset, bulk terrain reconstruction
and registry replacement discard derived seeds without changing pending jobs or
their publication deadlines. Mutation and invalidation follow the existing
simulation write/read phase barriers; concurrent preparation requests serialize
cache maintenance and copying. Background propagation workers receive owned
buffers and never modify these shared templates.

The cache bypasses maps of at most 4,096 cells. More than one sixty-fourth of a
map in the dirty queue selects direct preparation until 16 consecutive low-change
preparation requests justify rebuilding. These thresholds bound bookkeeping and
avoid repeated rebuilds during short mutation bursts; they affect execution cost
only. Optional storage is capped at 16 bytes per cell and 32 MiB per map, including
vector capacities and fixed cache state. Over-budget maps and allocation failures
use the direct kernel. The cache is neither serialized nor checksummed.

The direct resource kernel specializes market/visibility policy outside the cell
loop. It and the clearing kernel share a per-call terrain-policy lookup for the
built-in registry; custom terrain reads its compiled cell properties. Clearing
preparation also resolves resource clearability once per call and avoids repeated
coordinate conversion. Both paths preserve the original scalar predicates.

`GradientPreparation` in the engine test registry compares seed buffers against
the original scalar predicates for every resource and swim class, terrain types,
team masks, fog buffers, market stock and clearing/farming precedence. It also
checks tracked mutations, cache fallback/rebuild, map resizing, custom terrain
and registry replacement, guard crowding, and concurrent lazy resource-field
requests. Run it with:

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

## Resource optimization campaigns

Keep the archived pre-refactor engine, the approved-behavior-fixes control and the
merged runtime-resource engine as distinct references. Compare optimizations to
the merged engine; older controls help attribute overhead and intended behavior
changes. Retain source revision, compiler, flags and dependency provenance with
each executable rather than inferring them from its filename.

Freeze a manifest of the priority late-game windows and use 16 measured alternating
pairs plus the runner's automatic discarded warmup pair:

```sh
python3 test/benchmark_resource_refactor.py MERGED_ENGINE CANDIDATE_ENGINE PRIORITY_WINDOWS.json \
  --before-root MERGED_CHECKOUT --after-root CANDIDATE_CHECKOUT \
  --repeats 16 --report-only --output artifacts/resource-priority
```

Run the full frozen legacy corpus with eight measured pairs, and include large-map,
eight-team and custom-resource stress windows in separately identified manifests.
The runner requires distinct frozen data roots and refuses to overwrite an output
directory. `--report-only` retains historical threshold diagnostics while removing
their effect on the exit status; execution, integrity and incomplete-window errors
still fail. See [resource campaign metrics and integrity checks](../features/resource-catalogs.md#regression-testing).

Schedule timing exclusively: keep builds, compression, profilers and other owned
game runs outside the campaign. Record CPU affinity and frequency/governor evidence
alongside raw samples. If temporarily stabilizing a governor, record its original
settings first and verify restoration even after interruption. The runner records
per-CPU frequency/governor boundary snapshots outside each timed child run, and
probes `perf` cycles/task-clock permission once outside timing. It retains denied
counter diagnostics and labels interval effective frequency unavailable: neither a
successful permission probe nor a scaling/hardware-average boundary sample measures
the engine's effective frequency over its run. Collect that measurement separately
when supported. Preserve contaminated runs and repeat into new output directories.
Instrumented scopes and instruction counts explain costs but do not replace
end-to-end paired CPU measurements. Report confidence intervals, CPU, wall time and
peak RSS separately; the runner's memory metric includes the entire process.

On Linux, each raw measurement also retains `/proc/stat` snapshots immediately
before and after its child run, outside the timed interval. `host_cpu_activity`
reports total and per-CPU busy seconds, excluding idle/iowait without counting guest
time twice. It subtracts whole-child `wait4` CPU to estimate other busy CPU seconds
and average cores over the snapshot interval. This can reveal activity starting
mid-campaign that load averages hide. It covers all host CPUs, includes kernel,
runner and steal time, and does not identify other processes or prove interference
with the benchmark's CPU affinity. Coarse jiffies and different accounting boundaries
can yield small negative estimates; these remain visible. Missing counters, topology
changes or decreasing busy counters mark the estimate unavailable. These diagnostics
do not change threshold classification or automatically accept/reject a campaign.

After authorization to change the selected CPU policies, use the maintained wrapper
to record original governors, stabilize them, run an unprivileged command and verify
restoration (CPUs 0–7 by default):

```sh
python3 test/run_with_benchmark_governor.py --audit artifacts/governor-priority.json \
  --cpus 0 1 2 3 4 5 6 7 -- taskset -c 0-7 python3 test/benchmark_resource_refactor.py \
  MERGED_ENGINE CANDIDATE_ENGINE PRIORITY_WINDOWS.json \
  --before-root MERGED_CHECKOUT --after-root CANDIDATE_CHECKOUT \
  --repeats 16 --report-only --output artifacts/resource-priority
```

Only individual sysfs writes use noninteractive `sudo tee`; the wrapper refuses to
run as root. Shared policies extending beyond selected CPUs are rejected before any
write. `--dry-run` records the proposed command and original settings without writing
governors or starting the command. Timeout, failure and catchable signals stop the
owned process group and restore every touched policy, including a policy whose write
succeeded but readback failed. Cleanup/restoration failures remain errors even if
the command succeeded. Keep the audit beside the results; SIGKILL, power loss or a
host crash cannot guarantee restoration, so inspect saved originals after an abrupt
termination. The wrapper does not itself pin CPU affinity.

CPU affinity alone does not keep other processes off the selected cores. On Linux
with cgroup v2 and an already-enabled cpuset controller, an authorized temporary
partition can reserve complete physical cores while ordinary work keeps the other
CPUs. Verify the host's SMT topology first; these defaults describe a host whose
CPUs 0–7 have siblings 16–23:

```sh
python3 test/run_with_benchmark_cpuset.py --audit artifacts/cpuset-priority.json \
  --cpus 0-7 --reserve-cpus 0-7,16-23 -- \
  python3 test/run_with_benchmark_governor.py --audit artifacts/governor-priority.json \
  --cpus 0 1 2 3 4 5 6 7 -- \
  python3 test/benchmark_resource_refactor.py MERGED_ENGINE CANDIDATE_ENGINE PRIORITY_WINDOWS.json \
  --before-root MERGED_CHECKOUT --after-root CANDIDATE_CHECKOUT \
  --repeats 16 --report-only --output artifacts/resource-priority
```

The wrapper runs unprivileged, creates one fresh root-level cgroup, and uses
noninteractive `sudo` only for individual operations on that group (and reading
init's namespace identity if permissions require it). It does not write existing
cgroup controls or individual process affinity masks; the kernel temporarily
restricts their effective CPU allocation to the complementary cores. A gate
preserves the caller's UID, GID, groups and environment, confirms placement, and
sets command affinity before execution. `--dry-run` records preflight information
without creating a cgroup or launching the command. Every reserved core must
include all its SMT siblings, and CPUs must remain available outside the partition.

The partition uses `root`, which preserves scheduler load balancing. It verifies
exclusive/effective CPU masks and the complementary ordinary-work mask before
launch and throughout execution, and stops on invalidation or command-affinity
changes. See the kernel's [cpuset partition documentation](https://docs.kernel.org/admin-guide/cgroup-v2.html#cpuset).
Timeout and catchable signals allow graceful shutdown before removing the group;
nested governor wrappers get time to restore their original settings. Last-resort
`cgroup.kill` is restricted to the owned group and makes the run fail with an
explicit warning to inspect the governor audit. Group removal, original parent
state and the wrapper's original affinity are audited separately from governor
restoration. SIGKILL, host failure or power loss can prevent cleanup: retain the
audit containing the exact group path and inspect outstanding processes and nested
governor originals before recovery.

Exclusive CPU allocation does not isolate shared memory bandwidth, package power,
interrupts, kernel activity or shared caches. Other-host CPU activity is expected
outside the reserved set; use the runner's per-CPU busy counters to distinguish
that from residual activity on reserved CPUs, including deliberately idle SMT
siblings. Whole-host busy counts alone do not establish contamination of the
reserved cores. Continue recording CPU, wall time, RSS and frequency evidence and
retain anomalous runs for separate investigation.

### Resource growth pipeline measurements

`test/benchmark_resource_growth.py` accepts the parallel-compute scenario manifest,
with `--baseline`, `--delays 1 3 8`, `--threads 1 2 4 8`, and `--repeats 10`.
Use `--verify` separately for exact per-tick candidate comparisons at each delay.
Headless checksum telemetry also emits `world.checksums`, including every resource
and completed pending proposal; this heavy verification joins private growth work
and is deliberately excluded from timings.
Timing runs include `--benchmark-warmup 0` to expose the existing tick histogram;
whole-process wall/CPU/RSS and engine run time remain distinct intervals. An
unmeasured process warm-up precedes ten rotated paired rounds. Summary ratios and
bootstrap intervals are per scenario; different old/new trajectories are not
behavioral equivalence evidence. The zero-worker control (`--compute-threads 1`)
also changes AI and gradient concurrency, so it does not isolate growth placement.

`ResourceGrowthBenchmark` (opt-in benchmark tag) compares legacy immediate growth,
snapshot compute plus immediate mutation, delayed execution with zero workers,
and delayed shared execution. Fixtures reset outside the timer; ecology is warmed and snapshot
capture stays inside the timer. `GLOB2_GROWTH_BENCHMARK_OUTPUT` selects its JSON
output, and `GLOB2_GROWTH_ECOLOGY_OUTPUT` selects the twenty-seed ecology report.
Ecology runs both reserve-preserving and deposit-depleting harvesting. Its
single-material, one-unit-seed fixture uses stock conservation to distinguish
replenishment, new deposits and removals from the actual harvest.
The legacy component control executes the old algorithm in the candidate binary;
use the retained baseline executable for the old engine's end-to-end cost.

`ResourceGrowthFixtures` creates controlled full-engine starting saves from the
baseline-generated `idle128`, `idle256`, `idle512` and `ai256` initial saves.
Set `GLOB2_GROWTH_FIXTURE_INPUT` to their parent directory and
`GLOB2_GROWTH_FIXTURE_OUTPUT` to an empty output directory. It retains the teams
and buildings, installs uniform crops, and produces sparse, dense, saturated,
low-stock active-AI, blocked-spread, multi-material and disabled-growth cases.
The generator uses pre-pipeline APIs: when comparing different save versions,
compile this test-only source in the baseline test registry so both executables
can load its output. Do not modify the preserved baseline game executable.

Headless JSON `growth_*` metrics report submitted/published batches, samples,
proposals, accepted/rejected operations, capacity clamps, added stocks/tiles,
pending/proposal-buffer high-water marks and computation/queue/wait/publication
nanoseconds. Worker elapsed time is not process CPU. Snapshot capture/copy and
memory costs appear in the shared snapshot metrics. Wait time combines deadline
joins, zero-worker fallback computation and explicit drains; final draining completes computation without applying
future mutations. Report end-to-end regressions even when owner computation falls.
