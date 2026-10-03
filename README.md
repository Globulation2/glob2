# Opposite-side gameplay toolboxes

Source: `3abd5c71a3e956f17172ba4d3ff18cb27e7ef1b3`. Base: `fe33142db145d3025bc28ea6c1ee52a3d96511eb`.

Screenshots cover build, flags/zones, zone brushes and Tools for right/left thumb settings in portrait/landscape. Filenames name the **selected thumb**, not the toolbox side: right-thumb captures have left-side toolboxes, left-thumb captures have right-side toolboxes. These are native Linux SDL touch-harness captures, not physical Android screenshots.

Four GameGUITouch cases pass on the final release build. The layout checks verify both handedness settings, opposite-edge anchoring, palette item order/bounds, placement confirmation staying on the thumb side, and Android gesture-exclusion rectangles tracking the palette. The existing interaction suite exercises palette taps/drag-out placement, zone sizes/painting/undo/edge-pan, tactical lenses, flags, radial controls, cancellation and momentum. See `after.log`, `after.xml` and `gameplay-output.txt` for results and source provenance.

The overlay caption moves to the thumb side to remain opposite the toolboxes. Full-width statistics sheets, map peek, radial inspector, placement confirmation and Spacious desktop panels retain their layout. Physical Android reach/feel remains a review check. No simulation or persisted-format change.

## Reproduction

Native Linux, GCC 15.2.0, SDL 3.4.16, portable renderer under Xvfb:

```sh
GLOB2_SDL3_PREFIX=<native-SDL-prefix> CCACHE=1 scons -j16 release=1 server=0 engine-tests
env -u WAYLAND_DISPLAY xvfb-run -a -s '-screen 0 1920x1920x24 -noreset' python3 test/run_tests.py --binary engine --filter 'GameGUITouch/*' --keep-profiles --junit artifacts/opposite-toolboxes/after.xml --verbose
```

PNG images are lossless conversions of `opposite-*.bmp` from the retained gameplay test profile. Temporary evidence is kept on this separate evidence branch.
