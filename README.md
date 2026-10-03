# App-resume touch momentum evidence

Source: `365ace901f34e7d6d12ea636fb67207c77895eff`. Base: `fe33142db145d3025bc28ea6c1ee52a3d96511eb`.

The regression holds the session clock at zero to model its lag after background suspension, while feeding real SDL-timestamped swipes through Engine. It suspends/resumes twice in each serial/threaded mode. Simulation is paused, so camera displacement after release is momentum rather than world movement. Baseline uses unchanged master production code with the same added regression.

- Before: camera coast is 0 pixels; the regression fails.
- After: all four resume/swipe sequences coast; measured displacement is in `momentum-after.txt`.
- PNG pairs show the map at release and on the subsequent coasting frame. These are native Linux touch-event harness captures, not Android screenshots.
- Final selected suites: 9 passed, 1 failed. The failure is the existing settings category assertion at GameSpeedTest.cpp:113; `before.log` reproduces it with unchanged master production code. See JUnit and logs for exact results.
- Live speed/pause/replay validation compares simulation end-state checksums; see `speed-pause-replay.txt`. No simulation scheduling, save, replay or network format is changed.
- Physical Android app switching has not been tested. No cross-platform execution equivalence claim is made.

## Reproduction

Native Linux, GCC 15.2.0, SDL 3.4.16, release build, portable GPU screenshots under Xvfb.

```sh
GLOB2_SDL3_PREFIX=<native-SDL-3.4.16-prefix> CCACHE=1 scons -j16 release=1 server=0 engine-tests
env -u WAYLAND_DISPLAY xvfb-run -a -s '-screen 0 1920x1920x24 -noreset' python3 test/run_tests.py --binary engine --filter 'EngineSession/*' --filter 'GameGUITouch/*' --filter 'GameSpeed/*' --keep-profiles --junit artifacts/resume-momentum/after-junit.xml --verbose
```

For the baseline build, restore only `src/EngineRun.cpp` from the base revision, rebuild, and run the filters `EngineSession/momentum*` and `GameSpeed/settings*`. Screenshot BMPs are under the retained profile directory and converted losslessly to PNG.
