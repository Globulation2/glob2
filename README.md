# Asset backpressure cleanup fixture verification

Tested head 18ca291af85c645601dbfc3e285f2f2ac35cf996; fetched base ee67fd5f3d423567690551e0da685d1d555f2e73.
Ubuntu26.04.1 x86_64, GCC15.2, Python3.14.4, SDL3.4.16/image3.4.6/ttf3.2.2, release software-only unit binary. Asset encoder Pillow12.2/WebP1.6. Broad native unit suite checks affected shared asset infrastructure; engine/gameplay/platform matrices omitted for this four-line test-only scheduling correction. Production asset loader, simulation, save/replay/network versions unchanged.

Original GCC13 master9426 failed at AssetLoaderTest.cpp:56 with bufferedEncodedBytes66 rather than0. A reader publishes its result ready before the reader job is destroyed. The decoder may complete while that job retains the compressed source. Reading a decoded image therefore does not imply every read-job reference is already released. Test now waits for cleanup within its SAME five-second deadline, preserving all image, metadata, and zero-credit assertions.

Controlled reproduction at base adds diagnostic-reader-delay.patch (50ms sleep after reader publishes readiness, while its job still owns the result). Old test FAIL exactly66==0, 22/23 assertions pass. Same diagnostic with repair PASS23/23. Diagnostic removed before final commit/build.

Build commands (before/controlled-fixed/final log paths differ):
```
GLOB2_SDL3_PREFIX=build/sdl3-ci/prefix GLOB2_RECORDING_PREFIX=build/linux/client/release/recording/prefix GLOB2_ASSET_ENCODER_PYTHON=/home/bradley/.local/share/glob2/development/caches/asset-encoder-runtime-assets-v1-py3.14/venv/bin/python scons -j12 release=1 server=0 opengl=0 --build=build-software-terrain unit-tests
```
Controlled test:
```
build-software-terrain/test/glob2-unit-tests --test-suite=AssetLoader --test-case='read backpressure*' --no-breaks=true
```
Final full suite:
```
build-software-terrain/test/glob2-unit-tests --no-breaks=true --reporters=junit --out=artifacts/asset-credit-fixture/final.xml
```

Original hosted job log, both controlled logs/builds, and final build/log/JUnit are included. Local compiler is GCC15 rather than failed hosted GCC13; full master will establish hosted recovery. No fixed sleep, retry or longer deadline added to the committed test.

The initial final full-suite run with the old local SDL shared library fails only two pixel assertions in ImageAssets/16-bit RGBA, because that local dependency predates the already merged SDL PNG16 patch (#758). Preserve its final.xml/log. A second run using the old retained hosted dependency bundle also predates that patch and fails those same two assertions:
```
LD_LIBRARY_PATH=/home/bradley/.codex/worktrees/d1e6/glob2/artifacts/ci-monitor/native-d5bad36f7/build/sdl3-ci/prefix/lib build-software-terrain/test/glob2-unit-tests --no-breaks=true --reporters=junit --out=artifacts/asset-credit-fixture/final-hosted-sdk.xml
```

The correct patched SDL3.4.16 shared library is retained from #758 native verification. Final verification of the SAME committed binary uses that SDL first and the old bundle for image/ttf/net:
```
LD_LIBRARY_PATH=/home/bradley/.codex/worktrees/repair-browser-png16/glob2/artifacts/png16/native-prefix/lib:/home/bradley/.codex/worktrees/d1e6/glob2/artifacts/ci-monitor/native-d5bad36f7/build/sdl3-ci/prefix/lib build-software-terrain/test/glob2-unit-tests --no-breaks=true --reporters=junit --out=artifacts/asset-credit-fixture/final-patched-sdk.xml
```
All initial full-suite results are preserved and explained, including both stale dependency runs.

Final patched-dependency full suite PASS: 796 test cases, 13,706,047 assertions, 0 failures/errors, 14.2018 seconds.
