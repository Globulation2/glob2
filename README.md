# PR 500 rendering evidence

Renderer baseline: master `98301f7cb`. Complete PR: `fc7bff228`.
The baseline renderer and engine sources are untouched. `master-harness.patch`
only adds identical configurable warmup, fixed presentation phase and cold sample
logging to its existing benchmark. Master moved to `9fad29c60` during testing;
the measurements intentionally retain the pinned baseline for reproducibility.

Apple M3 native desktop OpenGL, optimized production SCons builds. Loaded
checkpoint: eight Maxima AIs, symmetric-arena generator15, mapseed42, gameseed19,
256x256 tiles, saved tick20000. Stress population is supplemented to 3000, then
advanced 100 ticks: 2518 live units, 2441 mapped, tick 20100, checksum ffd0609c.
This is a supplemented real AI checkpoint, not3000 naturally grown units.

HD artwork, health/food bars, full reveal, no clouds.1024x600 logical window,
864x600 map viewport,2048x1200 framebuffer, zoom0.0732421875. Pause presentation
at water phase22. Each process renders 600 warmups and 80 measured frames.
All PR steady samples finish with pendingTextures=0. No AI advances during the
primary render measurements. Timing includes map drawing, clear and glFinish;
it excludes simulation and presentation, so these are not whole-game frame times.

Runs execute sequentially in order master1,PR1,PR2,master2,master3,PR3.
Concurrent host activity varied; host-load-before.txt records an example.
No samples or runs are discarded. Summary uses median of the three per-process
medians (and, for p95, median of the three per-process p95s), not pooled samples.

| Run | Master CPU median ms | PR CPU median ms | Master elapsed median ms | PR elapsed median ms |
|---|---:|---:|---:|---:|
|1|44.560|18.603|40.124|19.186|
|2|31.371|18.805|30.866|19.393|
|3|30.256|16.134|29.883|17.218|

CPU summary 31.371→18.603ms,40.7% reduction. Elapsed summary30.866→19.186ms,
37.8% reduction. Elapsed p95 summary32.434→21.470ms,33.8% reduction.
Draw calls 25126→10448,58.4% reduction. All six renders preserve checksum ffd0609c.
See master-results.json and individual logs for raw numbers.

master.png and pr.png are pixel-identical (all 7,372,800 RGB channels match).
master-vs-pr.png presents them side by side. Fixed diagnostic comparison:
48 exact RGBA pairs. Active comparison: 102 AI ticks, eight zoom levels and wrapped
pans, maximum channel difference 1 with 25 differing channels across all 102 pairs;
final tick 20202 checksum 6ae27081. Both sides in each pair render identical state,
and every render asserts the simulation checksum is unchanged. The diagnostic
reference retains the PR resource batching and varying shader and is NOT the
master baseline. Clouds smoke: both modes preserve their own initial checksum;
the ten advancing diagnostic pairs are exact. Art/simulation feel must still be
reviewed by a maintainer playing the build.

14 targeted display regression cases, 429 assertions pass. Includes queue ordering,
wide clip-edge lines, texture mutation/deletion while diagnostics are disabled,
cold deferred copies, bounded geometry preparation, selective invalidation,
exception fallback and portable resource pixels. See display-tests-final.log.
Affected native and no-OpenGL modules compile; no-gl logs are included.
Physical low-end phone performance and the 5 ms goal remain unverified/not achieved.

First-use elapsed frames remain costly: master403–746ms, PR365–1014ms. These
include initial HD source uploads/driver work and are not steady-state samples.
New cache work is limited to 8 source-copy attempts and 16 geometry attempts per
scene, each with a separate soft 2 ms budget. An indivisible driver call can exceed
that ceiling. 64-layer pages bound allocation size, and unsupported/budget-limited
work draws through the original path. Source textures remain resident. Additional
array payload in this fixed scene 25,160,704 B, geometry 5,233,920 B; source payload
255,762,720 B. CPU metadata/driver overhead additional. Maximum cache payload caps:
64 MiB arrays plus 32 MiB geometry. These optimizations are desktop-only.

## Reproduce

Create separate checkouts at the revisions above. Apply master-harness.patch only
to the master checkout. In master:

```
scons --build=artifacts/render-master-build release=1 server=0 -j4 torus-render-benchmark
```

In the PR checkout:

```
scons --build=artifacts/render-production/build release=1 server=0 -j4 unit-tests torus-render-benchmark
```

Then from this evidence directory:

```
python3 reproduce.py --master-root /path/to/master --pr-root /path/to/pr
```

Exact original environment, working directory, revision and executable SHA256
are in each command JSON. The reproduction script relocates profile/save/capture
paths. Use an isolated profile. Asset data must match its checkout. The checkpoint
and preferences are included; SHA256SUMS covers all evidence files. Benchmark
logs retain original output, including saved Maxima strategy configuration.
