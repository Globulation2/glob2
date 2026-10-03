# Current master integration validation

Source: f4777296b3893ed574ca04126637d52e8f4a3db3, including master af7432520.
Native game, unit/engine harnesses, skin-game-preview, transport-test and
online-screens-probe built with the local SDL3 SDK and GCC release configuration.
All 607 unit cases, 70 selected engine cases and the live local API client
lifecycle passed. Engine selection: SceneExtract/*, *Render*, Settings*, Turn*,
Replay*, Online*. The build-contract suite passed 258 tests, with 3 environment
skips. JUnit and logs are adjacent.

The crowded render harness used the retained initial.game.gz and current
exported unit meshes, 800x600, 488 additional units, 180 total frames with
32 warmup frames. Both modes preserved all 180 simulation checksums. Steady
skin geometry and raster calls remained zero. This is a correctness/cache
check on a busy host, not a new matched before/after performance benchmark.

Current browser builds and final hosted verification are recorded separately
when complete. Earlier evidence predates this YOG-cutover merge.

Serial and threaded WebAssembly builds and packaging passed. Both browser skin tests passed in serial Chromium (44.8s) and threaded Chromium (83.2s): live WebGL2 mesh rendering, context recovery, and no optional-mesh requests for unskinned play. Browser logs and current render captures are adjacent. Hosted verification remains pending.

Firefox passed both current skin runtime checks (44.5s), as did WebKit (40.4s). The first WebKit launch failed before game execution because this host lacks libevent-2.1.so.7. The retry used the existing isolated wrapper and extracted runtime libraries under artifacts/skins/browser-libs; no system libraries or shared browser cache were changed. Both launch failure and passing retry logs are retained.
