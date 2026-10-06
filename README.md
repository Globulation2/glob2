Local / VM verification

- Tested commit SHA: `a1e2c59a392ef7523656fda2747c8d34ccdd65f7`.
- Base and integration: integrated master `7e54a3fc581d22d32e7b97a6aee76dbfeae58e9f`, including newer tiled-capture and terrain-composition changes. Master was fetched before final validation; subsequent `73c192619` changes map-generator version metadata/goldens, not the affected rendering components.
- Environment: Linux x86_64, Ubuntu GCC 15.2.0, native release `-O3`; Emscripten serial and pthread release builds (`-O2`). Native SDL 3.4.16 with the local X11 patch, using the explicit SDL and recording dependency prefixes shown below. Browser toolchain/dependencies are resolved by the repository build. See `revision.json` for OS details and executable/Wasm SHA-256 hashes; build logs contain dependency paths and flags.
- Coverage rationale: cache admission/retention, alpha-weighted coastline filtering, torus wrapping, terrain edits, cached/streamed equivalence, close-view density restoration, HiDPI limits, explicit offscreen raster scales, native/HD tiled captures, full torus rendering and picking. Presentation-only change: simulation algorithms/state, save formats and network/replay gates are unchanged, so no SIM_REVISION bump or cross-platform simulation replay matrix was needed. Terrain tests retain simulation checksum assertions.
- Native result: **22 passed, 0 failed, 0 skipped**, exit 0. Native OpenGL and portable SDL paths exercised. Browser results recorded below after the final sweep.
- Independent review found an offscreen-DPI defect; fixed by using the active raster scale and adding a regression for native/reduced captures at 1x/2x window DPI. The reviewer rechecked the correction and reports no remaining blocking findings; see `review.md`.
- Limits: no Windows/macOS/Android run, no Firefox/WebKit run, and no dedicated hardware browser performance measurement. Chromium uses SwiftShader. Timings are terrain-only diagnostics on a shared host, not game FPS; OpenGL waits for completion, while portable SDL only flushes commands. No separate human play session was performed. Distant terrain grain may be softer, and cold composition remains costly.
- Maintainer acceptance: the user explicitly authorized cleanup, independent review, feedback implementation and merge; the author accepts this focused evidence for the presentation-only change. Hosted cheap checks are separate from local engine verification.

Exact commands (all from the repository root):

```sh
GLOB2_SDL3_PREFIX=/tmp/glob2-terrain-sdl-patched/prefix \
GLOB2_RECORDING_PREFIX=/tmp/glob2-terrain-baseline-build/recording/prefix \
CCACHE=1 scons release=1 -j12 engine-tests
python3 test/run_tests.py --binary engine \
  --filter 'TerrainPresentation/*' --filter 'TerrainValidation/*' \
  --filter 'TorusRender/*' -j2 --artifacts artifacts/terrain-zoom/merge \
  --junit artifacts/terrain-zoom/merge.xml
CCACHE=1 scons target=web release=1 -j8
python3 browser/serve.py 8786 --bind 127.0.0.1 \
  --directory build/emscripten/client/release
node artifacts/terrain-zoom/merge-browser-check.cjs serial
node artifacts/terrain-zoom/merge-browser-check.cjs threaded
node artifacts/terrain-zoom/merge-browser-check.cjs serial 2
```

Final native fixture: deterministic 256x256 water/ice/grass stripes, 1152x896 drawable, zoom .25, translation (.375,.625), 145x113 tile bounds, camera (249,250), fixed presentation phase 19. One cold draw, then average of five warm draws. Both backends retained 80 pages, reported 74,453,120 bytes, and rebuilt no pages during the warm interval.

| Terrain draw | Cold ms | Warm ms |
| --- | ---: | ---: |
| OpenGL | 946.621 | 4.59284 |
| Portable SDL | 508.130 | 1.04635 |

The initial investigation reproduced the old cache rejection at approximately 442 ms/OpenGL and 466 ms/portable per warm terrain draw. Those observations predate the newer master composition changes, so they are retained as historical evidence rather than an isolated speedup measurement against today's master. See `initial-investigation/` for the original commands, hashes, timing files and images.

Final browser results: all three commands exited 0. Serial and threaded Chromium WebGL2 each completed 18 zoom steps and zoom-back; serial 2x-DPI completed six samples spanning 29%, 26%, 24%, 22% and 20%, then returned to normal zoom. All samples kept rendering in the requested runtime mode, with zero JavaScript page errors and zero WebGL errors. The 2x output was 2304x1792; the screenshot at 26% shows the crossfade. See each `merge-browser-*/check.json` and its screenshots. Native and browser images were visually inspected.

Files: `merge-tests.log` / `merge.xml` hold final native results; `merge/` holds screenshots, fixture files and timings. `merge-*-build.log.gz` are complete integrated build logs, and `merge-*-final-build.log` confirm the final source was rebuilt/up to date. `revision.json` identifies the tested source and executable/Wasm hashes. `review.md` contains the independent finding and follow-up assessment.
