# Gradient benchmarks

Measure individual field kernels and integrated game behavior separately; kernel speed alone does not establish an engine improvement.

## Whole-process GPU offload qualification

Use release binaries and retained identical saves/orders to measure CPU consumed
per fixed simulation tick. Include every process thread: worker elapsed time
includes device waits and is not a substitute for process CPU. Separate startup,
fresh-storage dispatch and warmed gameplay. Run builds and timed experiments
under one exclusive resource lock, selecting one GPU explicitly.

`test/benchmark_gpu_offload.py` consumes a frozen JSON manifest with binary hashes,
source revisions, build flags, environment overrides and hashed loaded-game
fixtures. It retains every sample and failure, rotates paired execution order,
and uses five screening rounds or ten confirmation rounds:

```sh
python3 test/benchmark_gpu_offload.py artifacts/offload/config.json \
  --output artifacts/offload/screen --lock artifacts/offload/resources.lock
python3 test/benchmark_gpu_offload.py artifacts/offload/confirmation.json \
  --stage confirm --output artifacts/offload/confirmation \
  --lock artifacts/offload/resources.lock
```

Freeze the confirmation manifest before measurement. Use independent games and
sealed kernel holdouts for final acceptance; do not tune against their results.
The runner's per-scenario intervals are screening evidence. Final acceptance also
requires an aggregate analysis that weights workload strata equally and clusters
phase windows by game, normal rendering/frame tails, per-tick checksums,
save/replay continuation, failure injection and removal ablations. A kernel win or
a headless-only pass does not establish an integrated benefit.

The selectors remain `GLOB2_GRADIENT_BACKEND=cpu|opencl|automatic`. Forced OpenCL
experiments can select `GLOB2_GRADIENT_PLAN=cpu|jacobi4|colored2|colored4|colored8|frozen8|frozen16`
(default `frozen8`), `GLOB2_OPENCL_DEVICE=N` (zero-based GPU ordinal), and
`GLOB2_OPENCL_CHECK_INTERVAL=1..32` (default 8). Retain these overrides with each
sample; a requested backend does not prove that a field actually executed there.
Check execution counters over the measured warm window and distinguish fallback
from completed accelerator work. Tracked accelerator host/device payload limits
are 64/128 MiB; driver memory must be measured separately through RSS/device tools.
`GLOB2_OPENCL_POLL_US=1..1000` selects the sleep between required transfer-event
completion queries. Zero (the default) uses a 100 microsecond safety wait;
required transfers remain nonblocking and cancellation is checked between queries.
Retain the requested setting when measuring CPU and publication latency.
`GLOB2_OPENCL_PROFILE=1` collects device upload/kernel/check/readback event time,
separate from host waiting time; use it for diagnosis rather than comparing
instrumented times to ordinary release samples.
`GLOB2_GRADIENT_WORKER_NOOP=1` screens finishing already-fixed seeds on their
preparation worker through the existing validated callback, avoiding an owned
GPU request and coordinator wakeup. It is off by default. Retain its CPU scan
cost in process measurements, and distinguish `cpu_reason_trivial` from actual
device execution; bypassed fields are never counted as GPU completions.
`GLOB2_OPENCL_ACTIVE_EPOCH=1` replaces per-dispatch tile-mask clears with
epoch stamps; `GLOB2_OPENCL_PARITY_BOUND=1` retains opposite ping-pong bindings
on two private kernel handles. Screen each separately before combining. Both
default to zero and retain inactive buffer copies and original-seed recovery.
Live capture remains a separate tuning override. The isolated epoch yielding
implementation uses the same initialized program/check windows; parity still
declines optional probes. Required-only measurements do not qualify live
promotion. Check `active_epoch`, `tile_mask_clears`, `parity_bound`, and
`kernel_argument_updates` in the warm-window OpenCL counters.

`GLOB2_BENCHMARK_DIAGNOSTICS=1` enables optional thread inventories, per-tick
publication waits, and inclusive/self CPU scope accounting for `game run`
benchmark windows. Use it with `GLOB2_GRADIENT_DIAGNOSTICS=1` and
`GLOB2_GRADIENT_ACCOUNTING=1` for attribution. Diagnostic windows include
instrumentation overhead and are rejected as acceptance evidence; CPU scopes
overlap and must not be summed. Thread names do not identify driver roles.

Development fixtures can construct 1024² games through the `GpuOffloadFixture`
engine harness. `GLOB2_BENCHMARK_LARGE_MAPS=1` enables a bounded, synchronous
headless saved-game import scope for these fixtures. Ordinary loader, generator,
lobby and network dimension limits remain unchanged. The scope supports at
most exponent ten in each dimension by default. A separate explicit
`GLOB2_BENCHMARK_2048_MAPS=1` admits exponent eleven only for saved-game imports.
Both 2048 imports and the separately gated
`GLOB2_GPU_OFFLOAD_2048_FIXTURES=1` bounded Numbi/Castor writer require
`GLOB2_BENCHMARK_FIXTURE_MEMORY_MIB` of at least 8192 and a supported Linux
available-memory reading with 2048 MiB additional headroom before construction.
This is a conservative development preflight, not a hard allocation bound or
memory reservation; the evidence owner must also serialize heavy work and check
any container limits. `GLOB2_GPU_OFFLOAD_POPULATION_STRESS=1` adds a separately
named 1024 fixture with 1024 initial units while retaining existing files.
Fixtures use existing bounded orders; their coverage does not repair the original
mixed-AI 1024 continuation's unsupported wide-brush orders.

Automatic selection requires valid, monotonic preparation and coordinator thread CPU clocks.
Unavailable or reversed measurements contribute zero learning credits and demote
the affected accepted plan; a committed device result still publishes exactly once.
The coordinator disables automatic device execution until reconfiguration after
such a measurement failure. Forced OpenCL remains available for diagnosis.
Use `cpu_reason_clock_unavailable`, `fallback_reason_cpu_clock`,
`cpu_clock_invalid_measurements` and `automatic_cpu_clock_unavailable` to identify
this conservative fallback.

## Terrain gradient benchmarks

`TerrainHazardBenchmark` provides opt-in CPU and wall-time measurements for idle
routing at fixed origins and full shared-field propagation. It covers safe ground,
nearby safety, broad ice patches, unreachable safety, and many custom damage rates.
It prints CSV rows with per-call nanoseconds. `idle-cold` measures the first query
after a terrain invalidation; `idle` measures repeated queries with warm caches.
Adaptive batches exclude setup and have no timing assertions. Run the same harness against both revisions with the
same compiler, flags, inputs and CPU affinity. Fixed-origin retries intentionally
measure a worst case; successful units move on in real games.

