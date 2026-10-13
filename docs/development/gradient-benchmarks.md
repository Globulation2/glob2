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

## GPU on/off verification

The legacy `--gpu off|on` compares CPU automatic with OpenCL. For all variants,
use the same release binary with `--gradient-backend cpu|scalar|sse2|neon|opencl|vulkan|webgpu`
and `--gradient-algorithm buckets|frozen8|jacobi`. Vulkan/WebGPU expose both GPU
algorithms; OpenCL retains Frozen8. Unsupported devices/operations use CPU
fallback. A one-slot pool cannot execute GPU gradients.

`game run` records `gradient_backend`, `gradient_algorithm`, `gradient_device`,
`gradient_driver`, `gradient_shader`, `gradient_gpu_fields`,
`gradient_cpu_fallbacks` and verification counters alongside existing execution
metrics. Check actual committed fields before attributing a result to the GPU.
Settings displays backend timing and fallback status during live comparisons.

Set `GLOB2_GRADIENT_DIAGNOSTICS=1` for host attribution runs. Native OpenCL and
Vulkan expose `gradient_host_diagnostics` in the game result or match's
`compute.json`. Its nested phases partition thread CPU and elapsed time: snapshot
capture (shared with other snapshot consumers), preparation, input conversion,
allocation, uploads, command recording, submission, convergence checks, waiting,
readback, validation, output conversion, CPU solving, recovery and retirement.
Nested scopes subtract their inclusive duration from their parent, including
scopes belonging to an inner adapter session. Counters distinguish submitted
work and transferred bytes from device-confirmed execution and committed fields.
`executed_fields` proves at least one completed dispatch, `converged_fields`
requires complete converged readback, and `validated_fields` records successful
independent fixed-point checks where implemented. `committed_fields` counts
successful transactions. A canceled adapter batch can have validated private
fields without commits; these milestones must not be treated as synonyms.
The device service batches OpenCL fields, but admits individual Vulkan/WebGPU
transactions so a later sequential field's decline cannot discard an earlier
completed field. Direct adapter batch callers still receive an atomic outcome.
OpenCL submissions count explicit queue flushes; command enqueues are separate.
Vulkan submissions count successful queue submissions. `gpu_validation_dispatches`
and `gpu_validated_fields` separately report its independent GPU proof pass;
`reference_upload_bytes_submitted` identifies independent uploads of immutable
seed/cost references within total uploaded bytes.
The full CPU fixed-point check remains enabled while that pass is qualified.
`proveVulkanGradientFixedPoint` is a synchronous diagnostic harness, not a solver
or publication path: tests require its device marker and reject missing results.
Its generated WGSL is currently unused by WebGPU. `resource_reuses` counts
retired Vulkan work sets borrowed from the bounded adapter pool. Mapped control reads are
separate from explicit transfer/readback bytes. Elapsed totals accumulate thread
scopes; overlapping threads do not form a process critical-path measurement.
These are API costs, not
measurements of GPU kernel duration or a separation of driver and application CPU.

The enable decision travels with admitted jobs. Disabled scopes read no clocks
and update no counters. `cpu_ns` is null without a usable CPU sample; inspect
`cpu_invalid` before using any partial aggregate. Browser clocks do not establish
worker CPU usage. The shared counters and phases do not qualify browser execution
or provide JavaScript/GPU-process attribution.

Result snapshots precede application teardown. When diagnostics and
`GLOB2_TEST_ARTIFACTS` are enabled, `gradient-host-final.json` captures cumulative
totals through explicit coordinator/adapter drain. Static driver teardown and
the diagnostic file writing itself may follow that snapshot. Measure complete
child-process user plus system CPU separately to include startup, final drain,
recovery and exit. Keep diagnostics, native sampling and GPU profiling outside
acceptance runs, and check actual GPU commits: short release runs can finish
before asynchronous device initialization makes any field eligible.

Freeze save inputs, source/executable/shader hashes, orders, tick windows, CPU
affinity and compute budgets. Separate initialization, uploads, device computation,
readback and deadline waiting. Measure driver/coordinator process CPU too; worker
waiting alone does not demonstrate CPU savings. Disable shadow verification in
timing runs; use `--gradient-verify on` separately for exact CPU comparisons.

Run the `RuntimeGradientBackend`, `GradientBackendSwitch`, `OpenCLGradient` and
existing gradient/save/replay suites. `GLOB2_REQUIRE_VULKAN=1` makes missing Vulkan
hardware a test failure; otherwise hardware-only cases report their omission.
Chromium's `gradient-compute.spec.js` checks generated WGSL against independent
exact fields. Keep real hardware and software-driver coverage distinct.

The `RuntimeGradientBackend` retained Vulkan case replays all three original
NVIDIA proof inputs from `codex/runtime-gradient-merge-evidence` and two subsequent
whole-game failures, with both
algorithms and input roundtrip verification enabled and disabled. The compact
permanent inputs retain the original proof hashes in
`test/fixtures/gradients/RetainedVulkanSeeds.h`. Successful dispatch observations,
GPU commits and independent heap-oracle equality are all required. These isolated
tests do not resolve intermittent whole-game failures or qualify performance.
`GLOB2_VULKAN_VERIFY_TRANSFERS=1` verifies uploads through GPU copy/readback;
unset or `0` disables the additional transfers. Use it only in diagnostic runs.

Analyze a saved proof independently, including untouched 32-bit corruption:

```sh
python3 tools/gradient_proof.py artifacts/gradient-proof-*.bin \
  --output artifacts/gradient-proof-analysis.json
python3 tools/test_gradient_proof.py
```

The analysis reports cells above and below the independent shortest-path result
and rejects malformed payloads. It consumes the captured seeds and costs rather
than reconstructing inputs from the current live world.
The same tool reads `artifacts/gradient-transfer-*.bin`, reporting expected/actual
word differences separately from field correctness. Transfer evidence retains the
original seeds, buffer slot, dimensions, cap and device/shader/generation sidecar.
The upload-retirement regression varies seed and cost patterns at 128², 256² and
512² with a zero propagation cap, isolating input preservation from relaxation.

Compare per-tick checksums, saves and replays across backends, worker counts and
platforms. Exercise initialization, memory, dispatch, readback, cancellation,
device loss, live changes and shutdown. Headless tests do not establish GUI
responsiveness; retain screenshots and live-play evidence separately. Budgets
remain 64 MiB host and 128 MiB tracked device payload; measure driver allocations
separately. No performance improvement is assumed by enabling a backend.
