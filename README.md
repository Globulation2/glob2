# Immediate CPU mode rename verification

PR #1156 tested head aa2fdda766d2308aeecc6047c4538b39d9483f17; base 0709322f3b140507f432aa820611c455314c91c2.

Linux x86_64, GCC 13.3, SDL 3.4.16, existing pinned dependencies. Build passed: GLOB2_SDL3_PREFIX=/home/bradley/glob2/build/sdl3/prefix python3 tools/dev_build.py -j8 unit-tests engine-tests (-O0 -g1, compiler caching). Final head: 60 unit cases and 35 TerrainMaterials cases pass. Eight standalone mode/dispatch contract configurations also pass.

Initial tested head 8e6935e7cff80f6199da38a73c7a576df78fb69a: 85 of 86 engine cases passed, including renderer, resize, backend-switch and terrain-presentation suites. A terrain fixture required executor dispatch counters without enabling current master's opt-in metrics. The final test-only fix enables Trace; all 35 TerrainMaterials cases then pass. Production sources are identical between those revisions. Initial failure and final corrected logs/XML are retained; this is not a claimed 86-case final-head rerun.

Archive includes exact patch, build logs, commands, inventories, JUnit, captures and standalone contract source/log. validation.json contains executable hashes. Initial builds were deliberately interrupted only to raise job count and integrate overlapping master changes; integrated build and final refresh passed. Other platforms and full hosted CI were not executed. This naming change does not alter render kernels, defaults, scheduling, simulation results, save/replay/network formats or dependencies. No performance claim uses this development build.