```sh
build/linux/client/release/test/glob2-engine-tests -ts=TerrainHazardBenchmark \
  '-tc=*idle decisions*,*shared terrain fields*'
```

Its separate `write mature game fixtures` case creates control, sparse-ice and
patchwork-ice saves. Set `GLOB2_HAZARD_BENCH_SAVE` to a mature save (the default is
`games/cross-replay.game`) and `GLOB2_TEST_ARTIFACTS` to the output directory. Produce
fixtures with the older build so both readers accept exactly the same bytes. Use
`game run --benchmark-warmup` to exclude loading and initial cache rebuilding
from whole-engine CPU time per tick. Alternate revision order across repeats and
report distributions; changed routes also change the later simulation workload.

Engine movement profiles come from the map's cell rules: each `CellRuleTable`
entry (`src/map/CellRules.h`) carries its entry costs per swim class, and
`Map::frozenTerrainMovementSnapshot` compacts the profiles its cells use, in cell
order, into `PreparedTerrainCosts` (`src/field/PreparedTerrainCosts.h`). Rules with
the same cardinal and diagonal entry costs share a cost class; equal edge costs
share queue destinations, including cardinal/diagonal aliases. Eager propagation can
select a one-class kernel only after checking every non-forbidden cell, including
source cells. Lazy searches retain the profile snapshot of their captured swimming
class. A swim class supports at most 256 distinct cost profiles; mixed cells take
their speed and health from different corners, so an imported registry whose
combinations exceed that limit is rejected when a cell first needs the rule. Each search or worker owns its mutable queue;
prepared profiles contain no search state and introduce no serialized cache.

Strategic AI travel fields in `src/field/TerrainTravel.h` use a separate bounded
integer queue. Their historical metric charges all eight neighbors the same
terrain entry cost, then rounds the completed wide distances to tile units. Do
not substitute the engine's cardinal/diagonal metric or round intermediate costs.

`tools/gradient_benchmark.py` builds an opt-in standalone, paired benchmark; it
needs a C++20 compiler but no SDL or game build. Capture the pre-optimization
source when comparing against the original terrain kernel:

```sh
mkdir -p artifacts/gradient-baseline
# This historical revision is the reference accepted for this optimization.
git archive 3266c8e51 src/field src/map/TerrainProperties.h src/map/TerrainType.h | \
  tar -x -C artifacts/gradient-baseline
python3 tools/gradient_benchmark.py \
  --baseline-dir artifacts/gradient-baseline/src \
  --output artifacts/gradient-bench --suite representative --repeats 11
```

The runner copies candidate headers (the field kernels and the header-only
terrain and resource tables they include) and harness source before compiling, records
compiler/flags and SHA-256 hashes, and writes raw JSONL samples plus per-case
median comparisons. `--cpu N` pins the subprocess on Linux. `--scalar` forces the
scalar implementation; otherwise the compiler target selects SSE2 or NEON.
`--suite full` adds 64² and 256² cases; `--suite smoke` reduces the main timing
matrix to 32² while retaining the correctness corner cases. The baseline adapter
is specific to the historical revision above and rejects changed source anchors
rather than silently omitting counter hooks. Use a fresh output directory for each
comparison to retain its raw evidence.

`--case '{"size":128,"pattern":"network","swim":3,"mode":"terrain"}'` selects
one custom case; repeat the option for a custom matrix. Optional keys are `width`,
`height`, `registry`, `costs`, `seeds`, `travel` and `cap`. The runner owns both
allocation layouts and the repetition count; cases cannot override them.

The benchmark retains the `road` pattern key for historical comparisons; it
uses the current Trail terrain identity with the same movement cost.

Cases cover classic terrain, uniform Trail/ice, sparse/connected trails, mixed
terrain and enclosed modifiers; all seven swimming profiles; dense/deferred
seeds and capped propagation; thin and rectangular tori; and synthetic registries
of 32 and 64 identities with equivalent or distinct movement costs. The real
registry (`TERRAIN_COUNT` built-in types, read from `src/map/TerrainType.h`) is
measured separately. Each cell takes one terrain identity directly, as a cell
whose four corners agree would; corner-mixed rules are outside this harness. Synthetic registries call the generic prepared
profile API; they do not add game terrain definitions. `--bucket-count 256`
is an isolated future-cost experiment that changes only copied headers.

The original general bucket function is adapted only to accept the registry
extent and a distinct name. It shares queue storage types and field constants
with the candidate, so these timings isolate relaxation changes; compare full
baseline/candidate game binaries when changing those shared components.
Independent heap oracles check engine fields and strategic distances outside the
timed region.

| Mode | What it measures |
| --- | --- |
| `terrain` | Both general engine kernels, including prepared cost classes and eager uniform-cost selection. |
| `dispatch` | Production dispatch for the real registry, including the classic fast path. |
| `plane` | General propagation through a precomputed cost-class plane; construction is reported separately. |
| `strategic` | AI travel fields against the original heap implementation. Report these separately from engine gradients. |

Travel modes 1, 2 and 3 mean walking, amphibious and flying. Production dispatch
and strategic travel use the real terrain costs, not synthetic distinct costs.
Strategic fields do not have an engine propagation cap or deferred seed costs.

Samples alternate implementations in one process, using both shared and separate
output/workspace allocations. Repetition -1 measures fresh queue storage; warm
samples retain capacity. Initialization, profile preparation, class-plane
preparation and snapshot copying are reported separately from propagation.
Preparation/snapshot timings are illustrative single constructions, not stable
microsecond-level comparisons. AI propagation includes its internal allocations,
wide-distance initialization and final rounding. This harness does not reproduce
Map seeding, worker publication or production lazy-search scheduling; validate
those with the integration harnesses and whole-game traces.

Use a second `--instrumented` run for popped/stale entries, successful relaxations,
occupied layers, reservation calls and allocation counts. Its allocator and
counter hooks change timing: never use instrumented times for speed claims.
Memory output separates caller input/output, prepared profile/plane, workspace
object, retained queue capacity, AI-local queue/cost-table objects, and the maximum
additional live heap bytes during each call. Compiler stack frames and register
spills are not measured. Cold separate-workspace samples show each algorithm's own
capacity; shared warm samples inherit capacity from both implementations. Global
allocator accounting covers ordinary `new`/`new[]` allocations used by these
kernels, not process RSS or unrelated engine memory. Zero counters in the
uninstrumented build mean unmeasured, not zero work. Keep timing assertions out of
routine CI; attach raw measurements and simulation checksums to the PR. The
runner's adapter and sampling contracts can be checked without a compiler:

