# Ecology integration for PR #787

Tested head `a27016c59` integrates the unchanged ecology optimization with master `ca788aec6f10335f037c23624af013b8114bd6ce`. Resolved the documentation conflict by retaining both Trail compatibility guidance and precise ecology invalidation rules. Updated the new ecology fixture from `ROAD` to `TRAIL`; stable terrain ID4 and its properties are unchanged. Independent review confirms new market arrays, locks, scheduling flags and serialization remain intact.

Full native release build passed. **796 unit cases and 291 focused engine cases passed** (17/19 skipped), including Markets V2 enabled/disabled/legacy checksum goldens, cache invalidation, save continuation, terrain/gradient behavior, replay/network and JavaScript coverage. All8 ecology Python contracts passed. Logs/JUnit/provenance are attached. No new simulation revision or gameplay change.

Environment: Linux x86-64/GCC15.2, unchanged SDL3/recording dependency prefixes and patched SDL test runtime from previous evidence. Build command:

```sh
GLOB2_SDL3_PREFIX=/tmp/glob2-sdl3/prefix GLOB2_RECORDING_PREFIX=/tmp/glob2-terrain-baseline-build/recording/prefix taskset -c 0-11 scons -j12 release=1 tests build/linux/client/release/src/glob2
LD_LIBRARY_PATH=/tmp/glob2-terrain-sdl-patched/prefix/lib python3 test/run_tests.py --binary unit --no-display --exclude-tag benchmark --junit artifacts/gradient-optimization/merge-integration/ecology-unit.xml --artifacts artifacts/gradient-optimization/merge-integration/ecology-unit
LD_LIBRARY_PATH=/tmp/glob2-terrain-sdl-patched/prefix/lib python3 test/run_tests.py --binary engine --no-display --filter '*Fertility*' --filter '*Ecology*' --filter '*Terrain*' --filter '*Gradient*' --filter '*Market*' --filter '*Save*' --filter '*Replay*' --filter '*Match*' --filter '*Maxima*' --filter '*Turn*' -j8 --junit artifacts/gradient-optimization/merge-integration/ecology-engine.xml --artifacts artifacts/gradient-optimization/merge-integration/ecology-engine
python3 tools/test_terrain_ecology.py
```

During validation master advanced to `f9a673e29` with the independent music scheduling change #794. That change does not overlap ecology production logic; the full build above is explicitly for ca788, not the subsequent audio merge. The stacked gradient PR will receive a full combined build and fresh per-tick/serial/parallel/save comparison before its merge. Existing non-Linux/display limitations remain. No new timing claims. User has authorized merging this optimization stack.

[Raw evidence archive](ecology-integration.tar.gz), SHA-256 `7e477eb3b46e472a8fa39efabd72b1463925948cd09962215493a8f7a31fefb1`. Extract at a separate repository root; its files stay under ignored `artifacts/`.
