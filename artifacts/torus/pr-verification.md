# PR #673 local verification

Tested head: `24053a2fdec8c046236f5c74cbd85b36ef29c87f`.
Integrated base: `a4a9233f3c10768eaff4f821bb32352df00edcc7`, including the shared camera zoom / strategic detail changes. Feature branch includes merge commit `25a13e371`.
Master subsequently advanced to `ddb1fad60` (statistics catalog). Its overlap was reviewed: GameGUIDraw changes affect statistics pages, GameGUI adds a statistics helper, and the source list adds two statistics translation units. It does not change capture, map rendering, camera, GL lifecycle or browser torus behavior. `git merge-tree --write-tree HEAD origin/master` succeeds; no additional rebase was made. This later statistics revision was assessed from source, not rebuilt with this branch.

Environment: Linux 7.0.0-31-generic, x86_64, GCC 15.2.0 (Ubuntu 15.2.0-16ubuntu1), native SDL 3.4.16 at `/tmp/glob2-sdl3/prefix`, Mesa llvmpipe LLVM 21.1.8. Emscripten 4.0.15 repository-managed SDK, Node 22.22.1; release serial and threaded WebAssembly builds. Playwright 1.63.0 Chromium with ANGLE SwiftShader. Xvfb supplies native display fixtures. Software GPUs on a shared machine establish correctness here; these timings do not predict hardware GPU performance.

## Commands and results

All commands run from the repository root, except Playwright commands from `browser/`. Build logs retain full compiler commands and flags. Both builds exit 0.

```sh
GLOB2_SDL3_PREFIX=/tmp/glob2-sdl3/prefix scons release=1 opengl=1 -j8 tests torus-render-benchmark
scons target=web release=1 -j8
python3 test/run_tests.py --binary unit --filter 'Torus*/*' --filter 'MapRenderGeometry/*' --filter 'FogFade/*' --filter 'CloudField/*' --filter 'ZoomDetail/*' --junit artifacts/torus/integration-unit.xml
env -u DISPLAY -u WAYLAND_DISPLAY -u XDG_RUNTIME_DIR python3 test/run_tests.py --binary engine --filter 'TorusRender/*' --filter 'HighResolutionIntegration/*' --filter 'PortableRenderer/*' --filter 'MapRenderResize/*' --filter 'SceneExtract/smooth unit motion*' --jobs 2 --display-jobs 1 --timeout 300 --junit artifacts/torus/integration-render.xml
python3 browser/serve.py 18770 --bind 127.0.0.1 --directory build/emscripten/client/release
# From browser/:
GLOB2_TEST_URL=http://127.0.0.1:18770 GLOB2_CHROMIUM_ANGLE=swiftshader GLOB2_TEST_RENDERER=webgl2 npx playwright test tests/torus.spec.js --project chromium
GLOB2_TEST_URL=http://127.0.0.1:18770 GLOB2_CHROMIUM_ANGLE=swiftshader GLOB2_TEST_RENDERER=webgl2 GLOB2_TEST_ENTRY_PATH='/?threads=serial' npx playwright test tests/torus.spec.js --project chromium
```

CPU: 26 cases pass (7 jobs), exit 0. Native integration: 13 cases pass, exit 0. Threaded Chromium: all 3 cases pass, exit 0. Serial Chromium: all 3 cases pass, exit 0.

CPU coverage checks texture/viewport limits, cell alignment, padding, partial tiles, periodic UV clipping, attribute interpolation, perspective equivalence, geometry and picking, fog, clouds and shared zoom detail.
Native GL coverage forces nine textures on a small fixture and compares every interior and gutter pixel with ordinary rendering at native scale (maximum channel difference <= 1/255). Fixtures include terrain detail, buildings, explorer sprite overhang, fog bands, unknown terrain, previews and markers. Preview phase advances once across all captures; paused animation, cloud and area phases remain frozen. Injected partial allocation failures exercise cleanup, half-resolution retries, one texel per cell retention, reset recovery and same-frame 2D fallback. Existing native cases cover shared zoom, strategic detail, panning, picking, unfolding, returning to 2D, rectangular maps, HD artwork, triple UI scale, resizing and context recreation. Chromium exercises settled torus toggles, context loss/restoration and software renderer fallback in both runtimes. Native and browser screenshots are stored alongside these logs; overview fixtures intentionally retain fog.

## Benchmarks

20 measured frames following 3 warmups per rendering mode; synchronized rendering via glFinish; rendering only, no HUD/simulation/frame pacing. Native software GPU llvmpipe, map viewport 1120 x 800, launch window 1280 x 800. Benchmarking overlapped other validation/build work on this shared machine, so wall times are illustrative, not a performance acceptance threshold or an old/new speed comparison. No deliberate graphics setting changes.

```sh
xvfb-run -a -s '-screen 0 1280x800x24 -noreset' env -u WAYLAND_DISPLAY -u XDG_RUNTIME_DIR SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=dummy GLOB2_USER_DATA_DIR=$PWD/artifacts/torus/integration-bench256 GLOB2_BENCH_MAP=maps/Oazis.map GLOB2_BENCH_FRAMES=20 GLOB2_BENCH_WARMUP_FRAMES=3 build/linux/client/release/test/torus-render-benchmark -g -F -m -s 1280x800
xvfb-run -a -s '-screen 0 1280x800x24 -noreset' env -u WAYLAND_DISPLAY -u XDG_RUNTIME_DIR SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=dummy GLOB2_USER_DATA_DIR=$PWD/artifacts/torus/integration-benchrect GLOB2_BENCH_SIZE=64x128 GLOB2_BENCH_FRAMES=20 GLOB2_BENCH_WARMUP_FRAMES=3 build/linux/client/release/test/torus-render-benchmark -g -F -m -s 1280x800
```

| Map | Mode | Median ms | p95 ms | Texture allocation |
| --- | --- | ---: | ---: | --- |
| Oazis 256 x 256 | 2D, no clouds | 5.579 | 6.377 | — |
| Oazis 256 x 256 | Torus, no clouds | 118.160 | 132.513 | 25 textures, 289,816,576 bytes (276.4 MiB), 32 pixels/cell |
| Oazis 256 x 256 | 2D, clouds | 14.845 | 17.148 | — |
| Oazis 256 x 256 | Torus, clouds | 193.961 | 199.787 | Same native allocation |
| Checkerboard 64 x 128 | 2D, no clouds | 2.855 | 3.935 | — |
| Checkerboard 64 x 128 | Torus, no clouds | 26.915 | 31.357 | 6 textures, 37,322,752 bytes (35.6 MiB), 32 pixels/cell |
| Checkerboard 64 x 128 | 2D, clouds | 14.568 | 16.609 | — |
| Checkerboard 64 x 128 | Torus, clouds | 55.027 | 68.733 | Same native allocation |

## Limits

Windows, macOS, Android, hardware GPUs, Firefox and WebKit were unavailable/unexecuted. No complete native `opengl=0` build or full engine suite was run. Native software rendering and the browser software path were exercised. Simulation, saves and network contracts are unchanged; simulation determinism/save continuity/network suites were not rerun for this presentation-only change. No SIM_REVISION bump.

Hosted PR cheap contracts passed; expensive hosted checks were intentionally not requested. They do not substitute for the local results above. Final tested revision has no observed test failures. An earlier pre-integration threaded lobby-start timeout and successful rerun are retained locally; final rebuilt threaded cases all pass.

Evidence is published on a dedicated evidence branch, outside the feature/master source tree. Codex prepared the evidence and proceeds under the user's explicit instruction to commit, push and merge. No additional human gameplay review is claimed.
