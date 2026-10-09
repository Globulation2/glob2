# Development build series: local verification

Code revision: 1cdcaf845 (full SHA in provenance.json). Base: d912223f4bf6c76961e84c3698b4d44884daea2a. Final integration inspection fetched master c26a0a02a and merge-tree completed without conflicts. Five touch UI objects compile against the combined tree, covering master changes that include the cleaned central headers. The mobile guide changes are in a separate UX section. The latest master browser change only repairs ImageAssets expected-case inventory; executing that complete browser harness remains outstanding. No merge or rebase was required.

This is a draft series. The implementation spans all requested build environments; local evidence covers Linux and Android x86_64, with browser validation recorded separately. It is not all-platform release qualification.

## Commands

Native builds used GLOB2_SDL3_PREFIX=/home/bradley/glob2-verify/integrator/sdl3/prefix and GLOB2_RECORDING_PREFIX=/home/bradley/.local/share/glob2/development/dependencies/build-6883bfc317f7295117be6c6a/prefix. Both are pinned pre-existing SDK inputs. The former was subsequently found to lack the current PNG patch; corrected image/display tests use the freshly built pinned SDL SDK via LD_LIBRARY_PATH=artifacts/dev-build/sdl3-prefix/lib. Logs retain the old-SDK failures.

```sh
python3 tools/dev_build.py -j24
python3 tools/dev_build.py -j16 tests
python3 tools/dev_build.py -j8
python3 tools/dev_build.py pch=1 unity=1 -j16
python3 test/test-dev-compile.py
GLOB2_TEST_CXX=/home/bradley/.local/share/glob2/development/toolchains/emscripten/Linux-x86_64-88a621bc131cd4954413aa39/upstream/bin/clang++ python3 test/test-dev-compile.py
python3 test/test-ccache.py
"$(python3 tools/package_assets.py --encoder-python)" -m unittest discover -s test/build_system -v
python3 -m unittest discover -s test/build_system -p test_dev_build.py -v
node --test browser/unit/*.test.js
python3 tools/dev_build.py target=web compile_commands.json
python3 tools/dev_build.py target=web pch=1 unity=1 compile_commands.json
python3 tools/dev_build.py target=web -j8
python3 mobile/dependencies.py --target android --arch x86_64 --jobs 8
python3 tools/dev_build.py target=android arch=x86_64 -j8
python3 tools/dev_build.py target=android arch=x86_64 pch=1 unity=1 compile_commands.json
python3 scons/sdl3_dependencies.py --prefix artifacts/dev-build/sdl3-prefix --work artifacts/dev-build/sdl3-sources --jobs 8
python3 test/run_tests.py --build-dir build/linux/client/debug/dev-dev_fast-true-linker-auto --quick --no-display -j8 --junit artifacts/dev-build/native-tests.xml
LD_LIBRARY_PATH=artifacts/dev-build/sdl3-prefix/lib python3 test/run_tests.py --build-dir build/linux/client/debug/dev-dev_fast-true-linker-auto --binary unit --filter 'ImageAssets/*' --no-display -j1 --junit artifacts/dev-build/image-retry.xml
LD_LIBRARY_PATH=artifacts/dev-build/sdl3-prefix/lib xvfb-run -a python3 test/run_tests.py --build-dir build/linux/client/debug/dev-dev_fast-true-linker-auto --binary engine --filter 'PortableGame/*' --filter 'GameplayRecording.Integration/*' --filter 'MusicSet/*' -j1 --timeout 300 --junit artifacts/dev-build/native-display.xml
build/linux/client/debug/dev-dev_fast-true-linker-auto/src/glob2 --verify-match "$PWD/test/fixtures/multiplayer/FourSquares1.g2mr" --map "$PWD/maps/FourSquares1.map.gz" --out "$PWD/artifacts/dev-build/match-valid"
cmp artifacts/dev-build/match-valid/checksums.txt test/fixtures/multiplayer/FourSquares1.verify-trace.txt
python3 test/check_javascript.py build/linux/client/debug/dev-dev_fast-true-linker-auto/src/glob2 --output artifacts/dev-build/javascript
```

