# Objectives and Teams modal margins

Source: `48b1818ea85bc8b5e08e48d6a5701eac559a2ba5`. Base: `fe33142db145d3025bc28ea6c1ee52a3d96511eb`.

The painted panels leave at least 16 screen points inside the safe/keyboard-adjusted dialog area on all four sides. Objectives no longer fills the available height. Teams scrolls its title, description and team rows together so large text cannot squeeze the scrollable controls out of view; OK remains in the footer. Touch width caps are 560 points for Objectives/Hints and 640 for Teams.

## Validation

The final selected run covers all four GameGUITouch cases plus the small portrait/landscape UIPresentation sweeps. See `after.log` and `after.xml` for results. Gameplay output records exact panel bounds in the dialog's logical coordinate space (which may differ from PNG pixel dimensions).

Gameplay checks explicitly cover 320×568, 568×320 and 1024×768 touch presentation, plus classic desktop at 1024×768. They assert every outer margin, short hints staying content-sized, long hints scrolling, and the final rival's chat toggle remaining reachable at 150% text size. The Teams fixture has three rival colonies, matching the reported shape of the problem. Tabs and OK are exercised through touch events. The UI presentation sweeps also cover safe insets and presentation modes.

PNG filenames identify dialog, viewport and large-text variants. These are native Linux SDL harness captures, not physical Android screenshots. Device testing remains unverified. No simulation, save, replay or network format changes.

## Reproduction

Native Linux, GCC 15.2.0, SDL 3.4.16, release build, portable rendering under Xvfb:

```sh
GLOB2_SDL3_PREFIX=<native-SDL-prefix> CCACHE=1 scons -j16 release=1 server=0 engine-tests
env -u WAYLAND_DISPLAY xvfb-run -a -s '-screen 0 1920x1920x24 -noreset' python3 test/run_tests.py --binary engine --filter 'GameGUITouch/*' --filter 'UIPresentation/*small*' --keep-profiles --junit artifacts/inset-dialogs/after.xml --verbose
```

Captures are lossless PNG conversions of `inset-*.bmp` in the retained gameplay test profile. Temporary evidence is kept on this separate evidence branch.
