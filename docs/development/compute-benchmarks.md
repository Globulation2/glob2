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

## Turret shortlist research controls

`TurretTargeting` is a pure planner and shadow consumer; the game still uses
`Building::findBestTarget` and its existing firing path. It is not a match rule or
a live firing experiment. The original selector is retained as the independent
correctness oracle. The planner folds that selector's scan order, strict score
comparisons, ground-before-air tile returns, ring priority termination and
conditional building fallback while collecting alternatives in the same traversal.
Entry zero preserves the preferred identity and building aim offset. Alternatives
use bounded capacity selection after entity deduplication; capacity zero is the
uncapped control. The consumer resolves incarnations and checks current team,
damage, occupancy, footprint, range, clearance and arrival eligibility without
scoring or firing bullets.

The immutable observation includes snapshot occupancy, terrain, catalogs, entity
slot indices and teams, plus captured effective unit offense and projectile-blocking
presence. Effective offense includes experience, glass-cannon rules and area effects.
Capture the source's separate unit/building attackability masks at the same boundary;
shared-vision exclusion applies to units only. The research producer is integrated
with `Game::captureReadBoundary`: due turret requirements join AI, map work,
diagnostics and presentation before the shared store captures the boundary.
The producer borrows a projection of that handle; it never captures a second world.
Effective offense and the occupied-cell index are prepared once for the admitted
team jobs at that boundary. The fixture convenience `observe(game, previous)`
announces a new read boundary through the same store; production always passes
the already captured handle to the observation adapter.

An observation lineage shares unchanged occupied-cell chunks using the snapshot's
16×16 occupancy stamps. Chunk records carry team masks and canonical cell positions,
never scores. Retained index allocations across overlapping observations in that
lineage are bounded to 32 MiB. Unretained chunks are reconstructed temporarily from
snapshot occupancy using the same chunk builder and occupied-cell traversal.
Pass the previous observation when refreshing to preserve the shared budget and
chunk reuse. There is one production shortlist enumerator: it assigns original
scan ordinals to occupied cells for stock octants and custom rectangular rings,
including every wrapped representative and repeated stock corner encounter.
Custom footprints, oversized regions and memory pressure do not select a separate
scan-based shortlist implementation. Empty and team-ineligible cells do not require
projectile clearance. The original game selector remains the independent head oracle;
test-only forward geometry checks alternative encounter order.

Build and run the focused correctness and research controls:

```sh
python3 tools/dev_build.py engine-tests
python3 test/run_tests.py --binary engine --build-dir build/linux/client/debug/dev-dev_fast-true-linker-auto --filter 'TurretTargeting/*' --exclude-tag benchmark --verbose
python3 tools/dev_build.py dev_fast=0 release=1 engine-tests
python3 test/run_tests.py --binary engine --build-dir build/linux/client/release/dev-linker-auto --filter 'TurretTargeting/*' --tag benchmark --exclude-tag callgrind --verbose
```

The paired targeting benchmark performs one untimed warmup and ten repetitions,
rotating the original selector, temporary-chunk indexed planner, retained-index
planner and shadow consumer. It emits every batch sample and work counter for 100 armed turrets with
empty ranges and for 100 turrets with stationary combat targets. These are query
cost measurements, not whole-engine speedups or CPU/memory qualification of a
background pipeline. Run with other builds and benchmarks idle; preserve host
activity and raw samples under `artifacts/`.

The `[pipeline-cost]` benchmark measures the integrated shadow producer:

```sh
build/linux/client/release/dev-linker-auto/test/glob2-engine-tests \
  --test-suite=TurretTargeting --test-case='paired shared targeting cycles*' \
  --no-colors=true
```

It uses 100 turrets across eight teams, capacity 8, margin 1, K=R=1, one warmup
and ten paired repetitions with rotated ordering. Each sample contains sixteen
completed-boundary capture/publication/consumption cycles. The baseline and trial
both capture Castor's existing core requirements through the shared store. Turret
requirements join that union, so the difference includes incremental capture,
index construction, source capture, deferred jobs and owner validation. There must
be exactly sixteen captures per sample in both paths. The trial uses the map's
existing executor at 1, 2, 4 and 8 participants, with one stateless job per due team;
there is no test-private pool. Executors and the initial boundary are prepared
outside timing. Each worker owns ordered outputs. Successful comparisons and
output printing happen outside measured batches; timing exports no saves/checksums.

