# Deeper software renderer: measured evidence

This evidence measures candidate **49bddcbeda51d0ecc980b9d8a1e03592d4eabf32** against base **6b95c81c0d438f64e3391ff4785bc95d3436d174**. Baseline carries only the identical benchmark camera, profiler/progress and deterministic HUD patch; its renderer/scheduler is unchanged. This is not a measurement of later master integration. See PR [1174](https://github.com/Globulation2/glob2/pull/1174).

## Results

Complete-frame ratios below use the median of 12 independent paired per-run median ratios, with deterministic 20,000-resample paired percentile bootstrap 95% intervals. Lower is better. Frames are not pooled.

| Fixed workload | New parallel / new immediate | New parallel / old immediate |
| --- | --- | --- |
| static-map | 1.0024 [0.9494, 1.0530] | 1.0008 [0.9059, 1.0481] |
| animated-map | 0.9552 [0.9385, 0.9824] | 0.9413 [0.9198, 0.9911] |
| half-gui | 0.8799 [0.8758, 0.8882] | 0.4744 [0.4686, 0.4791] |
| native-gui | 1.0547 [1.0529, 1.0698] | 0.5854 [0.5739, 0.5938] |
| streamed-map | 0.7062 [0.7032, 0.7085] | 0.7067 [0.7045, 0.7097] |

Half-scale GUI complete frames improve approximately 12% from parallel scheduling over the new immediate kernels, and 52.6% versus baseline immediate. Native-scale GUI benefits substantially from the new kernels, but parallel scheduling is approximately 5.5% slower than new immediate. Warm static performance is inconclusive around parity; its baseline comparison upper interval meets the specified 5% regression bound. Forced streamed rebuilding is a deliberately heavy case, not ordinary cached gameplay.

| Native instructions/frame, millions | Old immediate user; kernel | New immediate user; kernel | New parallel user; kernel |
| --- | --- | --- | --- |
| static-warm | 7.770; 0.325 | 7.770; 0.336 | 7.770; 0.321 |
| animated-warm | 33.397; 0.373 | 33.398; 0.407 | 33.607; 0.928 |
| gui-half-pan | 181.546; 0.734 | 109.863; 0.519 | 112.621; 2.823 |
| gui-native-pan | 52.101; 0.461 | 32.841; 0.362 | 34.506; 2.133 |
| streamed-pages | 289.620; 1.152 | 289.632; 1.200 | 292.902; 4.986 |

Callgrind's exact reconciled instruction counters place **80.49%** of half-GUI process instructions on graphics workers. This measures work ownership, not CPU-time shares or native throughput. Three repeats reconcile every thread's function self costs to footer totals and all thread totals to global collected counters.

Native PMU uses three repetitions per case/mode and reports user/kernel instructions and branches separately. All 75 stat groups schedule the four requested events at 100%. Kernel counts include synchronization and vary with scheduling. Absolute PMU and Callgrind totals need not match.

The 12 instruction-sampling profiles contain 21,160 parsed sample headers, 1 lost-sample count reported by perf, 5,399 samples with no stack, and 46 unresolved nonempty leaf stacks. Empty stacks occur extensively on workers. Do not infer precise worker proportions from the incomplete native stack output. Use exact Callgrind counters for worker share and PMU stat for total work; raw perf.data/self reports remain available. Sampling limitations do not affect counter or isolated timing gates.

Live simulation has 12 runs per mode and records positive tick progress, displayed frames, scene age and deadline distributions separately. Runs last a fixed duration and can execute differing work; no fixed-work speedup is inferred from them.

All specified half-GUI work-coverage, instruction-overhead, draw and complete-frame targets pass. These targets do not establish a universal win or authorize automatic pipeline selection. Immediate remains the default.

## Measurement boundaries and inputs

Linux x86_64, Ryzen 7 6800H, GCC 13.3, SDL 3.4.16. Native benchmark release builds use `scons release=1 software-render-benchmark`, no fast/PCH/unity build. Exact binary hashes, source revisions, patches, dependency/library hashes and command environments are supplied. Files under `symbol-bin/` are the exact roughly 57 MiB measured release executables; the directory name does not imply full debug information.

Frozen-state cases prepare/capture the scene every frame; they do not reuse one extracted scene. Draw includes scene preparation, command recording and intermediate flushes; final raster work may occur during presentation. Complete-frame timing is therefore checked alongside draw timing. Panning advances 0.25 logical pixels per frame. Clouds are disabled; fixture is tick 0, 128×128, four teams, 40 units, 100 buildings, simulation checksum 2231453127. The fixture SHA256 is `26cc515a843c418ccc6af6d024398ee5ab1c6d535e6e01d1c53635cd881d3da4`.

Timing: 12 groups × six cases × five modes = 360 runs; fixed cases use 240 measured frames after 30 warmup. The successful real cgroup-v2 cpuset audit reserves full physical cores/SMT siblings CPUs 0–11, runs on 0,2,4,6,8,10 and restores the partition/affinity afterward. Audit checks remain valid throughout. This excludes competing user tasks from those cores but does not isolate interrupts, shared caches, package power or memory bandwidth. Static variability remains visible in the interval.

Perf/Callgrind: warmup 30, measure 10 frames except streamed rebuilding 5; 75 stat runs, 12 sample profiles and nine Callgrind half-GUI runs. Perf attaches all existing threads at pre-loop stops, acknowledges enable/disable via FIFO and excludes warmup/startup/shutdown. The preload `getrusage` boundary sources are included. Instrumented timing is invalid and never used. Callgrind serializes threads; its explicit 32 KiB/8-way I1/D1 and 16 MiB/16-way LL cache model does not measure hardware bandwidth.

The benchmark now initializes camera bounds before applying zoom, verifies requested/effective zoom, and seeds fixed-scene HUD counters. GUI starts at the team viewport; map mode preserves origin zero. Previous mislabeled half-scale comparisons are superseded. Runner verifies camera, dimensions, population, checksum and completed frames across fixed variants, and byte-identical candidate captures across immediate/serial/parallel.

Baseline/candidate capture differences are bounded: native GUI maximum RGB-channel difference 1, half GUI maximum 2, alpha unchanged; candidate modes match exactly. Raw captures and visual comparisons are retained. Intentional integer blend rounding differs slightly from SDL; geometry, sampling and alpha are preserved.

## Validation and coverage

At renderer-tested revision d12f2938d1b3737ab6ed4a0d40f69c60a7b1ce24 on base 6b95: 75 focused unit, 115 affected engine and 24 display cases pass. Benchmark-only deterministic HUD changes are subsequently validated by 240-frame native captures and the complete measured matrix. The supplied direct logs/XML include earlier failures and build iterations as historical diagnosis; their names/timestamps must not be confused with final validation.

Standalone ASan/UBSan raster/cache checks pass. Portable scalar kernels run under Node/WASM (12 cases, 98,871 assertions); adapter checks have two cases/29 assertions. These are not a full browser graphics/application test. Windows/macOS/Android/iOS/native ARM were not executed. Existing full-unit recording/catalog failures were reproduced on baseline; isolated recording passes. Automated live probes and visual captures were reviewed; no human playtest was performed. Latest-master integration verification will be recorded separately in the PR.

## Evidence contents and reproduction

`final-analysis.json` is the detailed analysis; `file-manifest.json` inventories every extracted evidence file and SHA256. `SHA256SUMS` verifies archive parts. Archives contain final raw `comparison/`, `perf/`, `callgrind/`, captured images, exact binaries, patches, fixture, shaders, provenance, analysis/collection scripts and their prior `render-profile/` dependencies. Prototype, pre-HUD, pre-integration and Python cache directories are excluded.

All 7,470 runtime assets exactly match the already published immutable [input archive](https://github.com/Globulation2/glob2/tree/ee94fe474a9f5ecf23cd0381d5a40df690f0290d). Download its four `inputs.tar.gz.part-00` through `part-03` files and verify its SHA256SUMS. Concatenate/extract to recover `runtime-assets/`; the new input manifest verifies every asset. Fixture and shader archive are included here, avoiding another asset upload.

After downloading this branch:

```sh
sha256sum -c SHA256SUMS
cat evidence.tar.gz.part-* > evidence.tar.gz
tar -xzf evidence.tar.gz
python3 software-raster-depth/analyze_final.py
```

The analysis requires all final datasets and a real completed/restored cpuset audit; it refuses partial, unvalidated or instrumented timing evidence. It regenerates final-analysis.json solely from retained raw evidence.

To rerun measurements, check out base 6b95c81c0d438f64e3391ff4785bc95d3436d174 and apply `baseline-harness.patch`; independently check out candidate 49bddcbeda51d0ecc980b9d8a1e03592d4eabf32 (or reconstruct using candidate-source.patch on base). Supply pinned SDL/dependencies and build both with the release command above. Copy executables to software-raster-depth/symbol-bin/baseline and candidate. Update original machine paths in render-profile/support.py (fixture/assets/shaders/display) and the collection scripts; preserve directory adjacency because imports reference render-profile. Use fresh output directories rather than reusing metadata.

```sh
python3 software-raster-depth/native_counts.py
CG_REPEATS=3 python3 software-raster-depth/cg.py
# Reconcile raw Callgrind with the included parse_callgrind.parse_run API:
python3 - <<'PYCODE'
import json, sys
from pathlib import Path
sys.path.insert(0, 'render-profile')
from parse_callgrind import parse_run
root = Path('software-raster-depth')
runs = [parse_run(p) for p in sorted((root/'callgrind').iterdir()) if (p/'metadata.json').exists()]
(root/'callgrind-parsed.json').write_text(json.dumps({'runs': runs}, indent=2)+'\n')
PYCODE
python3 repository-tools/run_software_render_comparison.py \
  --baseline software-raster-depth/symbol-bin/baseline \
  --candidate software-raster-depth/symbol-bin/candidate \
  --save software-raster-depth/fixture.game --assets /path/to/runtime-assets \
  --output software-raster-depth/comparison
python3 software-raster-depth/analyze_final.py
```

Privileged read-only perf and the audited temporary cgroup require existing host permissions; no perf security or global CPU setting changes were made. The supplied cpuset example is specific to this machine and must fail on incompatible topology. Run timing independently of profilers/builds. Exact original commands/environments remain in every profile metadata and comparison manifest.

Evidence is isolated on a dedicated orphan branch; these generated reports, profiles and executables are not production PR contents.
