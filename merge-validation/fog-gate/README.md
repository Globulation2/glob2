# Local gate after smooth-fog integration

Source: e7e49ebed8f4f0fc1d45e1d98b1435eb0dfef04f, integrated with master
1d6b8ca09fb6ce5a237303751e3b949d444488c2. The maintainer explicitly authorizes
merging on passing relevant local tests while hosted CI is unavailable.

Environment: Linux 7.0.0-31-generic, x86-64; GCC 15.2.0, SDL 3.4.16,
native release C++20/O3, the existing isolated SDL SDK. Exact compiler/linker
flags and source provenance are in the build and test logs. Software OpenGL
runs use Xvfb; LP_NUM_THREADS=2 bounds the native rendering worker count.

The release game, unit/engine harnesses, previews and online probes build.
The final incremental build incorporates the white-GL-color restoration and
records the exact clean source revision. All 658 unit cases pass. The first
full run exceeded the existing fertility-cache CPU budget; an isolated retry
and then the complete suite passed without changing the threshold or code.
Both initial and successful results are retained. All 83 selected engine cases
pass with no skips, covering Scene/fog drawing, map repetition/save-load/golden
checksums, farm-area AI/editor/script integration, multiplayer recovery/order
validation, recording, replay, settings and online services.

The opacity diagnostic verifies cache-only composites across opacity values,
with zero draw calls for invisible meshes/shadows. Pixel readback checks 9,796
mesh pixels: half-opacity blends differ from the expected result by at most
1.063/255. The 53,708 shadow-only pixels differ by at most 0.588/255. Both
zero-opacity images are exactly the background. The verifier and all six PNG
captures are retained.

Four 180-frame classic/skinned comparisons pass: normal, overview, 5x zoom,
and adaptive detail disabled. Rendering never changes simulation state and
classic/skinned states match per frame. Steady geometry/raster counts are zero;
overview composites are zero. Normal and detail-disabled modes composite
73,556 poses over 148 measured frames. Classic images are pixel-identical to
the map-repetition integration in every configuration. Skinned images are also
identical except normal (82 of 480,000 pixels differ, at most 20/255). Captures
are retained; timings under concurrent builds are smoke evidence, not new
matched performance claims. Original paired profiling is retained separately.

The live API lifecycle probe passes. Build contracts: 291 cases, three
existing environment-dependent skips, no failures. Translation audit: zero
structural errors. Platform files remain unchanged from 3de565d65; studio-gate
contains 438 platform tests (six existing skips), lint/typecheck, six designer
cases, four accessibility combinations and the production web build.

Both serial/threaded release browser runtimes build. All 11 browser cases pass:
eight skin/asset checks across Chromium serial/threaded, Firefox and WebKit;
two 1,500-tick pacing/checksum and paused-input comparisons; one signed online
replay using the seeded real API. The restored Chromium capture was visually
inspected. All final browser runs exit cleanly with unchanged assertions.

Final fetch finds master 138976153, adding separate win-probability HUD and
optional victory features. Shared-file additions were reviewed: no mesh/fog
rendering or skin-protocol changes. The merge tree is conflict-free
(ac6f8ee0630809bd80b41e7da3feb37c6c3507d4). The binaries above validate the stated
head and base, not a newly compiled merge result with these unrelated additions.

Replays are losslessly gzip-compressed; BMPs are converted to PNG. Commands,
match records, checksum traces, logs and captures are accessible in this folder.
Windows/macOS, hardware GPU performance and real Stripe checkout/refund remain
unverified locally. Native mobile retains the classic renderer fallback.
