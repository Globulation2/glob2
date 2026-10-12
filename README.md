# PR #1148: CPU rendering verification

Feature PR: https://github.com/Globulation2/glob2/pull/1148

Final feature head: `217d10be744d3442429bcb71cbff53415ba704c9`.
Integrated master: `9b853c4a3dcb2647b8828b77bb48faf5251a3de5`.
Native suite binaries were built at `a7515f44ad928408910241f4c6d125912f425fd3`; the final follow-up changes documentation only. Full executable-source hashes and dependency/build metadata are inside the archive. Original independent references use `3510b4c4ee860428561d561b959e6c89e96c4b37`.

Download [verification.tar.gz](verification.tar.gz), verify its [SHA-256](SHA256SUMS), and extract it. `render-parallel/files.json` hashes every included input/result. Evidence contains no native executables, object files, compiler caches or contributor profiles.

## Native validation

Linux x86_64, GCC13.3, SDL3.4.16; cached fast debug `-O0 -g1` for contracts/integration and ordinary release `-O3` for performance. Generated SPIR-V shaders use pinned glslang15.1.0. Exact build/test commands, binary hashes and coverage inventory are in `final-metadata.json`, `integrated-tests-commands.json` and retained logs.

- Full unit suite: **1102 cases,0 failures,0 skips**.
- Integrated parallel engine coverage: **226 cases;224 passed**. The unchanged stock version152 replay fixture is rejected by master's minimum replay version154. The other initial failure was missing runtime shader configuration; corrected serial/parallel memory-capture runs each passed all4 cases, including real SDL GPU capture, simultaneous recording and cancellation.
- Serial terrain/software coverage: **82 cases;81 passed**; the same shader setup failure was corrected by the serial4-case rerun.
- Native backend-pair refresh with runtime shaders: **1 case passed**, preserving camera and simulation across available software, OpenGL and SDL GPU transitions.
- Active CPU pixel borrowing and injected memory-capture partial failure/discard/recovery: **2 cases passed**.
- The hostile-order fuzz case initially exceeded120s during compilation contention; integrated rerun passed in131s with300s headroom. Both logs retained. No unresolved failure introduced by this PR.

Saves, replays, per-tick checksums, multiplayer verifier records, captures and recording videos are included beside integration results. Recording and fixed-input rendering-ceiling tests preserve simulation checksums. Historical O0 farming-budget failures have original-source standalone evidence; the final full unit suite passed.

## Independent comparisons and review

Three sub-agents reviewed executor scheduling/lifetimes, terrain publication/admission, and raster ordering/ownership. Fixes include portable release of callback captures outside locks, bounded weak material sharing, reservation rollback and retained-capacity accounting, explicit CPU pixel fences, and failed-frame restoration. All final findings were addressed.

- Terrain original/owner/owned oracle:528 composition comparisons,8,682,058 assertions,0 failures;3,784,704 pixels per path and1,720,320 mask samples, including synthetic128px sources.
- Raster original/current reference:3600 random matched blit/fill pairs, exact pixels, ASan/UBSan; all supported arithmetic, clipping and wide scratch cases. Seven O3 paired probes show median totals48.027→48.697ms for3600 blits and9.878→9.864ms for3600 fills; these are kernel totals, not frame timings.
- Terrain O3 extraction comparison:36 cases ×10 rotated rounds, exact pixels; owner paired medians0.944–1.058. Worst observed synthetic128px cached-hole case costs9.664→10.194us/cell (+0.530us,+5.8%). Mixed mask-building cases approximately0.99–1.00. This small worst-case extraction cost is explicit; shared exact kernels and conservative automatic execution are retained. Setup, snapshots, cache planning, installation, opacity, executor and presentation are excluded from this microbenchmark.
- Executor ASan/UBSan:27 reservation checks,800 batch iterations,50 controlled fairness iterations,50 abandonment barriers and3 retained shutdown tickets;100 reentrant capture-deleter cases pass.
- Emscripten4.0.15 through Node24.19.0:serial45 checks and pthread49 checks, matching checksum112128. This covers executor contracts, not browser game rendering.

## Release measurements

`benchmark-summary.csv/json` reports medians of3 rotated trial statistics; percentile rows are not pooled percentiles.54 successful processes cover cold, animated warm caches, streamed composition, zoom/pan, interactive contention and static warm caches. Commands record fixture hash, viewport1280x800,4 compute workers, rendering cap2 and host load. Captures match exactly across auto/serial/parallel in all5 renderer capture workloads.

Cold serial→parallel composition batch21.757→17.275ms and draw65.244→61.314ms. Animated median draw4.479→3.900ms; streamed24.677→19.263ms. Static warm caches record720 hits,0 rebuilds and0 jobs per120 measured frames. Zoom/pan serial→parallel median draw7.700→7.561ms, while immediate auto is6.719ms: recording/copy overhead does not justify default raster activation. Interactive draw/present p99 auto/serial/parallel10.170/10.821/9.376ms, scene-age p9929–31ms and sampled simulation-lateness p990.667ms. These single-host results do not establish portable thresholds or guarantee smoother gameplay.

Visible/interactive benchmarks used6fc3f0881; final static-paused workload useda7515f44a. Only the benchmark's paused-view option and its default-false forwarding changed between those revisions; production rendering inputs are unchanged. Independent original sources cover extraction arithmetic and kernel cost; an untouched original whole-renderer performance executable was not retained.

## Limits and acceptance

Automatic execution remains immediate and serial. Worker terrain groups and software-backend tiles are experimental development overrides; ordinary native-size immediate surface drawing remains serial. No new runtime dependency, simulation rule, save format or sim revision is introduced.

Actual game integration was executed on Linux x86_64 only. Windows, macOS, mobile and actual browser gameplay/checksum comparisons were not run. Node WASM probes cover only the executor. TSAN could not initialize (`unexpected memory mapping`); ASan/UBSan and stress are not a race-freedom proof. Real HD asset packs were unavailable; synthetic128px inputs cover native stepping. Automated interactive/capture inspection does not replace maintainer play testing.

Author acceptance: the linked evidence is sufficient for the bounded experimental implementation with these limits; automatic selection remains serial. Hosted expensive platform CI was not requested and is not claimed by cheap checks.
