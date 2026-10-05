# PR #756 local verification

- Tested commit: `800c1dd4a47f07cfa09576ba4cab1df73d1c1fb0` (clean committed feature tree).
- Feature commit: `c68b02c78`; integrated base: `42df42802`.
- Fetched and integrated master `42df42802` before final validation, including its SDL 16-bit PNG fallback decoder patch. Refreshed the native dependency prefix and both browser SDK builds before rerunning tests.
- Environment: Linux-7.0.0-31-generic-x86_64-with-glibc2.43, x86_64; GCC 15.2.0; SDL3 stack pinned by scons/sdl3_dependencies.py; Emscripten 4.0.15; Playwright 1.63.0 Chromium; native Xvfb OpenGL/software and browser software plus forced WebGL restoration with SwiftShader.
- Assets: pinned Pillow 12.2.0 exporter. Reused completed content-keyed WebP cache entries; exporter verifies recipe, source/output hashes, dimensions, alpha and required lossless pixels on every cache hit.

## Build commands

```sh
python3 scons/sdl3_dependencies.py --prefix /tmp/glob2-fps-deps/prefix --work /tmp/glob2-fps-deps/work --jobs 8
GLOB2_SDL3_PREFIX=/tmp/glob2-fps-deps/prefix scons -j12 release=1 server=0 tests build/linux/client/release/src/glob2
scons target=web release=1 -j8
```

Native client and both browser runtimes built successfully (exit 0), then refreshed successfully after the final SDL decoder integration. Final build logs have `-sdl-final` in their filenames.

## Exact native test orchestration

Executed the following commands; final commands exit 0. The initial ImageAssets registry invocation exposed the inherited subcase-reporting issue described below; that suite was rerun directly.
The script below records the exact argv and log destinations:

```python
import subprocess
from pathlib import Path
commands = [
 ('review-engine-final.log', ['python3','test/run_tests.py','--binary','engine','--filter','ScreenExecution/*','--filter','SettingsGraphics/*','--filter','EngineSession/*','--filter','Settings/*1000x700*','--filter','Settings/render FPS*','--filter','TurnEngineHarness/the committed match record*','--filter','ThemeCatalog/*','--junit','artifacts/render-fps/review-engine-final.xml','--display-jobs','1']),
 ('review-unit-final.log', ['python3','test/run_tests.py','--binary','unit','--filter','RenderFramePacer/*','--filter','PerformanceTelemetry/*','--filter','SpriteSheets/*','--junit','artifacts/render-fps/review-unit-final.xml']),
 ('review-image-assets-direct.log',['build/linux/client/release/test/glob2-unit-tests','--test-suite=ImageAssets']),
 ('review-thread-equivalence.log',['python3','test/check_sim_thread.py','build/linux/client/release/src/glob2','--baseline','build/linux/client/release/src/glob2','--candidate-env','GLOB2_SIM_THREAD=1','--output','artifacts/render-fps/review-thread-equivalence']),
 ('review-native-reference.log',['python3','test/run-browser-determinism.py','build/linux/client/release/src/glob2','artifacts/render-fps/review-native-reference']),
 ('review-sim-version.log',['python3','test/check_sim_revision.py','--base','origin/master']),
 ('review-ci-policy.log',['python3','test/build_system/test_ci_policy.py']),
 ('review-scene-boundary.log',['python3','test/build_system/test_scene_boundary.py']),
]
failures=[]
for name, command in commands:
 print('Running',name,flush=True)
 with open(Path('artifacts/render-fps')/name,'w') as log:
  result=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT)
 print(name,'exit',result.returncode,flush=True)
 if result.returncode:failures.append(name)
raise SystemExit(bool(failures))

```

21 engine cases and 12 unit cases passed through the registry; five ImageAssets cases (299 assertions) passed directly. The initial registry run failed because current master changed the PNG16 test to named doctest subcases, while the strict registry expects its parent name. This is an inherited reporting/inventory mismatch, not a failed decoder assertion. Failed runner logs and the successful direct rerun are retained. Settings defaults, missing/legacy files, malformed values, all presets, Unlimited and graphics-preset independence passed. Pacer tests cover long-term rate, minor jitter, expensive frames, missed intervals, live changes/reset and synthetic refresh combinations above/below target. Screen execution covers skipped painting, input/updates and child admission/completion. Settings screenshots show desktop software/OpenGL and compact layouts. Integrated artwork/theme compatibility cases passed.

Native 128-tick checksums match for 25/60/120/Unlimited in serial mode. Serial/threaded equivalence separately passes new-game, new-game-maxima, generated-load, legacy-v121 and resume; checksum sidecars, replay bytes and final saves match. The committed multiplayer match record verifies unchanged, and the simulation revision check passes without a revision bump.

## Browser command

Local server: `python3 browser/serve.py 8897 --bind 127.0.0.1 --directory build/emscripten/client/release`.
From browser directory:

```sh
GLOB2_TEST_URL=http://127.0.0.1:8897 GLOB2_TEST_RENDERER=software GLOB2_CHROMIUM_ANGLE=swiftshader npx playwright test render-fps.spec.js pacing.spec.js viewport.spec.js settings-storage.spec.js rendering.spec.js replay-save.spec.js --project=chromium --workers=1 --grep 'render FPS selection|painting does not dispatch|display-paced match|menus follow the viewport|a running match survives resize|editor dialogs|small viewports|settings wait for durable storage|settings can continue after failure|WebGL context restoration|end-game replay save remains' --output ../artifacts/render-fps/review-browser-results
```

All 14 Chromium cases passed (exit 0). The DOM input regression, desktop/compact FPS selection and persistence, serial/threaded gameplay, paused cadence ceilings, viewport changes, storage failures, replay save/load and repeated WebGL context recovery passed. Both hosted browser traces are byte-identical to the native 1500-tick reference.

Browser hosted game traces at 25 FPS (serial) / 120 FPS (threaded) are compared against the same 1500-tick browser CLI trace. The native reference SHA256 is `4838173acd91d4768a0cd7f3e681cb3ea630de093e27c117994030a8ad31de43`. Final cross-platform trace comparison is recorded alongside browser results. Cadence sampling is performed while paused, which retains the existing 40 ms redraw suppression; it establishes the ceiling, not full rendering throughput.

## Review and limitations

Independent subagent review identified DOM action dispatch during painting; fixed and covered by a regression. Its second review reported no remaining actionable issues. Self-review also cleaned callback ownership, pacing comments, dead frame state and browser timer/deadline independence. See review.md.

Only Linux x86_64 and Chromium were run. Windows, macOS, Android/mobile hardware, Firefox, WebKit, physical high-refresh displays, live relay multiplayer, recording output and the full repository/hosted expensive matrix were not run. Injected-clock tests cover refresh combinations rather than physical displays. Native all-cap checksum coverage is serial; browser interactive coverage is 25 serial and 120 threaded; the serial/threaded equivalence checks are headless. No claim of interactive threaded equivalence at all four caps is made. Offline exports/benchmarks remain uncapped by construction; no performance benchmark is claimed.

User explicitly authorized committing and merging after review/cleanup. Local evidence is accepted for this merge within that authorization; platform and live multiplayer limitations remain listed above.
