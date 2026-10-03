# Blank-map dismissal

Source: `edf26ff6e2ef765e814797c02b8ceca75de0591d`; base: `fe33142db145d3025bc28ea6c1ee52a3d96511eb`.

A completed empty-terrain tap closes transient palettes, lenses, statistics and inspection together. It clears deferred palette restoration, so no earlier toolbox reappears. A map-peek outside tap does the same; explicit Done retains its return-to-tools behavior. Ordinary map drags and cancelled gestures do not dismiss. Painting, placement and map-mark actions retain their tool-specific behavior. A dismissal does not arm double-tap zoom; normal double-tap/one-finger zoom remains available when no menus are open.

All four GameGUITouch cases pass on the final native Linux release build. See `after.log`, `after.xml` and `gameplay-output.txt`. New checks exercise five transient views in both orientations, verify all menu flags and selection are cleared after drawing (including deferred restoration), assert no orders and an unchanged simulation checksum, and confirm a pan leaves Tools open. Existing inspector tests now assert that a previous open palette stays dismissed. Map-peek outside dismissal and the existing painting, placement, cancellation and zoom tests run in the same suite.

Screenshot pairs show each UI immediately before and after a blank-map tap. Numbered names: 0 Build; 1 Flags; 2 Tools; 3 compact statistics sheet; 4 older statistics list. Native SDL touch-harness captures, not physical Android screenshots. Physical device feel remains unverified. Persistent Spacious desktop panels retain their persistent presentation.

## Reproduction

Native Linux, GCC 15.2.0, SDL 3.4.16, release build with portable rendering under Xvfb:

```sh
GLOB2_SDL3_PREFIX=<native-SDL-prefix> CCACHE=1 scons -j16 release=1 server=0 engine-tests
env -u WAYLAND_DISPLAY xvfb-run -a -s '-screen 0 1920x1920x24 -noreset' python3 test/run_tests.py --binary engine --filter 'GameGUITouch/*' --keep-profiles --junit artifacts/blank-dismiss/after.xml --verbose
```

PNG files are lossless conversions of `dismiss-*.bmp` from the retained gameplay test profile. No simulation or save/replay/network format change.