Full cycle wall time includes allocation/cleanup and final job draining, without
publishing the last generation early. Process CPU includes all worker threads.
`capture_submit_ns` includes shared capture, index/offense/source preparation and
submission; `publish_ns` includes deadline joins and publication. Consumption
includes source compatibility validation and the first-valid consumer. The
`index_ns` diagnostic covers index and offense preparation within the capture
phase, so it must not be added a second time. Raw counters include captures,
bytes copied, accounted index and shortlist storage. These static refresh cycles
are not whole-engine battle ticks, and core reader requirements do not simulate
concurrent AI/hiring/gradient CPU load. Retain all samples and host activity;
qualify full-engine performance/interference separately.

`Game::configureTurretTargetingShadow(K,R,L,margin)` explicitly enables the
read-only research producer; normal gameplay does not enable it. It accepts the
initial age-bounded configurations. Admission occurs once per logical completed
boundary, including repeated polling and paused edits. A paused boundary admits
no turret work. Jobs may overlap and are joined/published before team stepping
at their deadlines. Draining computation for saves, pauses or teardown releases
snapshot leases without publishing early. Ordered lists and completed pending
outputs stay private research state; save/load does not enable or resume this
shadow tool. It is not the persistent live match experiment.

The research producer defaults to cooldown-aware admission. At completed boundary
`t`, its serving attempts are `t+K` through `t+K+R-1`; the next cooldown call is
`t` (the counter increments after team stepping). Admit when the count of
unsuccessful cooldown calls is at most `K+R-1`.
A positive cooldown reaching zero still consumes an unsuccessful call. Ammunition
never suppresses admission. Source upgrades and configuration changes still
invalidate incompatible lists on consumption/publication.

Source preparation shares one immutable damage row per interaction row within an
admission, and reserves shortlist output once. This boundary-local table is not a
mutable query cache. Unit offense uses captured performance/experience facts;
active area modifiers are folded into supplemental immutable offense at the same
boundary. Snapshot `Effects` contains render bullet/explosion/death records, not
those modifiers, and optimized targeting does not request it.

The index reuse cache holds only index chunks, not an old world snapshot.
Deferred batches reserve five slots (four-tick horizon plus one admission before
publication). The snapshot buffer horizon remains max(16,8,8,4)+1, within the
existing pool bound. Shortlist storage uses conservative admission reservations
and a 32 MiB cap, separate from the 32 MiB index lineage cap. A failed generation
is joined at its deadline and reported rather than silently dropped.

The native `turret-engine-benchmark` program measures full headless simulation
cycles with scheduled AI orders, order validation/settlement, normal turn checksums,
teams/buildings/units (including normal hiring), periodic/building gradients,
resource growth and completed-boundary capture. It also consumes the tick's GUI
events as the headless engine does. It excludes rendering, transport/pacing and
replay/checksum file exports. Run timing separately from correctness:

```sh
python3 tools/dev_build.py dev_fast=0 release=1 turret-engine-benchmark
build/linux/client/release/dev-linker-auto/test/glob2-turret-engine-benchmark \
  --test-suite=TurretEngine --test-case='whole headless ticks*' --no-colors=true
GLOB2_TEST_ARTIFACTS=artifacts/turret-engine-correctness \
  build/linux/client/release/dev-linker-auto/test/glob2-turret-engine-benchmark \
  --test-suite=TurretEngine --test-case-exclude='*benchmark*' --no-colors=true
```

The dedicated program reuses production objects except for a `TypeSteps` object
compiled with `GLOB2_TURRET_ENGINE_BENCHMARK`. That guard substitutes only target
selection at the original ready-and-armed point. Original mode uses the existing
selector; shadow adds planning/current validation but fires the original target;
cached mode fires the first currently valid published entry through normal bullet,
ammunition and cooldown machinery. The shipping game and ordinary engine-tests
link the original object; neither exposes this replacement or enables a live match
experiment. `test/tests.py` records the dedicated source. This is a native research
program, not a portable live/save/replay feature.

