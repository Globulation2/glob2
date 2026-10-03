# Gameplay double-tap zoom evidence

PR #584, source `73c94a86b4c7462888ddb48b5a9d0d4a866a8197`, clean build provenance in `gameplay-output.log`.
Linux x86_64, GCC 15.2.0, optimized client with SDL 3.4.16 Linux SDK.

All four GameGUITouch engine cases pass. The gameplay case checks portrait 320×568 and landscape 568×320: 0.75× → 1.5×, 1× → 2×, 2× → 3×, and 3× → 3×, with the tapped world point preserved. Below/above-default and maximum cases also cross toroidal map seams. Painting emits no orders on double tap; pinch, zoom dragging/direction/cancellation, placement, touch scrolling, flag dragging, and the editor remain covered by the existing suite. The fixture also checks that navigation and queued orders do not mutate simulation state.

Before/after PNGs capture 1× → 2× in each orientation. These are synthetic SDL touch events on Linux, not physical Android/iOS captures. Physical device, macOS and Windows coverage remains outstanding; hosted checks are tracked on the PR. No simulation algorithms or save/replay/network formats change.

```sh
GLOB2_SDL3_PREFIX=<Linux SDL SDK> CCACHE=1 scons -j16 release=1 server=0 engine-tests
env -u WAYLAND_DISPLAY xvfb-run -a -s '-screen 0 1920x1920x24 -noreset' python3 test/run_tests.py --binary engine --filter 'GameGUITouch/*' --keep-profiles --artifacts artifacts/double-tap/tests --junit artifacts/double-tap/evidence/junit.xml --verbose
```

`tests.log` and `junit.xml` record the four passing cases; `gameplay-output.log` contains the actual zoom transitions and compiler/source provenance. `build.log` records the successful build.
