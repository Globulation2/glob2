# Map momentum dead zone

Source: `bd6405085ed7f6731ce9c41ae5c43ee59fb4614f`; base `fe33142db145d3025bc28ea6c1ee52a3d96511eb`.

The input harness feeds actual SDL finger events to gameplay. It checks stationary contact, 4-point movement, 12-point movement, diagonal jitter, repeated oscillations within a 10-point radius, deliberate horizontal/diagonal swipes reaching 16 points, and catching a coast with a fresh short touch. Existing tests cover long swipes, toroidal wrapping, focus cancellation, simulation checksum stability, HUD/editor momentum and mouse behavior.

Before: a 12-point movement followed by release slides the map about 68 additional logical pixels in 100 ms. After: post-release displacement is zero. See `before.log`, `touch-output.txt`, JUnit and `after.log`. Four GameGUITouch cases pass on the final revision.

The PNG pairs show release and 100 ms later. Native Linux portable-renderer captures; no physical Android testing. Short positioning drags can still move the map while held; the new 16-screen-point threshold governs release momentum. Velocity and decay remain unchanged for deliberate swipes.

## Reproduction

Native Linux, GCC 15.2.0, SDL 3.4.16, release build:

```sh
GLOB2_SDL3_PREFIX=<native-SDL-prefix> CCACHE=1 scons -j16 release=1 server=0 engine-tests
env -u WAYLAND_DISPLAY xvfb-run -a -s '-screen 0 1920x1920x24 -noreset' python3 test/run_tests.py --binary engine --filter 'GameGUITouch/*' --keep-profiles --junit artifacts/momentum-dead-zone/after.xml --verbose
```

For baseline restore `src/gui/GameGUITouch.cpp` and `.h` from the base revision, keep the added regression, rebuild and run `GameGUITouch/touch scroll momentum*`. Baseline provenance records those restored files as modifications to the earlier fix commit. Screenshots come from retained test profiles and are converted losslessly from BMP to PNG. The regression fails on the 12-point case before the fix.
