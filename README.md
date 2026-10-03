# Touch unit inspection evidence

Source revision: `0aa693730cb41fe60e39cc3d28308e6455b4bf0c`. Base: `fe33142db145d3025bc28ea6c1ee52a3d96511eb`.

Native Linux release engine tests, SDL 3.4.16 / GCC 15.2, portable GPU under Xvfb.

Build: `GLOB2_SDL3_PREFIX=<native SDL SDK> CCACHE=1 scons -j16 release=1 server=0 engine-tests`

Run: `env -u WAYLAND_DISPLAY xvfb-run -a -s '-screen 0 1920x1920x24 -noreset' python3 test/run_tests.py --binary engine --filter 'GameGUITouch/*' --keep-profiles --junit artifacts/unit-touch/evidence/after.xml --verbose`

All five cases pass. See after.xml, after.log, and unit-output.txt (including source/compiler provenance).

The new fixture sends SDL touch events to the game at 390×844 and 844×390 window sizes. It exercises a 28-point near miss, an outside-radius miss, several zoom settings, fog exclusion, actual unit selection and Scene stats, body taps, scrolling, closing, near-unit dragging, overlapping flying/ground units, direct-hit priority, and removal of the selected unit. Existing flag dragging, gameplay, zoom, placement and scroll tests also pass.

The four PNGs show the card before/after scrolling in portrait and landscape. The fixture uses replay visibility to expose the map; Pause/Speed are replay controls, while the unit inspector and touch picker are shared with live games. Screenshots and logs are from the source revision above.

Physical Android/iOS gesture feel has not been tested. Hosted CI status belongs to the PR. No simulation/save/network changes.