```sh
python3 tools/test_gradient_benchmark.py
```

Before accepting an optimization, include preparation and allocation costs in the
comparison, inspect individual scenarios as well as aggregates, and validate
whole-game behavior with identical initial states and orders. Compare every tick
across serial and parallel workers, including save/load continuation. A standalone
kernel gain is not sufficient evidence of an integrated game improvement.

The production resumable-search benchmark is separately opt-in after building
unit tests:

```sh
python3 test/run_tests.py --binary unit --no-display \
  --filter 'production lazy gradient phases*' --tag benchmark --verbose
```

It exercises nearby, distant and unreachable requests across classic, connected
road, dense mixed, uniform road and uniform ice maps at 32², 128² and 512² for all
swimming profiles. CSV layout values 0–4 follow that order; query values 0–2 mean
nearby, distant and unreachable. Rows separate initial snapshot construction,
search initialization and resolution. The same initial snapshot timing is repeated
for each row of its map and must not be summed as per-query work. Repeat zero
starts with cold queues and later repeats retain search capacity.
Every requested result is checked against the independent heap oracle. Run this
on both revisions with matching inputs and compare it separately from full-field
propagation; ordinary test runs exclude the benchmark tag.

## OpenCL calling-thread CPU diagnostic

`GLOB2_OPENCL_API_CPU=0` is the default. It adds no diagnostic clock reads or
per-API allocations. The predictable disabled scope gate is present in this
candidate, so compare the mode-zero binary with the preceding clean binary
before interpreting small changes.

Mode `1` records guarded thread CPU intervals on the background required lane.
The fixed categories are preparation, upload, arguments, fill, kernel enqueue,
convergence read, output read, output copy and other. Preparation includes
allocation/cache/seed work, subtracting instrumented nested calls. Upload/read
categories measure the actual OpenCL calls; a blocking call includes its calling
thread CPU while waiting. Polling/flush/profiling calls and unclassified command
bookkeeping belong to other. Output copy includes transactional commit bookkeeping.
Other also contains lane queue/kernel setup and uninstrumented command release.
No device events or additional driver commands are introduced.

Mode `2` makes the same number of clock reads at the same scope entry points,
closing an empty bracket before the actual work. It reports `controlBracketNs`;
category CPU and `coveredNs` remain zero. Its call counts match mode one for the
same workload. Work remains included in the existing backend/coordinator CPU
totals. Empty brackets estimate clock/scope perturbation, not a correction that
may be subtracted to assert a speedup. Compare complete modes zero, one and two.

`coveredNs` is the valid inclusive time of lane batch scopes; exclusive category
CPU sums to it only when no scope is invalid and no reconciliation fails. The
existing backend total additionally covers wrapper eligibility/lane setup outside
these scopes. Zero/reversed endpoints invalidate their enclosing scopes; nested
time exceeding an enclosing interval or counter overflow increments reconciliation
errors. Partial valid category readings remain diagnostic and must not be called
complete. Scope calls include host phase scopes as well as instrumented APIs;
the upload/arguments/fill/kernel/read call counts each correspond to actual APIs.

Initialization latches the requested mode and marks `apiCpuConfigured` only after
successful setup. Status exports are scalar snapshots and invoke no driver calls.
Counters are calling-thread CPU, not process/driver CPU attribution. They cannot
qualify automatic promotion. Optional yielding probes decline modes one and two;
offline batch profiles must decline them until a matching configuration is bound
and independently measured. No production plan or algorithm changes accompany
this diagnostic.

Hardware-free tests cover nested conservation, unavailable/reversed clocks,
reconciliation failures, equal empty-bracket read counts and zero disabled reads.
Evidence owns all builds and real device measurements. Required protocol gates
still include exact arrays, saved/replay outputs, actual command counts and complete
process CPU/tick-tail controls; diagnostic stage times alone are not acceptance.
## Single-work-group frontier development experiment

