# Compute pipeline benchmarks

Measure scheduled AI, gradients, growth and scene preparation using retained inputs. Use [telemetry contracts](performance-telemetry.md) to interpret exported fields.

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

The hiring study is separately available through `--telemetry hiring-stats` and
`--telemetry hiring-shadow` ([measurement contract](performance-telemetry.md#cost-and-sampling)).
The full matrix uses an owner-executed control. `--hiring-schedule K:R:L:split|combined`
selects one diagnostic schedule; `--hiring-execution owner|pool` compares the same
planner on the owner or shared executor. The pool uses separate FIFO team lanes,
admits at `t % R == team % R`, and joins/publishes before team stepping at `t + K`.
Old lists remain visible until replacement. Zero delay joins in the capture boundary.
Draining at teardown, reconfiguration or export finishes pending work without early
publication. Hiring remains authoritative and this diagnostic continuation is not
saved match state. `K + R - 1` must stay within eight ticks.

Without `--telemetry hiring-shadow`, a selected schedule measures compute without
reference-opportunity collection. `hiring-compute.json` records publication digest,
worker/owner job counts, waits and bounded output/cache reservations. Its
`planner_metrics` separates setup, geography hashing, prechecks, candidate evaluation
and sorting/output batches; evaluation includes nested route seeding and propagation.
Timers report elapsed nanoseconds, not CPU time, and include startup. Counters report
field hits/misses, cells seeded, evictions, changed geography components, candidate
counts and published/private resource reads. Metrics cover retained/published outputs;
future outputs drained without publication are excluded. Use process CPU and a sampling
profiler alongside these timers, and compare the same planner in owner/pool modes.
These diagnostics do not enter simulation checksums or the publication digest. Snapshot leases
end when each job completes. Its maximum pending batches is `K + 1`; admission at
one boundary shares one batch across its selected teams. The shared executor reserves
nine additional slots and sixteen lanes for this producer.
Shadow output retains three ranked controls per job (combined, observed available,
observed busy) to compare bounded policies; this matrix storage is not a proposed
live output format. Building distance queries lease the engine's existing field generation at admission.
A per-generation recursive mutex protects search expansion and owner reads; replacement
transfers the existing buffer/search to the lease without copying them. Proposal queries
do not update owner refresh-demand history. Missing building fields yield no suggestion;
the planner neither creates nor refreshes them. Leases remain usable after building removal.
`shared_route_owner_wait_ns` and `shared_route_owner_contentions` report owner lock waits;
planner counters report shared queries, expansions, missing fields and worker waits.
`sharedQueryNs` times actual expansion only; cached reads use counters, not inner-loop timers.
Admission accounting includes leased storage at capture and the measured peak storage
of retired generations (`shared_route_peak_retired_bytes`). Expansion of the engine's
current field uses its existing workspace; it is not a second planner cache. This
prototype measures retired workspace growth rather than reserving its full theoretical
worst case. Accounted peaks above 160 MiB stop the diagnostic study.
The owner captures the engine's resolved supplier modes (catalog support and actual
team supplier lists), rather than treating fetch permissions as supplier presence.
Natural fields and non-self-excluding unified market fields reuse the matching
published snapshot plane. Direct-supplier or self-excluding fields, and absent
published planes, use a private snapshot field; capture does not create or refresh
live resource fields. Geography hashing is lazy and runs only for private fields.
Refreshes include only alive buildings whose maintained desired staffing exceeds
captured working subscriptions. Fully staffed buildings produce no lists and lease
no route fields; buildings with zero desired staffing also skip planning. Newly
understaffed buildings enter their team's next scheduled refresh, with the same
fixed publication delay and no population fallback. This changes shortlist
availability: a vacancy arising after capture can wait for the next publication.
Reference hiring can fill such vacancies before a later snapshot ever observes
them, so demand-filtered shadow coverage must not be interpreted as live delay.
Attack strengths are captured only for teams with an active defense recruitment need.
Material jobs are filtered using outstanding deliveries after existing subscriptions,
with the same target/multiplier arithmetic as owner hiring. Attraction roles retain
borrowing alternatives within the building's remaining seats. Team qualification
and cargo indexes are built lazily once per team refresh. Worker-to-building
distances are reused within each building/route refresh, including unreachable
results; per-field lock scopes remain one query. Flying building-distance queries
share one incremental snapshot-backed air field within a building refresh.
An optional pure-planner measurement control for ground fetch jobs with an existing
resource plane uses a bounded heap and the
exact lower bound `2 * resourceDistance` on the trip score. It is disabled by
default because maintaining the heaps can cost more than the queries avoided. A building-distance
query is skipped only when that lower bound cannot enter any requested pool,
including level and identity ties. This preserves the same canonical shortlist
as exhaustive evaluation; it is not approximate distance ranking.
Scheduled shortlists default to remaining job vacancies plus two backups, capped
by `L`. `--hiring-extras 0|2|4` controls backups; `fixed` restores the fixed-L
measurement control. Capacity is total across split pools, with at least one busy
slot when capacity is two or greater. `--hiring-availability available` is a
separate policy control that rejects observed-busy/medical candidates before route
work and uses the entire capacity for its available-only combined list. The default
is `all`; do not interpret this control as a live admission or delay guarantee.
`--hiring-diversity rounds` is an explicit available-only, vacancy-sized control
for single-material teams without attraction roles and with aggregate vacancies
plus backups within `L`. The owner captures its priority order; private fair
rounds choose distinct primary suggestions across buildings, then add backups.
Workers are not reserved or assigned in the live world. A wider private ranking
buffer supports this projection; only vacancy-sized lists are published. Other
job shapes fall back to independent lists and increment `coordinationSkipped`.
This is a policy experiment, not proof of equivalent hires or live delay.
The `hiring-study --prepare opportunities OUTPUT_DIR` harness compares warmed,
static real-engine opportunities with rotated repeated planner controls and a
shared-executor control. Direct fill timings include subscriptions and immediate
swaps; planner timings exclude commitment and snapshot capture. Its separate
K4/R4 waiting model freezes positions, stock and medical availability while
hypothetical hires remain busy. It is not a live gameplay delay measurement.
These options require `--hiring-schedule`. Neither changes normal hiring.
Candidate selection uses bounded partitioning followed by sorting retained prefixes;
the resulting canonical lists equal full-sort prefixes. Scheduled runs build only
the selected pool policy; the full shadow matrix builds both controls.
Resource fields otherwise use a private,
deterministically evicted cache bounded to at most 32 MiB per team and 128 MiB per match. The study rejects an
accounted cache-plus-output working set exceeding 160 MiB.

Build the `hiring-study` target to prepare deterministic sustainable and contention
saves with `HiringStudyHarness --prepare sustainable|contention OUTPUT_DIR`.
The harness checks population throughout its 4,096-tick window.
`HiringStudyHarness --prepare opportunities OUTPUT_DIR` measures isolated static
1200-worker/48-building hiring opportunities at several availability fractions.
Ten rotated repetitions compare a planner refresh with the original complete
fill pass (including subscription/swaps); setup/reset are untimed. Private fair
round overlays report how many seats suggestions can cover before conflicts
exhaust shortlists. This is neither whole-engine timing nor a live economy or
recruitment-delay measurement. Exact job filtering can eliminate all planner work
on reference games that immediately fill deficits before completed-tick capture;
such a no-output run is not useful evidence of live hiring efficiency. Inactive controllers
provide null orders when continuing these fixtures in the full headless engine.
`test/benchmark_hiring.py` accepts a workload JSON array with `name`, `save`, and
absolute `ticks`. Its `verify` mode compares disabled, statistics and shadow modes
against `--baseline` using traces, replay bytes and final saves. Its `shadow` mode
collects reference coverage; `timing` uses one process warmup and ten rotated paired
repetitions by default, without diagnostic exports or saves. Retain its metadata,
commands, input/executable hashes, raw measurements and host observations under
`artifacts/`. Timing results alone do not establish a cached-hiring speedup.

Structured `game run` accepts `--ai-order-delay D` for a new match (default 8
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

For AI scheduling changes, retain paired full-match CPU and elapsed-time samples with uncertainty. Separate deadline misses and waits from average tick time. Compare gameplay at delays 0, 1, 4 and 8 independently of throughput; keep the same delay when comparing worker counts.

Headless benchmark mode also exports `benchmark_tick_histogram`: fixed
logarithmic nanosecond buckets with exclusive upper bounds (the final `null`
bound denotes overflow). It samples complete advancing engine steps after
warmup, including AI deadline waits. Stalled calls do not create tick samples.
Use these separate instrumented runs for tick-time distributions; ordinary
paired timing runs retain the default loop and do not pay histogram sampling.

Snapshot component pools permit 22 buffers. Resource-plane pools retain a 17-epoch bound per plane. These limits account for retained immutable inputs across consumer horizons; buffers allocate on
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
On Linux, a child peak at the runner's inherited memory high-water mark is
rejected as censored; see [RSS measurement limits](resource-benchmarks.md).
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
58. Older saves start with an empty queue. Format 123 introduced scoped invalidation and the corresponding historical replay/protocol gates. Current admission values live in [Version.h](../../src/app/Version.h) and [ReplayReader.h](../../src/replay/ReplayReader.h); version-120 saves remain supported. Platforms without worker
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


See [resource benchmarks](resource-benchmarks.md) for corpus preparation, measurement isolation and growth pipeline comparisons.

## Executor wait diagnostics and tracing

Use `--compute-telemetry waits` to attribute unfinished required batches to AI,
periodic/building gradients, resource growth, hiring, presentation, other or mixed work.
Use `trace` for scheduling events. Collection defaults to off; legacy compute timing
fields are unavailable (`null`) unless the selected mode collects them. Benchmark readers
must check availability rather than treating null as zero. The gradient pipeline benchmark
accepts `--compute-telemetry off|waits|trace`, defaulting to off; timing and tracing variants
must be compared separately. See [the telemetry contract](performance-telemetry.md#optional-compute-executor-diagnostics).

For overhead validation, use release compilation, retained games and paired repetitions
with an instrumentation-free control built with the same compiler and flags. Include empty
batches, tiny and substantial callbacks. Rotate execution order and retain every sample,
wall time, process CPU, memory, tick latency, and lost-event counts. Inspect optimized disabled
worker/callback specializations for diagnostic clocks, stores, allocations, event writes and
mode tests. A successful code inspection or fast-development build does not establish that
disabled overhead is unmeasurable. Report uncertainty and incomplete platform coverage.

Resource-growth benchmark cases accept `GLOB2_COMPUTE_TELEMETRY=off|waits|trace`
(default off). Their JSON records include the selected mode and use null for
uncollected pipeline timing. Direct calculation/publication timing remains measured
by the benchmark's own clock.
