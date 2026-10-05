# Landscape picker harness lifecycle verification

PR head 6e57f5df1df4a183fb7b60707cbca95e284a05c5; fetched master/base 9426e290cb97eaac97c142df87d915e87dbfd494.
Ubuntu 26.04.1 x86_64, GCC 15.2, Python 3.14.4; SDL 3.4.16/image 3.4.6/ttf 3.2.2, release client engine harness, OpenGL disabled. Asset encoder Pillow 12.2/WebP 1.6. SDL libraries from retained hosted native dependency artifact; Xvfb/Openbox display 1024x768. Canonical SDL3 SDL_VIDEO_DRIVER=x11 prevents unrelated local Wayland initialization failure.

The old harness with only `picker.select(other)` immediately before the randomized-landscape click reproduces the exact hosted GCC 11 assertion: popup closed, run=false, return=44. The final harness exercises that precondition permanently, closes/reopens execution before continuing, and asserts each independent Return/tile/Use action closes its own live execution. No production changes, simulation or compatibility impact.

Build:
```
GLOB2_SDL3_PREFIX=build/sdl3-ci/prefix GLOB2_RECORDING_PREFIX=build/linux/client/release/recording/prefix GLOB2_ASSET_ENCODER_PYTHON=/home/bradley/.local/share/glob2/development/caches/asset-encoder-runtime-assets-v1-py3.14/venv/bin/python scons -j12 release=1 server=0 opengl=0 --build=build-software-terrain engine-tests
```
Test (before-x11/fixed/final report paths differ):
```
env -u DISPLAY -u WAYLAND_DISPLAY SDL_VIDEO_DRIVER=x11 LD_LIBRARY_PATH=/home/bradley/.codex/worktrees/d1e6/glob2/artifacts/ci-monitor/native-d5bad36f7/build/sdl3-ci/prefix/lib python3 test/run_tests.py --build-dir build-software-terrain --binary engine --filter 'CustomGameSetup/custom game screens; captures*' --junit artifacts/picker-lifecycle/final.xml --artifacts artifacts/picker-lifecycle/final --verbose
```

Full display case covers preferences, translated keys, picker preview scheduling and random controls, all confirmation actions, and lobby setup. Broad simulation/platform suites omitted because only harness lifecycle changes; original failure is GCC 11 hosted and local compiler is GCC 15. Native fixture behavior, not gameplay, is repaired. Hosted recovery awaits subsequent full master. Preserved original failed job log included; screenshots archived with final validation output.

Final committed revision: build PASS, 1 display case PASS in 65.6 seconds. Repaired working tree also passed in 65.5 seconds; old controlled case failed in 51.6 seconds at the hosted popup assertion.
