# Matched master comparison: snapshot rendering

Baseline: `a60018aa0066d1f4dc5e733cb86d0085b80bd604` (master before the rendering stack, including snapshot gradient work). Final: `4c3f6074ff1fd1c5f91dc9a7e1fac448d274d98e` (merged rendering stack and wasm32 repair). This supersedes extraction-only or forced-parking tests as evidence about whole-engine performance.

These measurements do **not establish a general throughput or critical-path speedup**. Large-map rendering-active CPU cost increased consistently in the two- and four-CPU runs. Wall-time comparisons are noisy on this shared machine. This is evidence of a performance concern, not proof of the underlying cause.

## Method

Linux x86_64, Threadripper 2950X, GCC 15.2, release -O3, identical SDL dependency prefix. See environment.json for versions, source and executable hashes and build flags. Both revisions receive identical benchmark-only probe/harness code and equivalent owner-loop hooks; complete patches are included. Minimal benchmark-only test executables link the production engine objects. No production changes were made for this measurement.

Two identical saved four-AI spectator games: balanced 128×128, game seed 123, and generator 15 / 512×512, map seed 4242, game seed 19. Save bytes and hashes are included. Balanced starts at tick 0 with 40 units and finishes tick 2500 with 81; large starts tick 5000 with 48 units and finishes tick 7500 with 72. These are early/sparse games, not dense late-game stress tests. Fixed mouse motion, no gameplay orders. Each run starts a fresh process/profile, warms graphics with 20 static frames, warms simulation 500 ticks, then measures exactly 2000 ticks. Simulation runs at maximum speed. Verification runs are separate from timing runs.

800×600 portable GPU backend under Xvfb/Mesa llvmpipe, one software-renderer worker, target 60 FPS. Actual frame counts vary with simulation duration and missed frame deadlines; CPU/tick includes this presentation work. This is not a physical-GPU FPS benchmark. Two/four CPU allocations use two/four compute participants (owner included). Eight allocated CPUs retain four compute participants to leave more capacity available to presentation. Allocation means affinity, not exclusive cores. Six paired runs per rendering configuration, alternating baseline/final order; four pairs per control. Do not compare different CPU configurations as a scaling curve: they ran at different times under different host contention.

## Results

Numbers below are medians of six run-level statistics, baseline → final. CPU is aggregate process CPU milliseconds per simulated tick (summed over engine threads). RSS is median sampled process resident memory in MiB. Tick p95 is the interval between simulation completions, including owner work, parking and scheduling gaps. Owner p95 separately excludes between-iteration waits. Per-run quantiles are taken from 2000 measured ticks; table quantiles are medians of those run quantiles, not pooled quantiles.

| Map | Allocated CPUs / compute | CPU ms/tick | RSS MiB | TPS | Completion interval p95 ms | Owner iteration p95 ms |
|---|---:|---:|---:|---:|---:|---:|
| balanced | 2 / 2 | 3.10 → 3.23 | 1192.3 → 1194.8 | 363.1 → 366.4 | 8.28 → 8.44 | 7.84 → 8.44 |
| balanced | 4 / 4 | 3.11 → 3.12 | 1195.5 → 1194.1 | 309.8 → 359.6 | 9.78 → 9.12 | 9.46 → 9.12 |
| balanced | 8 / 4 | 3.18 → 3.25 | 1195.5 → 1197.0 | 447.4 → 549.9 | 6.70 → 5.43 | 6.46 → 5.43 |
| large512 | 2 / 2 | 15.42 → 17.66 | 1503.8 → 1516.8 | 68.9 → 63.9 | 48.22 → 51.16 | 47.70 → 51.16 |
| large512 | 4 / 4 | 14.43 → 17.42 | 1528.3 → 1555.2 | 91.3 → 91.3 | 35.90 → 33.47 | 35.10 → 33.46 |
| large512 | 8 / 4 | 15.51 → 19.79 | 1530.3 → 1555.0 | 101.9 → 95.0 | 31.37 → 33.23 | 30.68 → 33.22 |

Paired changes below compute final/baseline within each adjacent pair, then take the median. They need not equal the ratio of the independent medians above, particularly with this much host noise. Ranges are observed pair ranges, not confidence intervals. Lower CPU and tick latency are better; higher TPS is better.

| Map | CPUs | CPU change: median [range] | TPS change: median [range] | Completion p95 change: median [range] |
|---|---:|---:|---:|---:|
| balanced | 2 | +1.4% [-4.7, +18.4] | -5.7% [-21.5, +61.8] | -1.2% [-41.6, +44.7] |
| balanced | 4 | -1.3% [-10.3, +29.3] | +11.0% [-22.3, +23.9] | -6.6% [-16.7, +17.4] |
| balanced | 8 | +2.1% [-6.9, +29.7] | +19.7% [-14.1, +132.5] | -15.4% [-57.0, +7.9] |
| large512 | 2 | +16.0% [+1.4, +50.5] | -11.1% [-27.0, +43.2] | +11.7% [-37.0, +43.0] |
| large512 | 4 | +22.4% [+14.1, +32.0] | -3.7% [-9.1, +12.4] | -2.3% [-16.8, +4.8] |
| large512 | 8 | +19.7% [+1.1, +49.1] | -6.7% [-18.0, +31.8] | +3.4% [-30.1, +14.6] |

