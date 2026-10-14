# Gradient benchmarks

Measure individual field kernels and integrated game behavior separately; kernel speed alone does not establish an engine improvement.

## Preparation memory feasibility

Before allocating resident snapshots, run the native record-size worksheet:

```sh
python3 tools/gradient_preparation_memory.py \
  --output artifacts/gradient-preparation/memory.json
```

Set `GLOB2_SDL3_PREFIX` or pass `--include` for non-system SDL headers. The tool
compiles a size probe against the actual C++ snapshot records. It models 128²,
256² and 512² maps, one/two/four overlapping versions, eight pending fields,
whole snapshot arrays, stock sidecars, shared tables, staging and solver/readback
storage. Override `--pending-fields`, `--table-rules` and `--table-resources` for
the intended workload. It counts shared table owners once.

These numbers are a conservative worksheet, not an admission proof: variable
catalog allocations, allocator overhead, device alignment and other retained
gradient work still require actual lease accounting. The worksheet assumes
whole-snapshot host staging is live together; streaming staging can reduce that
part of the envelope. Sparse uploads do not reduce retained snapshot arrays.
The fixed limits remain 64 MiB host and
128 MiB device. On the native little-endian record layout, a material snapshot
uses 52 bytes per cell before multi-yield stocks; a full stock sidecar adds
24 bytes per cell. Under the worksheet's default assumptions, two fully stocked
512² versions exceed the modeled host envelope. Actual admission must use the
adapter's leases and allocation/retirement proofs.

Production currently prepares periodic seeds on CPU. The generated predicates
classify clearing cells once, using the open seed as a terrain
placeholder; only cells that remain open read terrain. CPU terrain lookups use
three compiled 16-bit columns (land, land/water and water-only), shared with
immutable rule owners. They cost six host bytes per rule, in addition to the
36-byte accelerator table. The CPU cell loop does not decode that wider table
or rebuild a terrain lookup for each field. Both representations are compiled
and rolled back together, and preserve all eight movement classes.

Complete CPU fields select scalar or SIMD execution before entering the cost-layer
sweep. A resumable building search resolves its CPU choice once per resume; a
later resume can use a newly selected variant without rebuilding the queues.
CPU-selected periodic jobs use their captured decision directly, retaining the
same input validation, trivial-field shortcut and fixed publication deadline.
GPU ownership admission, host-budget reservations and device coordination are
skipped for these jobs. Existing snapshot leases and pooled output buffers are
still required for asynchronous CPU execution and survive backend switching.
Rule insertion rebuilds prepared movement metadata only when adding a distinct
cost profile. Its rollback journal copies only the active prepared alternative,
so an ordinary insertion does not copy the 256-profile storage for every class.
Default execution omits optional compute diagnostics; enable telemetry explicitly
when measuring phase attribution, then disable it for default-path comparisons.

The generated predicates and shared residency state/range contracts do not establish GPU
preparation execution or qualify any platform. GPU preparation requires separate
kernel execution evidence and release measurements against CPU preparation using
the same backend and algorithm. Use at least eight alternating run pairs, count
final drain and unused preload CPU, and treat pairs as statistical units. Freeze
fixture weights, measurement windows and the paired confidence-interval method
before timing. Native and browser cohorts must remain separate.

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
median comparisons. `--cpu N` pins the subprocess on Linux. `--backend cpu|scalar|sse2|neon` selects kernels in the same executable.
`--scalar` retains the older build-time scalar reference switch.
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

## GPU correctness and performance

Use identical release builds and retained inputs with
`--gradient-backend cpu|scalar|sse2|neon|opencl|vulkan|webgpu` and
`--gradient-algorithm buckets|frozen8|jacobi`. OpenCL uses Frozen8; Vulkan and
WebGPU retain both GPU algorithms. A one-participant pool stays on CPU.

Numerical validation is test-only. The `RuntimeGradientBackend` suite compares
committed raw device outputs with an independent weighted-path oracle and tests
fixed-point corruption detection separately. The retained NVIDIA cases must
execute on the device; an unavailable adapter or CPU fallback cannot qualify GPU
coverage. Set `GLOB2_REQUIRE_VULKAN=1` when requiring the native Vulkan cases.
Set `GLOB2_REQUIRE_OPENCL=1` to require OpenCL device execution. Run browser
WebGPU cases separately.
`browser/tests/gradient-compute.spec.js` compares 66 fields, including all five
retained NVIDIA inputs on both algorithms with repeated buffer reuse. Set
`GLOB2_WEBGPU_EXPECT_DEVICE=NVIDIA` (or another device-name pattern) to require a
matching hardware adapter; software substitution fails the test. Without this
option the test uses SwiftShader for portable browser coverage.

Production checks input bounds, convergence and driver completion. Cancellation,
unknown retirement and device loss must preserve original CPU seeds and retain
GPU storage until it can be safely reused. The `GradientPipeline` lifecycle suite
covers fixed deadlines, initialization, bounded admission, one-shot recovery,
multiple registrations and teardown. Cross-backend full games must retain equal
per-tick checksums, replay orders and save continuation.

Measure user+system CPU, elapsed time, CPU per tick and peak RSS with one warmup
and ten rotated paired repetitions. Keep startup and warmed execution separate;
include recovery, final drain and exit in whole-process measurements. Compare
CPU-only builds before GPU variants. Investigate repeatable CPU regressions above
2% per workload and extend uncertain comparisons to twenty pairs. Timing limits
are review criteria, not CI timing assertions.

Results retain selected backend, device/driver/shader identity, committed GPU
fields and CPU fallbacks. Detailed phase profiling, scalar reruns, runtime proof
artifacts and their settings/CLI options have been retired. Do not report removed
measurements as zero. Budgets remain 64 MiB host and 128 MiB tracked device payload;
measure driver allocations separately. Device support alone is not evidence of
CPU savings or cross-platform correctness.
