# Resource benchmarks

Reproduce resource-catalog and growth comparisons with retained scenarios and controlled measurement conditions. These procedures collect evidence; they do not establish a universal performance guarantee.

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