## Client/render-disabled control

Four CPU/four compute participants, four pairs. After the same static graphics warmup, this control does not service client frames/input or draw. It is not the headless CLI and does not isolate drawing alone. CPU changes here were much smaller than rendering-active large-map changes, suggesting presentation-related work is involved; profiling would be needed to attribute a cause. Wall-time improvements in controls are also affected by shared-host contention.

| Map | CPU ms/tick, baseline → final | Median paired CPU change | TPS, baseline → final | RSS MiB, baseline → final |
|---|---:|---:|---:|---:|
| balanced | 1.87 → 1.88 | +0.6% | 468.60 → 550.61 | 1193.72 → 1193.68 |
| large512 | 9.77 → 9.44 | -3.5% | 110.80 → 123.11 | 1504.94 → 1515.17 |

## Definitions and limits

- Process CPU uses CLOCK_PROCESS_CPUTIME_ID over the measured window, including all engine/llvmpipe threads, excluding Xvfb and other processes. Owner CPU uses CLOCK_THREAD_CPUTIME_ID. Window ends at the last owner iteration; worker drain/teardown is outside timing. This is steady-state window cost, not total launch-to-exit cost.
- simulation_ns measures the owner simulationStep span plus clock/delay checks before presentation work. owner_iteration_ns continues through Scene capture/preparation/submission and telemetry. completion_interval_ns adds gaps between owner iterations. Asynchronous rendering completion is not included in a simulation tick's completion dependency. CPU, owner timing, simulation timing and all percentiles are available in summary.json and raw ticks.csv files.
- Resident memory is sampled from /proc/self/statm about every 20 ms. It includes assets, graphics, simulation, Scenes and snapshots; it does not isolate snapshot storage or allocations. A prior temporary graphics-warmup Scene can leave allocator memory resident. Full-process peak RSS includes setup and warmup and is reported separately in the data.
- Heavy, uncontrolled concurrent host workloads and SMT contention remain despite affinity. Adjacent pairs, counterbalancing and CPU-time metrics reduce but do not remove noise. These results are not a quiet-machine acceptance benchmark. No statistically significant TPS win is claimed; measured effects should not be generalized to other hardware.
- Native Linux only. Browser, Windows, physical GPU, dense late-game maps, human interaction latency and turn/network workloads were not benchmarked here. Existing functional tests do not substitute for these performance measurements.
- 72 rendering-active timing runs, 16 controls, 12 separate verification runs. All 100 completed successfully, all final states match across revisions/core counts/modes for each fixture, and all six pairs of per-tick traces match for all 2500 warmup+measured ticks. Pilot/smoke rows in all-results.csv/results.jsonl are excluded from summaries and conclusions.

## Reproduction and raw evidence

Apply instrumentation/baseline.patch and final.patch to worktrees at the pinned respective revisions. The patches add the probe and harness, hook the owner loop, and replace test/SConscript with a minimal benchmark target. Build both with:

```sh
CCACHE=1 GLOB2_SDL3_PREFIX=/path/to/sdl3/prefix scons -j8 release=1 server=0 optimized_assets=0 engine-tests
```

Place the included fixtures alongside run_benchmark.py. Adjust its WORK paths and affinity CPU IDs for the target machine; preserve equal settings across variants. Use the included plan.json (500 warmup/2000 measured ticks), rather than rerunning pilot to select a new duration. Run sequentially:

```sh
xvfb-run -a -s '-screen 0 1024x768x24 -noreset' python3 run_benchmark.py verify
xvfb-run -a -s '-screen 0 1024x768x24 -noreset' python3 run_benchmark.py measure
xvfb-run -a -s '-screen 0 1024x768x24 -noreset' python3 run_benchmark.py control
xvfb-run -a -s '-screen 0 1024x768x24 -noreset' python3 run_benchmark.py wide
python3 analyze.py
```

The original controllers/Xvfb used taskset 0-11,16-27 for two/four-CPU runs and 0-7,16-23 for eight-CPU runs. Every executable invocation, cwd, environment, timestamps, host load and /proc/stat counters is in raw-runs.tar.gz. It also contains per-tick timings, metrics, checksum traces, build provenance and test logs. User profiles were omitted. Successful final build logs are compressed separately. verification.json records matching trace hashes. SHA256SUMS covers the exported files.
