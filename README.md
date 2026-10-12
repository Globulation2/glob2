# Cumulative renderer performance comparison

Comparison: original master `9b853c4a3dcb2647b8828b77bb48faf5251a3de5` (immediately before PR #1148) versus `2d5cf67e8ea20ad8ebb983c9b207e24234e0356c` (both #1148 and #1151 merged). This directly measures the cumulative implementation, rather than adding earlier percentages.

## Result

In the clean software-renderer trials, automatic execution reduced animated-terrain frame latency and process CPU cost about 4–7%. Static and GUI workloads were broadly unchanged. Explicit parallel execution reduced uncached-terrain stress latency 35% and cold-terrain first-frame latency 13%, at higher CPU cost. It increased half-scale/fractional GUI latency about 5–7% and CPU cost about 11–13%. Keeping automatic execution conservative is supported by these measurements. These results do not establish a blanket game FPS increase.

Here “automatic” means `GLOB2_RENDER_EXECUTION=auto` on the explicitly selected **software renderer**. It does not mean automatic selection of a hardware renderer. Hardware backend results are listed separately.

## Clean software-renderer measurements

Seven matched trials per variant/workload, with order rotated/reversed. 210 release processes total. All sampled compiler counts were zero; sampled one-minute load peaked at 3.14. The unrelated heavy simulation campaign started after this matrix completed.

Full-frame timing includes snapshot preparation, draw submission/execution, the final raster flush and native presentation. SDL event pumping and camera bookkeeping precede the per-frame wall timer, but are included in process CPU. Process CPU includes owner and worker threads. No `PROFILE_CPU_SCOPES` clock substitution and no `PROFILE_NO_PRESENT` override were used.

Absolute values below are medians across seven per-run frame medians. Percent changes are medians of the seven matched candidate/original ratios; because pairing accounts for drift, percentages need not equal ratios of the displayed medians. Negative changes mean less time or CPU. No frame distributions were pooled.

| Workload | Original frame ms | Current auto ms | Current parallel ms | Auto frame change | Parallel frame change | Auto CPU change | Parallel CPU change |
|---|---:|---:|---:|---:|---:|---:|---:|
| Warm static terrain | 1.577 | 1.542 | 1.554 | -1.9% | +0.6% | -1.6% | +0.2% |
| Animated terrain,1280x800 | 3.367 | 3.137 | 3.209 | -7.2% | -5.7% | -7.3% | +15.2% |
| Cold terrain first frame | 66.152 | 64.465 | 58.319 | -2.2% | -13.1% | -2.2% | +3.5% |
| Uncached terrain stress | 21.748 | 20.711 | 14.252 | -5.5% | -35.2% | -5.4% | +13.2% |
| GUI pan,1x | 4.335 | 4.303 | 4.296 | -0.2% | +0.3% | +0.4% | +0.9% |
| GUI pan,half-scale | 5.426 | 5.391 | 5.737 | -0.2% | +5.2% | -1.3% | +11.1% |
| GUI fractional,half-scale | 5.388 | 5.386 | 5.764 | -0.7% | +6.2% | -0.3% | +12.6% |
| Animated terrain,1854x1011 | 6.070 | 5.825 | 5.905 | -4.7% | -4.5% | -4.2% | +14.0% |
| GUI half-scale pan,1854x1011 | 9.979 | 9.926 | 10.619 | +0.0% | +6.7% | +0.1% | +11.8% |

Treat differences around 1–2% as near parity, rather than portable gains. Cold rows summarize seven separate first-frame observations after game/assets loaded; they do not measure total startup or establish p95/p99 tails. The uncached case deliberately disables the terrain page cache and represents a composition stress case, rather than normal warm-cache play.

| Workload | Original CPU ms/frame | Current auto CPU ms/frame | Current parallel CPU ms/frame | Original frame p99 ms | Auto frame p99 ms | Parallel frame p99 ms |
|---|---:|---:|---:|---:|---:|---:|
| Warm static terrain | 1.682 | 1.656 | 1.654 | 2.194 | 2.181 | 2.190 |
| Animated terrain,1280x800 | 3.525 | 3.307 | 4.089 | 4.179 | 4.019 | 3.910 |
| Cold terrain first frame | 67.560 | 65.904 | 70.888 | n/a | n/a | n/a |
| Uncached terrain stress | 22.214 | 21.224 | 25.155 | 22.655 | 21.788 | 15.524 |
| GUI pan,1x | 4.461 | 4.448 | 4.481 | 5.251 | 5.048 | 5.164 |
| GUI pan,half-scale | 5.618 | 5.535 | 6.217 | 6.660 | 6.290 | 6.905 |
| GUI fractional,half-scale | 5.586 | 5.582 | 6.280 | 6.426 | 6.460 | 6.834 |
| Animated terrain,1854x1011 | 6.310 | 6.064 | 7.351 | 7.111 | 6.960 | 6.848 |
| GUI half-scale pan,1854x1011 | 10.350 | 10.207 | 11.410 | 11.337 | 11.204 | 11.907 |

The p99 columns are medians of seven per-run p99 values, not pooled p99. Raw distributions, paired ratios, ranges and all logs are in [the archive](verification.tar.gz).

Packed serial controls also ran for animated terrain, uncached stress and half-scale GUI pan. These isolate the packed path without worker execution:

| Workload | Current serial frame ms | Serial frame change vs original | Serial CPU change vs original |
|---|---:|---:|---:|
| Animated terrain,1280x800 | 3.259 | -2.2% | -2.2% |
| Uncached terrain stress | 21.178 | -2.6% | -2.8% |
| GUI pan,half-scale | 5.678 | +5.0% | +5.1% |

## Memory

Peak process RSS includes loading and rendering, measured by `/usr/bin/time`. Its absolute value also includes runtime assets, libraries and driver allocations. The automatic path stayed close to the original. The largest median parallel increase was about 3.3 MiB on the larger half-scale GUI workload.

| Workload | Original peak RSS MiB | Auto peak RSS MiB | Parallel peak RSS MiB |
|---|---:|---:|---:|
| Warm static terrain | 231.8 | 230.4 | 230.9 |
| Animated terrain,1280x800 | 230.1 | 230.8 | 231.0 |
| Cold terrain first frame | 231.2 | 231.1 | 231.2 |
| Uncached terrain stress | 226.2 | 224.5 | 226.1 |
| GUI pan,1x | 231.4 | 231.0 | 231.8 |
| GUI pan,half-scale | 238.9 | 237.4 | 239.3 |
| GUI fractional,half-scale | 238.9 | 237.8 | 238.9 |
| Animated terrain,1854x1011 | 268.1 | 267.4 | 268.5 |
| GUI half-scale pan,1854x1011 | 267.9 | 267.1 | 271.2 |

## Gameplay under background contention

63 additional processes ran real simulation, scene extraction and drawing for 12 seconds at an external 60 FPS schedule. A separate Maxima simulation campaign began during the first rotated group, using roughly six to eight concurrent engine processes. We retained that group but excluded repeat 0 uniformly from this summary: six remaining matched trials per mode/workload. Load traces are in `host-load.jsonl`; before/after load observations are also retained per run.

These are uncontrolled background-contention observations, not isolated causal estimates of renderer responsiveness. All variants sustained roughly 59.4–59.6 displayed FPS. Tail results were mixed; parallel did not consistently improve displayed-scene age. Do not infer an overall smoother-gameplay improvement from them.

| Workload/mode | CPU ms/displayed frame | Displayed FPS | Draw/present p99 ms | Scene age p99 ms | Latest sim lateness p99 ms | Missed render deadlines |
|---|---:|---:|---:|---:|---:|---:|
| stationary / original | 6.524 | 59.44 | 12.030 | 38.0 | 2.667 | 0.14% |
| stationary / current-auto | 6.470 | 59.50 | 11.503 | 39.0 | 2.333 | 0.14% |
| stationary / current-parallel | 6.733 | 59.47 | 12.772 | 43.0 | 2.500 | 0.28% |
| panning / original | 9.418 | 59.53 | 14.839 | 45.0 | 2.167 | 0.91% |
| panning / current-auto | 9.408 | 59.57 | 14.021 | 44.0 | 2.333 | 0.49% |
| panning / current-parallel | 9.414 | 59.57 | 14.136 | 48.5 | 2.333 | 0.42% |
| panning-constrained / original | 9.279 | 59.52 | 14.789 | 41.0 | 2.667 | 1.05% |
| panning-constrained / current-auto | 9.260 | 59.57 | 16.234 | 43.5 | 2.333 | 1.47% |
| panning-constrained / current-parallel | 9.169 | 59.57 | 13.692 | 42.5 | 2.833 | 0.42% |

Normal cases used four compute threads and a rendering concurrency cap of two. Constrained cases used two compute threads and a cap of one. Interactive clouds and 1x GUI settings were the normal defaults; interactive does not apply the fixed-scene clouds/zoom/pause overrides. Process CPU here includes simulation and scene work. “Latest sim lateness” is the latest observed tick lateness sampled at display, not a per-tick lateness distribution. All tails/deadline fractions in this table are medians of per-run values.

The earlier interactive harness combined an external schedule and the internal frame pacer. The benchmark-only patches here disable the internal pacer and retain device admission, so the external schedule is the sole pacing authority. Earlier double-paced FPS observations should not be used as a cumulative comparison.

## Hardware accelerated checks

42 release processes: seven alternating original/current pairs for native Legacy GL, Programmable GL and SDL GPU, using paused half-scale GUI pan. These ran while the unrelated simulation campaign remained active. They do not establish a hardware-renderer speedup. Timers measure host submission/presentation calls, not GPU execution timestamps.

| Backend | Original frame median ms | Current frame median ms | Paired frame change | Paired process CPU change |
|---|---:|---:|---:|---:|
| legacy-gl | 6.040 | 6.049 | +0.1% | -0.7% |
| programmable-gl | 2.389 | 2.559 | +2.6% | +0.3% |
| sdl-gpu | 2.477 | 2.441 | -4.1% | +3.4% |

Backend identity was asserted from the actual renderer. `gpu` and `opengl` are legacy aliases in this repository; the formal matrix used explicit names. Startup shader/resource preparation was warmed for 30 frames before measurement.

## Work equivalence and correctness

- Same fixed 128x128 four-team save, reported 40 units/100 buildings; same exact runtime assets. Original export and preserved current export match all 7,470 files byte-for-byte.
- Every fixed-scene process checked that rendering preserved simulation checksum `2231453127`. Rendering-only comparisons use a frozen simulation, rather than differing tick counts.
- Terrain animation advances once per unpaused draw. Matching frame counts therefore match phases. Cache hits/rebuild counts and scene/camera inventories matched between variants. Normal animated 1280x800 measurements had 480 hits/960 rebuilds per 240 frames; larger-surface animated measurements had 960 hits/1920 rebuilds.
- Streaming temporary caches do not populate the outer cache counters. Matching 420 terrain-cache scope calls per 60 frames and candidate executor 2880 parallel batches/5760 jobs establish actual composition work.
- Worker and recorder counters confirmed actual parallel dispatch. Warm static terrain had no rebuilds or jobs, as expected.
- Fifteen software captures covered animated map, static map, native GUI pan, half-scale GUI pan and fractional half-scale GUI. Both current modes matched the original exactly: ten comparisons, zero differing pixels. GUI captures used 300 frames +30 warmup; map captures 240+30. Raw PNGs/hashes are included.
- Requested 1920x1080 windows were clamped by the native window manager to 1854x1011. The larger rows use those actual dimensions; every matched executable had the same size.

## Provenance, reproduction and limits

Linux x86_64, AMD Ryzen 7 6800H, native X11 hidden windows, AMD Radeon 680M (radeonsi/LLVM 20.1.2), GCC 13.3.0, SDL 3.4.16, existing pinned dependencies. AC power, performance CPU governor. Ordinary `scons release=1` compilation (`-O3`), rather than a fast-development build.

Candidate committed production sources match merged 2d5cf67e8; the build checkout is d2b4e5736, which differs only in documentation. Original renderer production sources are unchanged. The original Engine has benchmark friendship and a diagnostic lateness atomic/accessor; the shared measurement controls and distribution reporting are ported into its benchmark. Candidate changes are benchmark instrumentation only. Both patches are included.

Binary SHA256:

- Original: `c5f421f4fd301fff7a8263dd2f1baf1a7fb4e309e2949edfdadefb7a936467a3`
- Current: `cd113952ae51af2eaeded1705dab186ba8ccd5f637d7f2b2a8677e54ba2c5335`

Fixture SHA256: `26cc515a843c418ccc6af6d024398ee5ab1c6d535e6e01d1c53635cd881d3da4`.

Reproduction files and exact executed command/environment records are in the archive. Create separate checkouts at the two endpoint revisions, apply the respective `baseline-harness.patch`/`candidate-harness.patch`, and build each with the same compiler/dependency prefixes:

```sh
GLOB2_SDL3_PREFIX=/path/to/sdl3/prefix scons release=1 -j6 software-render-benchmark
```

Copy `reproduction/` into `original-checkout/artifacts/cumulative/reproduction/`. Set `GLOB2_BENCH_ORIGINAL`, `GLOB2_BENCH_CURRENT`, `GLOB2_BENCH_ASSETS`, `GLOB2_BENCH_FIXTURE`, and `GLOB2_BENCH_SHADERS` to the matching built binaries/export/save/generated shader directory. Select a native X11 display via `DISPLAY`, then run `measure.py` and `remaining.py` sequentially. Do not run benchmarks concurrently. `analyze.py`/`report.py` produce the summary after the result sets are complete.

Cold results exclude game/asset-loading startup; render-only results exclude simulation progression. The workload matrix covers one save/seed, one host and two actual resolutions. Windows/macOS/mobile/browser performance, different hardware, dense late games and real HD artwork were not measured. Native hidden-window presentation does not measure visible display/scanout response. Interactive and hardware-GPU results need a quiet-host rerun before making causal responsiveness/GPU claims. CPU cost is not a measured power/energy saving.

This is a dedicated evidence branch. No benchmark-only changes or generated evidence were added to master. Original/current executables and matching asset/shader inputs are also retained locally for further comparisons.