`dependency-reuse.json` records two disposable shared Git clones at revision 1599a2ec4 (later changes only add benchmark metadata, document browser testing repair the cheap test fixture and extend cache/debugger tests) running `python3 tools/dev_build.py -j8 dev-dependencies` against the same managed store, without an explicit recording prefix. Separate logs distinguish seeding from reuse. The native cache fixture also uses two distinct disposable checkouts, verifies warm reuse/header invalidation, and checks relative DWARF source paths. GDB batch source lookup passes in both cached checkouts and the actual fast game executable; this checks source resolution, not full variable inspection.

## Measured scope

`relay-benchmark/results.json` contains all 36 individual samples and medians: three runs per six scenarios for ordinary and fast native relay builds. Command:

```sh
GLOB2_SDL3_PREFIX=/home/bradley/glob2-verify/integrator/sdl3/prefix python3 tools/benchmark_build.py --scons-arg role=relay --scons-arg=-j8 --source src/relay/RelayConfig.cpp --header src/relay/RelayConfig.h --output artifacts/dev-build/relay-benchmark-verified
```

This run used a frozen dirty candidate snapshot based on d912223f4, before the staged commits. It is not a clean final-commit benchmark. The relay does not link recording; its dependency scenario measures configuration/probes with an explicit pre-existing SDL prefix, not first-use codec setup. Archive work and libgag object timing are not fully instrumented in this snapshot; total process wall time is authoritative. GNU time peak RSS is maximum per process, not aggregate parallel memory. Runs shared a busy host; there are no CI queue/runner-minute savings claims. Full game, mobile, browser, PCH and unity performance measurements remain outstanding.

| Scenario | Ordinary median seconds | Fast median seconds |
| --- | ---: | ---: |
| Dependency/configuration setup | 9.33 | 7.43 |
| Uncached relay | 100.12 | 69.15 |
| Warm cache | 5.40 | 4.88 |
| No change | 1.26 | 1.16 |
| One source edit | 5.90 | 5.50 |
| Relay central header edit | 69.52 | 64.84 |

All six no-change samples record zero compile and link actions. `header-profile/results.json` and individual logs measure isolated HiveWorker.h parsing with g++ -std=gnu++20 -fsyntax-only -ftime-report, three samples each: 1.556 seconds median before, 0.612 after. This is one header, not total build time.

## Results and limits

- 421 build-system tests passed, two skipped, using the pinned asset interpreter. The system-interpreter asset failures reproduced on the base revision; those diagnostic logs are retained locally.
- 85 browser loader/unit tests passed. Serial/threaded and experimental configuration checks passed. The complete threaded build succeeded. Chromium startup/settings/shutdown, music, Hive, golden match, OPFS recording export and embedded x264 fallback all passed. Firefox and WebKit golden-match and Hive checks also passed. Serial runtime and the complete browser suite remain outstanding.
- GCC 15.2 and Clang 22 real compilation fixtures passed normal/PCH/unity/combined configurations, dependency invalidation and no-change checks. Cache contracts passed. PCH consumers compile directly to avoid forbidden cache sloppiness; cache behavior for these options is an explicit tradeoff, not a claimed speedup.
- Full Linux fast and Android x86_64 fast builds passed numeric build guards. Repeated builds compiled and linked nothing. Android device/emulator runtime was not run.
- The native golden match matched all 842 committed checksum records. JavaScript one/four-worker runs, replay and save continuations passed. These tests were first run on the dirty candidate; the full combined native build also matched all 842 golden records and passed both JavaScript profiles, replay/save equality and all six save continuations.
- Native broad quick/no-display run: 1,100 passing jobs, 11 failures, 191 skipped (2,212 JUnit cases). This is not a green suite. Failures include 120-second O0 timeouts under contention, a LAN latency assertion, an O0 CPU budget assertion, and image fixtures against the old unpatched SDK. Corrected ImageAssets rerun passes all seven cases. The longer targeted engine rerun passed nine of ten cases; LAN host latency still exceeded 50 ms (155 ms). Fixed-budget assertions are not waived for release qualification.
- Native candidate Xvfb checks passed all five selected cases: music, portable rendering, recording shortcut, production menus/gameplay recording, and threaded scene recording with per-tick checksum preservation. Generated capture evidence is kept locally; logs/JUnit are attached here.
- Windows/MinGW, macOS, iOS device/simulator, Android non-x86_64 ABIs/device execution, serial browser runtime and complete cross-platform checksum equality are unverified. Apple infrastructure is unavailable on this Linux host. PCH/unity stay opt-in; ordinary release/default flags stay unchanged. No simulation revision bump was made.

