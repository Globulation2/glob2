# Maxima placement fixture repair evidence

Original hosted failure: https://github.com/Globulation2/glob2/actions/runs/37996712273/job/114052874512 on master350d324da53b050a25cec09a39b76eaaf7b446a3.

Diagnosis: Map::setResource assigns wood stocks1–4 through ResourcePlacement. GameHeader defaults the seed to time(NULL). The maintenance planner minimizes total resource burden, not tile count. The fixture expects one cleared tile, therefore its input costs must be equal. The repair sets each placed wood stock to1 using Map::setResourceAmount; no production behavior or assertions change.

Baseline reproduction uses the previously built Linux GCC15.2 debug test executable at557ac4db96a9da5c4b0a2404e9850a5285dd13ff. It is diagnosis only, not final-revision validation. fixed-time.c overrides time() in this disposable test process so the setup uses a specified timestamp. Compile it with `cc -shared -fPIC artifacts/maxima-placement/fixed-time.c -o artifacts/maxima-placement/fixed-time.so`. Run with `LD_PRELOAD="$PWD/artifacts/maxima-placement/fixed-time.so" TEST_TIME_SEED=1791590195 build/native-coverage-torus/test/glob2-engine-tests --test-case='placement maintenance regressions'`. seed-1791590195.log reproduces20 resources/2cleared/18removed on Linux. The other14 seed logs pass. This identifies input seed sensitivity, not Windows-specific execution drift.

Final revision: c7a60be4e0ed218b6ea85e1e6b7b757543f106f7, based on2c37ebf45e7b8ac2986f9ebd27fe2b03f06a810a. Linux x86_64, GCC15.2, debuggnu++20 -g -Wall -fPIC; explicit SDL3 prefix. Full compile commands and dependency probes are in build.log. Build: `GLOB2_DEV_MODE=isolated GLOB2_SDL3_PREFIX="$PWD/build/sdl3-ci/prefix" scons --build=build/native-coverage-torus release=0 -j8 build/native-coverage-torus/test/glob2-engine-tests`.

No local Windows toolchain is available. Platform-specific rendering, replay/network/save and full matrix checks are omitted from focused repair validation because this change only normalizes test input resource costs; it does not alter engine computation or persistence. Subsequent hosted full master Windows results remain the recovery check.

Final results: both builds exited0. `python3 artifacts/maxima-placement/run-seeds.py` passed all15 seeds,33assertions each. `build/native-coverage-torus/test/glob2-engine-tests --test-suite=Maxima.Implementation` passed29cases/2065assertions. Fresh master fetch still2c37ebf45 before acceptance. Logs contain exact provenance for finalcommit.
