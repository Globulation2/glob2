# Touch resource inspection

Source: `9e82bbc7b040957a04b52a17001887c153f21397`; base: `fe33142db145d3025bc28ea6c1ee52a3d96511eb`.

The second layout in the report was the generic tactical list: touch selection opened the panel, but no resource-specific rendering or input branch existed. The fix adds a resource card sourced from the extracted Scene, matching desktop name/sprite/amount semantics. Wood is not granular and therefore has no fractional quantity; wheat shows the selected tile's current/maximum amount.

Four GameGUITouch cases pass on the final native Linux release build. See `after.log`, `after.xml` and `gameplay-output.txt` for results and source provenance. New checks tap wood and wheat in both phone orientations, verify name and quantity, verify a compact card, tap the readout without opening statistics or issuing orders, close and restore panel state, switch to Tools and draw a frame to catch deferred restoration, and remove a selected resource to check invalidation. Existing interaction coverage remains in the same run.

Screenshots show wood and wheat in portrait and landscape. Cards sit opposite the selected thumb, consistent with the toolbox change prepared separately. These are native Linux SDL touch-harness captures, not physical Android screenshots. Physical device testing remains unverified. No simulation or save/replay/network format change.

## Reproduction

Native Linux, GCC 15.2.0, SDL 3.4.16, portable renderer, release build:

```sh
GLOB2_SDL3_PREFIX=<native-SDL-prefix> CCACHE=1 scons -j16 release=1 server=0 engine-tests
env -u WAYLAND_DISPLAY xvfb-run -a -s '-screen 0 1920x1920x24 -noreset' python3 test/run_tests.py --binary engine --filter 'GameGUITouch/*' --keep-profiles --junit artifacts/resource-info/after.xml --verbose
```

PNG captures are lossless conversions of `resource-*.bmp` from the retained gameplay test profile. The test fixture temporarily supplies a known three-unit resource at an empty visible map tile and restores it afterward.