Final focused development policy suite: 12 tests pass, including shared active readers and explicit compilation database paths. The final complete local suite runs all 421 tests, with two skips. Numeric guards now propagate tool settings to PCH object environments and are included in real GCC/Clang fixture tests. Final combined native repeat build: zero compile/link commands. `combined-match` and `combined-javascript` retain the checksum/save manifests. Native application inputs do not change in the last store/query/test-host/documentation commits; the artifacts retain their exact invocation provenance rather than claiming every earlier test ran after the final documentation commit.


## Browser execution commands

Install existing pinned test dependencies with `cd browser && npm ci --ignore-scripts`. Serve the fast threaded directory using `python3 browser/serve.py 8777 --bind 127.0.0.1 --directory build/emscripten/client/debug/dev-dev_fast-true-linker-auto-web_variant-threaded`.

From `browser/`, set:

```sh
export GLOB2_TEST_URL=http://127.0.0.1:8777
export GLOB2_TEST_BUILD_DIR=/home/bradley/.codex/worktrees/3d84/glob2/build/emscripten/client/debug/dev-dev_fast-true-linker-auto-web_variant-threaded
export GLOB2_HIVE_NATIVE=/home/bradley/.codex/worktrees/3d84/glob2/build/linux/client/debug/dev-dev_fast-true-linker-auto/src/glob2
export GLOB2_TEST_RENDERER=software
npx playwright test tests/runtime-build.spec.js tests/determinism.spec.js tests/music-game.spec.js tests/hive-worker.spec.js --project=chromium --grep 'threaded startup|committed match record \(threaded\)|threaded game sends soundtrack|Hive worker matches'
npx playwright test tests/recording.spec.js --project=chromium --grep 'threaded missing WebCodecs|threaded recording segments'
npx playwright test tests/determinism.spec.js tests/hive-worker.spec.js --project=firefox --project=webkit --grep 'committed match record \(threaded\)|Hive worker matches' --output=../artifacts/dev-build/browser-compat-results
```

All ten selected browser tests pass. The attached `browser-recording.mp4` is the exported recording test result. Browser application compilation began on 5e7297b92; the PCH guard and store reader corrections were subsequently committed while it ran. These Python orchestration changes do not alter browser application/compiler flags or pinned library sources in this invocation, but this is not a clean final-SHA first-use/no-change browser benchmark. Browser dependency orchestration must be reseeded under the final recipe identity before claiming final no-change behavior. Browser linking used the direct Emscripten driver; application and auxiliary worker compilation used ccache.

The final Android invocation reseeded recording dependencies after the orchestration recipe identity changed, then successfully rebuilt with unchanged mobile dependency flavor. Its next invocation (`android-final-repeat.log`) compiled and linked nothing. Native normal-fast and combined repeats likewise compile/link nothing.

Hosted cheap contracts initially failed because a newly added pure policy test imported SCons in a lane without it. The fixture now mocks that narrow interface and passes `python3 -S -m unittest discover -s test/build_system -p test_dev_build.py -v` (12 tests) without site packages. The original hosted failure log is retained; the rerun status is separate from engine evidence.

Final cache contract invocation set `GLOB2_TEST_CXX` to the same LLVM 22 binary listed above and passes compiler/optimization-flag invalidation as well as actual GDB source lookup in both checkouts. Native GDB command: `gdb --batch -ex "set pagination off" -ex "info line main" -ex "list main" build/linux/client/debug/dev-dev_fast-true-linker-auto/src/glob2`.

Final hosted cheap contracts pass at 1cdcaf845: https://github.com/Globulation2/glob2/actions/runs/37984204825 . This is cheap-contract evidence only. `master-integration.json` records the successful five-object compile against exact merge tree 5fc55eda385e0c97aa329dea766eda0d5f5819e8 (master c26a0a02a).

The measurement draft now owns its Git-snapshot/compiler-manifest corrections and a baseline configuration-only dependency target (85edfd4d0). The dependency draft now owns the active-reader fix with two standalone contracts, passing without site packages (3cfd9b347). These backports leave the tested final application revision 1cdcaf845 unchanged. `stage-fixes.json` and the stage-specific logs record the independent checks; phase instrumentation is progressively wired by the subsequent platform/compilation integrations.