The isolated `tools/gradient_qualification/compare_persistent.py` candidate uses
one work-group per complete field, with uniform global/local barriers between
compact frontier epochs. Each destination enters the following N-entry list at
most once per epoch. Original zeros stay blocked, nonmaximum sources survive,
and cap/toroidal semantics use the same monotone maximum fixed point as the
independent oracle. This avoids assumptions about device-wide publication from
legacy relaxed atomics; see the [Khronos OpenCL C memory model](https://registry.khronos.org/OpenCL/specs/unified/html/OpenCL_C.html).

The kernel initializes immutable ushort seeds into private uint state, then
performs at most the configured pops and 65536 epochs without stamp wrap. An
entire epoch is rejected before work if its frontier exceeds the remaining pop
budget. One successful metadata read proves device completion; only converged
fields read/convert staged output. Bounded exits recover from the original seeds
through the paid strongest compatible production CPU. Imported caps/costs beyond
that CPU contract use the separately labeled independent oracle. API failures
reap the queue before stack metadata, borrowed output or device storage expires.
There is no promise of asynchronous host cancellation during a resident kernel:
finite work and event completion protect ownership, and measured deadlines are
still required. Neither work limits nor barriers guarantee a wall-time bound.

The runner requires the shared campaign lock before compilers or device setup,
freezes source/commit/binary hashes, configuration and compiler logs, and uses
only development layouts and independent edge cases. It records one actual
kernel submission, one metadata check, pop/round/update counts, queue high-water,
output transfers and exact CPU recoveries. Device payload includes the original
upload (22N+32 bytes); host payload and comparison GPU buffers are bounded and
reported separately from CPU workspace and driver/allocator overhead. Interval
process CPU remains diagnostic, and production dispatch/qualification and final
holdouts stay unchanged.

Screen correctness first with `--edges-only`, then repeat with `--pop-limit 1`
to prove exact bounded recovery. Compare local sizes 64/128/256 only when the
compiled kernel supports them, and pop budgets 64K/256K/1M. Small development
screens give no eligibility evidence for 512-square maps: include `--max-size
512` before any target CPU-benefit claim. Single-group underutilization can
outweigh fewer host commands, particularly for singleton fields; prune the
candidate if target-size completion and publication slack fail.

The persistent prototype has a development-process-only fatal safeguard: if an
OpenCL error is followed by a failed emergency `clFinish`, it writes an explicit
stderr marker and best-effort `fatal-native.json`, then terminates without stack
unwinding with exit code 86. This prevents borrowed stack/output storage from
expiring while driver completion is unknown. It is not production recovery;
retain the external process return code, freeze and partial result files and
reject the entire run. Normal successful execution adds no safety `Finish`.

Before real-device persistent screens, evidence can run the independent
`check_persistent_failure.py --lock <shared-lock> --output <fresh-directory>`
controlled subprocess test. It compiles the actual native helper against OpenCL
stubs, loads no driver, and injects metadata-read failure. Successful drain
writes borrowed native stack metadata while it is alive, returns the API error,
and preserves seeds/output sentinels; a destructor marker proves ordinary
unwinding works. Failed drain checks those same sentinels, requires exit code 86,
exact fatal receipt/stderr markers, and absence of the destructor marker. The
shared lock precedes compilation, source/binary hashes and all subprocess
outputs/return codes are retained, and a completion receipt requires both cases.

### Optional CPU reference clock guard

The optional resumable production CPU reference requires nonzero, nondecreasing
CPU clock readings across setup, every checkpoint and successive advances.
Zero, reversal or an exception permanently invalidates its measurement, stops
optional work and releases its retained leases. `cpuNs` then records only a
known lower bound; `requiresFullReservedCharge` requires the caller to settle
the full reserved optional CPU credit. Setup callers can request a declined
metrics receipt. A failed or unmeasured setup also requires conservative charge
from the surrounding capture reservation. Recovery never reruns required CPU
work and never mutates captured original seeds.
Create and advance the reference on the same background producer: native thread
CPU clock values cannot be compared across different workers.

### Isolated accepted CPU capture primitive

`GradientCpuCapture` is an unattached worker-side interface; required scheduling
and automatic learning do not call it. After normal seeding, admission reserves
the policy's sampled global opportunity and CPU credits before cloning. The
shared 16 MiB probe subset of the 64 MiB host budget reserves both original and
golden ushort arrays, exact classic cost input, retained policy/clock payload,
holder and control storage before the first allocation. Fixed arrays avoid
vector-capacity guesses; tracked payload/control allowances do not claim driver
or allocator RSS. Caller input is borrowed only during copying.

The first implementation accepts only the actual classic CPU path. Unweighted
classes need no cost scan or water lookup; weighted classes clone the supplied
byte water plane unchanged. Modified terrain and CellRule indirection decline.
Those future views need bounded deep copies of the used rule and movement
metadata. Existing terrain capacity estimates omit transitive object graphs and
must not be passed as proof of complete retained ownership.

Copying uses 16 KiB blocks with guarded clocks, measured 500 us block limits and
a separate capture CPU limit within the full reservation. Allocation, driver,
callback and block overshoots remain measured declines, not hard latency
guarantees. Invalid clocks stop optional copying permanently and require full
reserved charge. Rejected admission returns an optional-work receipt too; future
attachment must record/exclude this cost rather than count it as accepted work.
Receipts taken before final record destruction exclude final cleanup. Policy
metrics include cleanup and cancellation; future outer accounting must also
cover validation and rejected attempts outside the returned record.

The required job runs exactly once between the original and golden clones. Its
reference is a guarded propagation-only CPU span, excluding preparation and
capture. Clone gold before publication or reuse of the job's mutable array;
`complete` then binds that reference in policy-owned state. Common preparation
is added once to both alternatives. Future accepted-job credits must sum the
disjoint required spans, not a wall-to-wall CPU bracket containing capture.

The immutable record holds storage and the global admission slot until its last
alias is released. An explicit one-time worker-to-background producer handoff
starts a separate thread CPU clock namespace; implicit migration is invalid.
Cleanup is measured and reaps admission after releasing captured storage. This
primitive only authorizes metadata and cancels on disposal; no GPU dispatch or
promotion is attached. Any future device adapter must retain aliases through
terminal event reaping and include private cost preparation, driver work and all
cleanup in conservative accounting. Device/compiler initialization must already
be qualified and prepared under its own tested accounting contract.

### Classic CPU empty-layer ablation

`GLOB2_GRADIENT_SKIP_EMPTY_CLASSIC=0/1` is latched per process and defaults to
zero; other nonempty values are rejected. It selects the eager classic CPU
specialization once per field. Mode one
skips `expandBucket` when its source bucket is empty; it retains every cost-layer
iteration, deferred-seed insertion and cap rule. Building resumptions, modified
terrain kernels and optional chunked CPU references retain their existing paths.
There is no added per-cell test. This is an unqualified component candidate.

Run fresh processes with each value against the same immutable development
corpus using `tools/gradient_qualification/compare_compact.py`, the shared
resource lock and separate output directories. Its freeze and CPU rows report
`cpu_skip_empty_classic`; only `classic_land` and `classic_water` rows activate
the shortcut. Retain exactness receipts and release compiler/library hashes.
Compare mode zero against the pre-candidate binary as an overhead control.
The compiler may already sink empty-loop setup, so source structure does not
establish a speedup. A paired CPU component result precedes any full-game trial;
modified-only or shortcut-only corpora cannot establish activation or benefit.

## Yielding epoch probe development gate

The optional epoch counterpart uses the same initialized `ACTIVE_EPOCH=1`
program, a private cloned kernel, fresh mask stamps A=1/B=0, and unchanged
inactive-tile pingpong copies. Each advance submits at most one kernel and yields
to the required-work coordinator. Its convergence reads match the required
check window, including final padded dispatches; initialization arguments bind
once and the masks never clear between dispatches. Epochs stay bounded through
the 65536-dispatch limit. Cancellation retains uploads/readbacks and immutable
original inputs until their trailing event has completed.

Inside a healthy epoch window, successive broker turns replace the preceding
kernel event without polling it. The in-order queue makes the new event cover
every preceding dispatch. At most the configured 1..32 kernels can be queued;
required priority and cancellation are checked on every turn. Upload, check and
readback boundaries still require positive completion proof. Ambiguous enqueue
or cancellation cannot use an older event to release storage. Device profiling
retains per-kernel waits so its existing individual-event measurements remain
valid. `queuedWindowDispatches` records actual poll bypasses, rather than just
requested configuration.

Evidence should run the `isolated epoch probes*` actual-device case in three
fresh processes, with `GLOB2_TEST_OPENCL_EPOCH_PROBE=1`,
`GLOB2_OPENCL_ACTIVE_EPOCH=1`, `GLOB2_OPENCL_CHECK_INTERVAL` set to 1, 8 and 32,
and profiling, parity binding, direct seed upload and API CPU diagnostics explicitly zero.
The case rejects missing activation/configuration, exercises every compiled plan
on singleton/thin/odd maps, compares exact arrays to an independent oracle and
required dispatch counts, and interleaves larger/smaller required buffers.
It verifies mask/argument/check counters and cancellation during initial upload,
kernel, convergence read and final readback, including stale session generation.
Checks greater than one also cancel a partially enqueued window, require no new
dispatch after cancellation, and reconcile bypasses to dispatches minus checks.
Caller input/session/cost retention and backend storage share the optional/total
host budgets; cancellation must return the subset to its previous reservation.

This is an implementation gate, not promotion evidence. Yielded/cold probe CPU
includes event polls, setup and lifecycle costs and is not faithful required
batch timing; it also excludes asynchronous driver CPU outside the calling
thread. Capture and live promotion remain disabled until calibration, full
process CPU, device contention and tick-tail gates pass. Unknown cost planes
still decline; this candidate does not warm them synchronously or on the owner.

Optional probe clocks must be nonzero and monotonic at setup, advance endpoints,
and each 64-entry copy checkpoint. An invalid measurement cancels new optional
commands and polls pending transfers before releasing captured storage. The
`invalidCpuClocks` probe counter makes missing expense explicit: future learning
callers must conservatively charge the full optional reservation or supply a
known external CPU measurement, and must never promote or settle it as zero.
The appended owned clock callback exists for correctness diagnostics; callers
charge and retain its captured allocation through the shared probe storage lease.

`GLOB2_OPENCL_PROBE_COLD_COSTS=1` enables isolated optional preparation for
immutable all-cell cost owners whose retained allocation size is known. Default
zero declines unknown/unready weighted planes. Costs use reserve-only storage,
validated packing in at most64 entries per CPU checkpoint, and a nonblocking
upload/event wait before seed upload or propagation. Known uniform proofs use
one packed word with no full-grid initialization. Prepared planes remain private
through probe destruction and never enter the shared cache, including after
upload completion; required work cannot wait on an optional cache future.

Cold preparation, captured owners, seed staging, lane/state and host capacity
share the16MiB probe subset and64MiB host budget; cost/device buffers also charge
the128MiB device budget. The caller retains/charges original captures separately,
so temporary duplicate owner charges are conservative. Allocation/driver calls
may exceed the500us target and are recorded as overshoots; clocks and chunking
do not create a hard bound or qualified promotion evidence. The diagnostic owned
device reservation models tracked-budget exhaustion and does not allocate GPU
memory or claim driver-allocation pressure coverage.

The explicit actual-device test `GLOB2_TEST_OPENCL_COLD_COSTS=1` requires the cold
configuration and all six ready plans. It starts weighted planes without forced
warmup, compares thin/odd toroidal fields and moved/removed goals/caps against the
independent oracle, and lets same-identity required work finish during cold
packing. Separate cases cancel packing/upload waits, invalidate the session,
reject unknown/forbidden-dependent/bad costs, and exhaust16/64/128MiB tracked
budgets. Use fresh processes with epoch zero and one separately; optional capture
and live learning remain disabled, and full cold CPU cost is paid per probe.
### Optional probe completion quarantine

Optional probe admission reserves one process-wide backend slot before state
construction, including fixed object/control host and probe-subset reservations
before `make_shared`/`new`. Its shared state owns the Device/API parent, original/cost/session
keep-alive graph and all host/probe/device leases. Every accepted asynchronous
command requests a trailing event on the in-order queue. Positive completion of
the latest event proves all preceding borrowed transfers are finished. Negative
event status, query failure or an accepted command without its requested event
does not establish that proof.

Ambiguous completion returns `Quarantined`, retains the already-allocated graph
in a bounded slot and disables further GPU/probe admission. Retention requires
no failure-path allocation. Repeated failed queries retain the same graph;
`reapOpenCLProbeQuarantine` polls only the existing event and releases retention
after a positive completion proof. External handles continue holding admission
until their payload is destroyed. Cancellation and premature disposal never
call blocking `clFinish` on the required coordinator. Ordinary shutdown cannot
free an unproved graph because the retained state pins the slot's parent; this
bounded cycle survives until proof or operating-system process reclamation.

One stable background coordinator owns begin, advance, result/metrics access,
destruction and reaping. These operations cannot overlap for the same probe;
reaping mutates its trailing event without taking the lane mutex. Only atomic
cancellation may originate elsewhere. A future broker must enforce this lifetime
contract before attaching captures.

An enqueue error preserves any returned event but latches unknown command
acceptance. Neither that errored event nor an older event can reap the graph.
An omitted completion event likewise cannot be reaped through an older event. This
candidate has no automatic emergency-drain thread and no automatic GPU recovery
after quarantine. It remains an isolated diagnostic prerequisite; required
synchronous Runtime failure paths still need a separate ownership repair. Tool
exit code 86 is not a game fallback. No timing sample or policy settlement may
be inferred from quarantine, and capture/policy leases must stay in its retained
graph. Hardware-free tests exercise the actual retention helper with live seed,
output and metadata sentinels, repeated failed proof, later successful reap,
foreign-owner rejection and last-external-owner admission release. The actual
submission helper also has injected first-command and prior-event enqueue-error
tests, including an error that returns a replacement event.

### Required transfer failure ownership

Required batches pre-reserve a heap transaction before submitting driver work.
Descriptors, convergence flags, preparing and active cost owners, retained events
and lane staging survive a failed event query or read/write. Original seeds are
never borrowed by a transfer; readback targets only private staging. Requested
direct-seed mode currently uses a safety copy even for blocking calls, since an
errored call cannot establish rejection or completion. `directSeedUploadSafetyCopies`
reports this path, and actual direct upload counts stay zero.

On failure the background executor polls an existing covered event once. Only
positive completion establishes proof. Otherwise one bounded required quarantine retains
the transaction and Device/API parent and disables new GPU admission. CPU recovery
uses the unchanged original seeds once on its worker and cannot race late driver
writes. An existing positive in-order event can reap only if no newer unobserved
command or ambiguous enqueue invalidated its coverage. Older, error-returned or
omitted events cannot establish proof; such storage remains retained until proof
or operating-system process reclamation. Required batches use a fixed pool of
64 precharged transaction slots, aligned with the 64 live lane registrations.
The production broker still runs exactly one required batch at a time.
Independent direct-API lanes can reserve separate slots; exhausting the bounded
pool declines to CPU without waiting. Production reaping and required execution
share one background coordinator. Reaper callbacks are serialized even for
independent direct-API callers. No reaping, Finish, driver wait, or fatal exit is
added to save/publication/owner code.

Hardware-free tests exercise the shared proof and retention machinery with late
readback writes, live descriptor/upload sentinels, failed queries, exactly one CPU
continuation and positive subsequent release. Actual driver failure, normal exact
arrays, save/replay continuity, storage pressure and release overhead require the
serialized native evidence campaign before this candidate is admitted.

Run `isolated required query failure*` alone in a fresh native test process with
`GLOB2_TEST_OPENCL_REQUIRED_QUERY_FAILURE=1` and nonzero `GLOB2_OPENCL_POLL_US`.
This default-off injection rejects the first polling read and immediate proof
query while preserving real queued storage. The test requires a usable device,
original-byte rollback before one CPU callback, retained cost-owner/host credits,
and later positive-event release. It deliberately disables the backend for that
process. It supplies failure-lifetime evidence, not performance or driver-hang
coverage.
### Captured replay adapter

`CaptureProbeReplay` is an unattached background lifecycle adapter. Its immutable
input owns original and golden arrays plus packed classic water costs. A borrowed
water lookup may pack a weighted capture in guarded blocks; no terrain graph or
borrowed lookup context survives capture. Modified movement still declines.

The adapter reserves its fixed state, private accounting-free BackendSession,
clock/backend owner graph and control storage before allocation. It never starts
device/compiler initialization. Every advance receives the current required
generation, tick, available slack and backlog; stale or late input cancels new
work while outstanding device transfers retain their graph until terminal proof.
Exact comparison visits at most 16 KiB per advance and never changes captured or
published arrays. Only atomic cancellation may cross producer threads.

Quarantine pins record, policy, session and leases. After positive terminal cleanup,
exclusive `takeSettlement` transfers the captured admission to a move-only token.
Call `cleanup` to release actual captured arrays while retaining admission, close
the outer complete-cost envelope, destroy the adapter wrapper, then settle. The
default authorization suppresses inference and conservatively pays full reserved
credit because calling-thread CPU alone does not cover driver/tail expense. Only
the future qualified `CaptureProbeAccounting` bridge can authorize inference.
Rejected setup leaves an attempted-work receipt; if ownership was not consumed,
the caller must retain/cancel that record and add the attempt's CPU to settlement.
Hardware-free injected backends test exact/mismatched results, required backlog,
unknown completion, retained admission, invalid clocks and unprepared alternatives.
These tests do not establish actual driver lifetime or performance qualification.

### Required transfer cancellation

Required GPU jobs have a pre-reserved owned `OpenCLRequiredControl`. It contains
only cancellation and an optional absolute `monotonicNs()` deadline. The worker
publishes this control atomically. At publication, pending save drain, reset, or
registration reconfiguration, the owner sets bounded cancellation metadata;
it does not inspect driver state or join optional work. The original executor
ticket still resolves after exactly one CPU continuation on original seeds.

Required uploads and readbacks now use nonblocking transfer events. The single
coordinator polls covered event completion, checking cancellation between polls
and dispatch windows. `GLOB2_OPENCL_POLL_US=0` uses a 100 microsecond safety wait;
explicit nonzero polling overrides retain their configured delay. Cancellation
interrupts the first controller's wait; other batch controllers are checked at
the next poll. The deadline is an optional conservative recovery trigger, not a
change to simulation publication ticks. No deadline is invented from tick IDs.

Unproved commands immediately disable further GPU admission and quarantine
private staging, descriptors, costs, controls, events, and backend lifetime.
Original mutable seeds are never a transfer destination, so late driver writes
cannot race CPU recovery or publication. Reaping uses only nonblocking positive
covered-event proof, never failure-path `Finish`. Driver API calls themselves
may hang internally: cooperative polling establishes safe retention and recovery
between calls, not a hard response bound for a broken driver. Changing blocking
transfer behavior needs fresh exact-array, actual activation, CPU overhead and
tail evidence; the previous measured candidate does not qualify this change.

`CaptureProbeAccounting` is the background-only envelope for captured replay.
Create it before `CaptureProbeReplay::begin`; reserve the complete retained
registry/reader graph, then record disjoint optional calling-CPU brackets around
allocation, queue manipulation, sampling, replay and cleanup. Required work is
excluded. Each envelope advance visits at most 64 registered clocks. The total
charge is the larger of capture-plus-optional calling CPU and the settlement
receipt, plus the enclosing process window's unknown CPU upper bound. Sampling
CPU is component telemetry and is not added twice. Unrelated concurrent process
work may be overcharged; unknown CPU must never be labeled causal driver CPU.

After device terminal proof, extract the settlement, call its idempotent cleanup,
and destroy the replay wrapper. Confirm cleanup with the matching ticket and both
generation namespaces. Keep that ticket and its storage lease through two
separate 250 ms post-cleanup intervals and the bounded final clock scan. The
accounting holder's final destruction is measured by `close` and added to both
costs before settlement; the outer service must likewise charge remaining queue,
receipt and settlement wrapper work. Raw process-CPU checkpoints at cleanup and each tail expose interval costs;
they are never extra additions or deductions. These empirical tails do not prove silence
or complete causal attribution. Invalid/churned/omitted clocks, generation changes,
chunk overshoots, incomplete cleanup and unqualified configuration suppress
inference and retain the full reserved CPU charge. Read `failed()` as well as
`tailFinished()` so invalid clocks cannot strand the tail state machine.

Only the accounting friend can mint an authorization, bound to the opaque ticket,
policy generation, required generation and minimum measured cost. A frozen native
release qualification is separately required; it remains false until the complete
tuning-on/off CPU, deadline and compatibility gates qualify that exact protocol.
Synthetic clock tests exercise this gate without supplying release evidence.
No compiler/device initialization or owner scans belong inside this envelope.
### Accelerator lane metadata storage

The backend registry has 64 weak lane slots. Execution payloads are allocated
separately from shared control blocks; a dedicated credit is pinned by the
control block and conservatively by the registry until its last weak reference
is released. Optional control metadata remains charged to the 16 MiB subset.
Concurrent status aliases retain the payload lease until actual destruction.
Terminal optional disposal and background quarantine reaping remove expired
registry entries only after their payloads have gone.

Device metadata is reserved before allocation, including bounded retained
strings and registry controls. Initialization accepts at most 64 platforms,
devices per platform, and work-item axes, with 2048-byte device identity strings
and 4096-byte compiler diagnostics. Oversized or malformed metadata declines
admission; driver allocations are outside these application-owned byte limits.
These lane and initialization bounds are independent of cost-plane, required
input, session, and optional capture accounting, which must also satisfy their
own lifetime reservations.

### Cost-plane metadata storage

Owned cost-plane admission reserves data and mask capacities, the declared
complete retained input graph, and fixed Plane/control/promise/failure metadata
before allocation. Buffer storage has its own preallocation credit, released
only after buffer and shared control destruction. Optional cold/scalar preparation
reserves these same fixed allowances in the 16 MiB subset before creation,
including the future buffer-storage allowance. Shared storage is globally charged
once; additional retained-owner/subset estimates are conservative duplicate
charges rather than claims of uniquely allocated bytes.

A fixed preallocated failure is published to the preparation future. It cannot
retain an arbitrary callback's exception graph in cached or quarantined storage.
Declared `retainedBytes` must cover the actual reachable immutable cost graph;
the backend cannot determine the size of arbitrary caller-owned objects.

The isolated `GLOB2_TEST_OPENCL_HOST_ADMISSION=1` case requires an initialized
GPU and proves healthy required admission rejection, unchanged originals and
subsequent exact recovery, both on a new lane and a retained lane. Its pressure
may reject the required transaction before cost-plane creation. Existing
`GLOB2_TEST_OPENCL_COLD_COSTS=1` cases with
`GLOB2_OPENCL_PROBE_COLD_COSTS=1` separately reach `CostsReserve` before applying
host/subset pressure and verify cancellation and complete lease release. Neither
case supplies allocator-size or performance qualification by itself.


Owned accepted captures carry an explicit readonly private-cost permission.
`CaptureProbeReplay` sets it only after immutable original/golden/cost storage
and CPU admission exist. It permits a proven scalar or chunked private cold cost
plane even when the development cold/uniform flags were latched off during an
earlier forced run. It never permits compiler/device initialization, changes
required cache contents or exposes a pending optional plane to required work.
Legacy optional inputs keep permission false and retain their existing gates.
Fresh-process native activation uses
`GLOB2_TEST_OPENCL_CAPTURE_PRIVATE_COSTS=1` with
`GLOB2_OPENCL_PROBE_COLD_COSTS=0 GLOB2_OPENCL_UNIFORM_METADATA=0`, parity/direct/API
CPU diagnostics disabled, and the desired epoch/check configuration. The case
requires all six plans and exact72 completed arrays (24 scalar,48 weighted),
unchanged originals, one private cost upload each and no initialization CPU change.
This receipt tests ownership/exactness; it does not qualify live promotion.

### Controlled live automatic capture

`GLOB2_GRADIENT_TUNING=1` attaches the captured-job protocol to automatic mode
with at least two compute slots. CPU and one-slot configurations do not create
learning policies. This remains opt-in pending native qualification of this
complete configuration; historical forced-GPU measurements do not qualify it.
Unknown profiles execute required work on CPU. A worker may reserve one sampled
capture after seeding, then copies original and golden arrays around the accepted
CPU propagation. The coordinator alone replays, compares, samples process CPU,
retires storage and trains/promotes plans. Required scheduling and presentation
never join optional replay or its post-cleanup accounting tails.

Automatic classification, cadence samples and accepted CPU credits activate only
after background initialization succeeds with valid startup accounting. Pending,
failed, stopped or quarantined native devices leave required jobs on CPU and do
not keep fused seed counting enabled. An atomic availability snapshot changes
worker admission and the next owner's seed preparation without driver queries,
initialization or joins on the owner. Explicit forced cross-due diagnostics remain
separate. This guard does not enable tuning by default or qualify its overhead.

The service has fixed64 ready-request slots,64 registrations,64 observation slots
and128 cadence slots. Requests beyond those limits decline to the original CPU
continuation. Its byte counter aliases the leased State so an escaped required
holder cannot release the State credit early. Production hooks are empty; test
hooks must own their captured state. Request-descriptor allocations are included
in the fixed State allowance. Broker storage is separately precharged.

Fused source/blocker counting uses stores already performed by supported seeders,
without a classification scan or per-cell clock reads. It belongs to accepted-plan
preparation and enters both the CPU credit/reference and common paired preparation.
Its extra instructions still enter total process CPU and the complete tuning-on/off
qualification; add an isolated classification ablation if that difference is material.

An unseen qualified alternative uses a1ms exploratory dispatch admission quantum,
not a predicted whole-job completion bound. Recheck generation, required backlog,
tick and slack between every bounded setup/comparison block or dispatch. At actual
completion another full measured completion interval must remain before the due
estimate. Promotion retains the measured maximum and2x uncertainty margin; no
unknown required job is selected from the exploratory quantum.

Bounded CPU setup and comparison blocks request the next broker turn immediately;
the broker rechecks required EDF work before each turn. A retained native event
without terminal proof uses the 100 us polling delay, and quarantined graphs retain
their 100 ms reaping delay. Completed dispatch transitions do not add a second
artificial wait. Accounting tails still sleep until their two 250 ms endpoints.
Each advance retains its existing CPU slice and single-kernel limit; this changes
scheduling latency rather than the CPU credit, accounting or promotion bounds.

Cold compiler/driver preparation is separately reported as an enclosing whole-process
CPU upper bound. Charge it as startup debt on the first accepted observation.
Concurrent process work may be overcharged, but readiness never means free startup.
Further optional admission must fit both the rolling256-tick1% credit cap and the
cumulative1% cap, so startup must be amortized before optional replay is admitted.
Invalid startup clocks suppress further admission. Cold and warm qualification
windows remain separate and include this actual initialization behavior.

After complete envelope closure, add a predictable500us retirement allowance to
both candidate cost and tuning credits for settlement/statistics/diagnostics. Measure
the actual remaining work; an overshoot adds debt and demotes the plan. Diagnostic
`gradient_probe` rows identify ticket/generations, admission/device/cleanup/tail
endpoints, process CPU/known deduction/unknown upper bound, preparation/calling/
comparison costs, reserved/charged CPU and inference eligibility. They are emitted
only on the background coordinator. Required GPU observations include the valid
narrow enclosing process window's unknown CPU upper bound for conservative demotion;
these windows do not claim causal driver attribution or replace full-game qualification.

Worker capture completion has a separate optional CPU bracket around golden
cloning, queue submission and capture-wrapper disposal. Only the valid internal
clone expense, frozen before coordinator ownership transfer, is subtracted from
that bracket; the remaining work is tuning debt. Zero/reversed/inconsistent
measurements charge invalid debt rather than supplying free work. Rejected queue
submission and required-exception cleanup remain on the worker; conservative
cancellation may overcharge cleanup, but never includes required propagation.
The accepted propagation reference remains frozen separately and excludes these
optional expenses. Fused seed counts continue to belong to required preparation;
there is no new scan or per-cell clock. Their incremental instruction cost must
pass the complete tuning-on/off process CPU gate (or a classification ablation
when material), independently of the optional probe credit budget.

Policy credits, captures, observations and cadence use the pipeline's local tick
namespace. A loaded snapshot's game tick is immutable simulation input, not a
policy-credit timestamp. Mixing those clocks after load/reconfiguration would
exclude all fresh rolling credits and prevent capture admission; saved-game
activation checks must cover a nonzero game tick with a freshly configured pipeline.
### Service lifecycle source fixtures

The service's replay hook is empty in production. Injected lifecycle fixtures
declare their complete retained owner graph and reserve that allowance with the
registration before copying the hook. They exercise one active plus 64 queued
required fields with two exact CPU recoveries, accounting admission pressure
before backend entry, and map/session destruction while an independent optional
transfer retains its policy ticket until positive background proof. These fake
backends do not establish actual OpenCL event or performance qualification.

Run `isolated process shutdown abandons unresolved optional proof without joining
it` alone in a fresh `glob2-unit-tests` subprocess with
`GLOB2_TEST_CAPTURE_SHUTDOWN=1` and an external timeout. It deliberately leaves
the fake borrowed graph unresolved. Successful normal process exit, without
forced termination, is part of the required result; an in-process test return
alone does not validate the broker's shutdown path.

For the tuning overhead comparison, set `GLOB2_GRADIENT_TUNING_PROMOTION=0`
in both automatic off/on variants. New profiles keep required CPU plans fixed,
while genuine captures, device replay, exact pairing, confidence updates and
retirement/accounting still execute when tuning is on. Report this override and
confidence-qualified counts. It prevents GPU savings from masking optional
expense. Separately validate the ordinary promotion-enabled configuration's
actual GPU selection, exactness and whole-mode responsiveness; suppression
alone cannot qualify promoted execution.


Pipeline reset/reconfiguration preserves its monotonic local credit/cadence tick
and sets the last-submission marker to that tick. A new submission still requires
an advance. Publication and save/restore remain relative to current ticks, and
Map executor deadlines retain their external simulation-boundary callback.
Retirement performs only bounded session/policy generation invalidation after
required work drains. Captured old generations cancel on the coordinator, keeping
their policy/global slot until storage and events are actually released; credit
history and established plans are retained without owner-side profile cleanup.

Owned captures transfer their already reserved host/probe graph allowance into
trusted storage credits. A private cost-owner wrapper pins the immutable replay
state plus capture and state credits. Native planes omit a repeated retained
graph charge only when both the wrapper pointer and shared control identity
match; arbitrary cost owners remain conservatively charged. Plane, Storage,
staging, output and device buffers still receive their own allowances. The
wrapper has a separate precharged control allowance; escaped weak wrappers or
records retain credit after payload cleanup. Settlement retains its credit until
policy mutation and the final policy alias release, and no replay-state member
owns the wrapper back from a plane.

Run `isolated trusted storage admits exact weighted 1024 replay and declines 2048
capture` alone in a fresh native subprocess with
`GLOB2_TEST_OPENCL_CAPTURE_STORAGE=1`, uniform metadata, cold-cost diagnostic,
parity, direct upload and API CPU modes all zero. The same 1024 weighted owned
capture must decline under the legacy duplicate debit and then complete actual
private native cost packing/dispatch/readback under trusted credit. Its exact
array is compared with accepted production eager CPU output and the original
array is unchanged. A 2048 capture declines before any clone or clock read,
because its two ushort arrays alone exhaust the 16 MiB subset. An injected
adapter clock keeps this ownership test separate from timing qualification;
neither it nor fake backend controls can promote a plan.

Optional scalar batch capture must reserve its complete future replay graph
before cloning the first input. `OpenCLProbeBatchStorage` transfers the group's
real paired host/probe admission and separately reserves device bytes on an
already initialized backend. Local native/adapter/accounting claims subdivide
that promise; returning a local claim never releases the process-wide promise.
The holder and its shared control retain the full admission until all payload
and weak aliases disappear. Generic and singleton paths retain direct budgets.

The estimate includes the actual flattened ushort values array reused for upload
and readback, native fixed objects and scalar Plane/Storage metadata, two device
gradient buffers, two tile masks, flags/descriptors, and a checked fixed adapter
and complete registry/accounting allowance. Original and golden capture arrays
are separate four-byte-per-cell admission. Thus eight 512-square scalar fields
need about 12 MiB of arrays plus metadata; a 1024-square group must downshift to
two or fewer before capture, and a 2048-square capture cannot fit. Extra injected
diagnostic owner graphs require an explicit retained-byte allowance. Hardware-
free tests prove subdivision, rollback and weak-credit retention using real host
credits and explicitly synthetic device bookkeeping; they do not qualify device
allocation, batch dispatch, or live automatic execution.

`beginOpenCLProbeBatch` executes two, four or eight homogeneous scalar fields in
one real three-dimensional dispatch per advance. Originals remain immutable;
owned flattened values supply uploads and indexed readback. Per-field flags
retire a converged field only after its two pingpong regions agree, and an
updated owned descriptor receives positive transfer proof before reuse. A
prepaid group retains its full future admission through native payloads and
escaped weak controls. This API does not initialize a device or compile a plan.

Run `isolated native probe batches share one dispatch and preserve indexed exact
retirement` alone in a fresh native subprocess with
`GLOB2_TEST_OPENCL_PROBE_BATCH=1`. Set parity, direct upload and API CPU modes to
zero; separately cover active epoch zero/one and check intervals one/eight/32.
The 72 rows cover all six plans, thin/odd shapes, two/four/eight fields, mixed
retirement, arbitrary original seeds and caps, required-lane interleaving, and
cancellation after actual submission. A fully activated successful case has
2,534 successful assertions; polling does not add variable assertions.

Run `isolated native batch storage transfers whole promises before real 512
clones` alone with `GLOB2_TEST_OPENCL_BATCH_STORAGE=1` and the same excluded
required diagnostic modes. Its 49 assertions cover actual initialized-device
pre-admission, rejection under tracked device-budget pressure before cloning,
two 512-square exact production-CPU arrays, local credit return, and retained
global promise through an escaped weak holder. Tracked pressure is not driver
out-of-memory proof. Initialization and dispatch in both fixtures belong to the
background coordinator. These tests establish exactness and ownership only;
native release CPU, process accounting and live promotion require separate
evidence.

Terminal replay receipts retain the policy-bound eager propagation reference,
common required preparation and reserved optional CPU separately. They also
retain the latest native stage, copied cells, prepared cost cells, dispatches and
checks after native storage is destroyed. Budget, required-priority/deadline,
explicit cancellation, clock invalidity, block overshoot and backend failure
remain distinct diagnostic causes. These scalars introduce no extra CPU clock
reads or driver queries and do not qualify incomplete or overspent pairs.
