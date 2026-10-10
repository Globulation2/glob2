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
`GLOB2_OPENCL_POLL_US=1..1000` tests sleeping between transfer-event completion
queries; zero (the default) uses blocking transfers. Keep this a separate
candidate because CPU savings can trade against publication latency.
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
default to zero, retain inactive buffer copies and original-seed recovery, and
decline live optional probes because their yielding implementations have not
been qualified. Check `active_epoch`, `tile_mask_clears`, `parity_bound`, and
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
most exponent ten in each dimension and does not admit 2048² fixtures yet.

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