Each participant/workload cell has one untimed three-mode warmup and ten paired
seeds with rotated mode ordering, 256 complete ticks each. Workloads include 100
armed turrets with empty ranges, 100 turrets amid active ground combat, and an
ordinary four-turret control; Numbi and Castor run in every workload. Configure the
shared pool with `Map::configureCompute` and assert its actual `threadCount`.
`configureGradientPipeline` enables gradient scheduling but does not choose the
shared pool size. Total wall/process CPU includes completion/draining of admitted
work at the end of the sample, without early publication. Cheap attempt/found/shot
and surviving-entity counters are exported after timing; correctness additionally
exports per-tick heavy checksums, orders and final saves outside timing runs.

Use `TURRET_VARIANT=baseline|forecast|preparation|combined` to isolate the research
optimizations and `TURRET_CAPACITY=4|8` to compare output lengths. The baseline
retains the previous admission/preparation behavior, using the same indexed kernel.
All configurations retain original targeting as their paired comparison. Timing
includes initial boundary admission and final draining, and omits CPU probes.

Run the same timing case separately with `TURRET_PROFILE_CPU=1`. It reports
per-thread CPU for original scans, shared boundary capture/requirements (minus
admission), source/fact preparation, index maintenance, all planning jobs,
publication excluding joins, and cached consumption including list validation.
Worker planning CPU is summed after joins, including jobs executed by the owner;
publication excludes owner join CPU to avoid counting those jobs twice. Shared
capture is a union cost: report its paired increase over original, rather than
charging all common capture to targeting. Probe calls have overhead, so these
breakdowns must not replace uninstrumented whole-engine timing/process CPU.
Thread CPU probes require a platform thread CPU clock and fail explicitly if it
is unavailable.

The separate `reference trajectory measures cached opportunities` case measures
agreement and first-valid delay on the original trajectory with the actual producer,
including cooldown filtering. It reports missed and still-open windows separately;
misses are failures, not removed from a passing delay claim. This does not establish
combat outcomes on the cached trajectory. For the more contested existing stock
Numbi/Castor corpus, run `Numbi Castor stock combat*` in `glob2-engine-tests` with
`TURRET_PIPELINE_ONLY=1` and the same variant/capacity variables. This omits the
serial parameter matrix and records actual-producer agreement, full delay samples,
missed/censored windows, reference/shadow saves, orders and checksums for twenty
matched battles. These checks are diagnostic and must not run during timing.

Original/shadow trajectories must match across participants. Cached firing must
be deterministic across participants on its own trajectory; combat outcomes may
differ from the original, so report shots and surviving units with performance.
Require at least 10% of reference eligible searches to find targets in the combat
fixture, and report any unexpected shots in the nominal empty fixture. Compare
paired per-tick costs and process CPU at the same participant count, retain all
outliers/host activity, and evaluate ordinary-control and sustained-combat gates
as well as the empty-fixture improvement. These fixtures do not establish a
representative distribution of played matches or interference with unmerged hiring
experiments. Save continuation probes report whether the research cache's cold
restart matches the uninterrupted trajectory; passing selected cuts cannot replace
serialization of published/pending lists and admissions for a live trial.

Correctness cases compare preferred coordinates, priority, score, arrival facts
and complete list order across original, live, retained and temporary-chunk adapters.
They cover simultaneous ground/air occupancy, movement rejection, wrapping,
custom footprints, projectile blockers, building offsets, immutable combat facts,
occupancy invalidation, bounded-capacity prefixes and cooldown boundaries. Numbi/Castor continuations compare
AI order bytes, per-tick checksums and complete final save bytes with and without
shadow queries. Fixture-created finished buildings must be registered in their
static ability lists so turrets and swarms actually step. The tests retain both
saves and the order/checksum CSVs in the test artifact
directory. This does not replace full replay-file or supported-platform
comparison before a live simulation change.

The serial shadow matrix includes K and R in {1,2,4}, L in {4,8,16,32}, margins in
{0,1,2}, and a separate uncapped control. Its birth stress fixture gives a
new fast airborne incarnation one physically valid firing attempt above a custom
1×1 turret. Zero-tick arrival is legal in the original routine; the next movement
progress update makes tile residence truncate to zero. Births, movement progress
and occupancy are prescribed fixture inputs, not a complete battle simulation.
The original turret routine fires with its normal ammunition and cooldown; controlled
opportunities are spaced 64 ticks apart and ammunition is replenished between them.
The same-observation control must hit that opportunity. Delayed lists cannot contain
a newly created incarnation soon enough: every positive-delay configuration misses
that window, including the uncapped control. Missing windows are recorded as
responsiveness failures, never omitted from added-delay statistics. This is a
controlled failure case, not an estimate of its frequency in ordinary battles.

The stock combat matrix runs Numbi and Castor over ten seeds in skirmish and
fortified-outpost layouts, with ordinary ground and air units. Original shots
remain authoritative. A thread-local scoped research observer reads the world at
the actual turret step, after ammunition conversion and before cooldown. With no
observer installed, the original routine computes and fires its own target as
usual. Each shadow run must preserve every AI order, tick checksum and final save
against its paired continuation with the observer disabled.

The first 256 ticks define the active combat workload; the fortified layouts must
find targets in at least 10% of eligible searches. The remaining ticks through
1024 retain the quieter continuation, rather than discarding it from the CSV.
Each original shot starts a shadow first-shot opportunity. Delayed consumption
continues on subsequent turret steps, independently of the original shot's ensuing
cooldown, until a cached entry passes or the original target's current shootable
window ends. This measures availability on the reference trajectory; original
bullets can change that trajectory before a hypothetical cached shot. A different
valid target satisfies first-shot availability but fails target/aim agreement.
Missed windows are failures in the p95 rank. Source changes supersede stored lists;
source death closes an outstanding opportunity. Source configuration changes and
end-of-run outstanding windows are reported as censored and cannot silently count
as successes. Raw successful delays, misses, censored counts, representation,
entries examined and rejections accompany every configuration.

Separate stock birth and approaching-warrior controls cover eight admission
phases with normal simulated movement. An approaching unit which never offers an
original shot is recorded as `no-reference`; it is not a missed reference window.
An initial window which closes before cached consumption is a failure even when
the cache later finds another firing opportunity. Use these controls to reject
individual configurations: the list-age bound alone cannot guarantee shot delay
because current movement/arrival eligibility can interrupt firing windows.
Keep the custom one-tick birth stress results separate from stock cohorts; neither
cohort establishes the other's frequency in actual matches.

The executor correctness control submits one independent job per team, traversing
sources in identity order, over four overlapping generations. It compares every
ordered output with serial planning at participants 1, 2, 4 and 8, reversing team
submission and generation join order and perturbing scheduling with thread yields.
Workers receive immutable observations and copied source facts, with private output
vectors. Changing live HP after capture must not change their outputs. The isolated control remains useful for completion-order perturbation. Additional
integration cases verify shared component identities, single capture per boundary,
admission phases, pause/repeated-boundary behavior, source invalidation and
publication deadlines. Numbi/Castor shadow continuations also exercise the
integrated producer. These do not establish interference/performance with hiring,
AI and gradients.

The live match experiment, persistence/version gates and whole-engine/platform
qualification still require the later milestones. Report responsiveness by cohort
and configuration, and retain adverse cases. Query-cost improvements and shadow
availability do not establish live combat outcomes or complete engineering gates.
Do not add owner rescoring or a fallback spatial search, or widen the four-tick
budget to make a failed configuration pass.

The opt-in `[callgrind]` benchmark brackets only planning queries with Callgrind
client requests. Capture, chunk maintenance, fixture setup, warmup, assertions and
output are outside the collected regions. It emits four named dumps: empty range,
stationary combat, dense custom/wrapped geometry and temporary-chunk reconstruction.
The last case measures the same indexed algorithm with retention unavailable.
Valgrind headers are optional test-only development inputs; builds without them
compile the client requests as no-ops. No runtime dependency is added to the game.
Run a native release engine-test binary with function symbols available:

```sh
valgrind --tool=callgrind --instr-atstart=no --collect-atstart=no \
  --callgrind-out-file=artifacts/turret-targeting/callgrind.out \
  build/linux/client/release/dev-linker-auto/test/glob2-engine-tests \
  --test-suite=TurretTargeting --test-case='indexed planning callgrind regions*' \
  --no-colors=true
callgrind_annotate --inclusive=yes --threshold=95 artifacts/turret-targeting/callgrind.out.1
```

Keep pre/post-optimization dumps, build provenance and source snapshots under
`artifacts/`. Compare identical fixtures and query counts using collected instruction
counts; Callgrind elapsed times are instrumentation overhead, not native timing.
Use the separate paired release benchmark to assess native query costs, recording
host activity and preserving all ten samples. Neither profile qualifies whole-engine
speed, producer interference or process CPU on its own.

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
