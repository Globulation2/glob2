# PR #676 repair verification

Base: `79229b3101c6bd6e0c83abb5ed55579834595785` (fetched again before validation; unchanged). Local environment: Ubuntu 26.04, x86_64, GCC 15.2.0, SDL3 3.4.16 with pinned SDL3 image/ttf/net dependencies. Native release flags: `-std=gnu++20 -Wall -fPIC -O3 -s`; source and binary provenance is embedded in retained JUnit output. The build logs record dependency paths and full compiler/linker flags.

## Native coverage

`393d0aece503b04cb8488b18ed2bc5894df21253`:

```sh
GLOB2_SDL3_PREFIX=/home/bradley/glob2-verify/integrator/sdl3/prefix scons -j12 release=1 server=0 tests
LD_LIBRARY_PATH=/home/bradley/glob2-verify/integrator/sdl3/prefix/lib python3 test/run_tests.py --exclude-tag map-generators --jobs 8 --display-jobs 2 --fullscreen --junit artifacts/ci-repair/native-final.xml --artifacts artifacts/ci-repair/native-final
```

Exit 0: 597 process groups, 1,276 JUnit cases, no failures/skips. Includes six complete presentation sweeps, software/OpenGL rendering, save/load continuation, replay/network golden match verification, scripting, settings and unit suites. This revision predates only a generator test expectation repair and the browser settings failure retry change below.

`3ac128c9a403c0278e7f00283885e997ebaebfc2` generator expectation repair:

```sh
GLOB2_SDL3_PREFIX=/home/bradley/glob2-verify/integrator/sdl3/prefix scons -j12 release=1 server=0 build/linux/client/release/test/engine-MapGeneratorDefaultsTest.o build/linux/client/release/test/engine-support_TestMain.o
GLOB2_SDL3_PREFIX=/home/bradley/glob2-verify/integrator/sdl3/prefix python3 artifacts/ci-repair/link-generator-test.py
LD_LIBRARY_PATH=/home/bradley/glob2-verify/integrator/sdl3/prefix/lib python3 test/run_tests.py --build-dir artifacts/ci-repair/generator-build --binary engine --filter 'MapGeneratorRegistry/catalog generation contracts*' --junit artifacts/ci-repair/catalog-final.xml --artifacts artifacts/ci-repair/catalog-final
```

Exit 0: complete catalog contract passed (213 seconds), including supported envelopes, resource extremes, growth, mutation rejection and repeatability. The isolated binary preserves the already-running native suite's executable; `generator-link.log` records the exact linker command. Independent map-skill review confirmed all explicit revision/default expectations match current generator definitions.

Settings retry source subsequently committed as `a444f65866416cbe15de48e42f7bc4b71e6daf73` was built atop 3ac128c9a and rechecked with:

```sh
GLOB2_SDL3_PREFIX=/home/bradley/glob2-verify/integrator/sdl3/prefix scons -j12 release=1 server=0 tests
LD_LIBRARY_PATH=/home/bradley/glob2-verify/integrator/sdl3/prefix/lib LP_NUM_THREADS=2 python3 test/run_tests.py --filter 'Settings/*' --filter 'UILayout/*' --filter 'WinProbability/*' --filter 'HiveMindPresentation/*' --display-jobs 1 --fullscreen --junit artifacts/ci-repair/settings-native-final.xml --artifacts artifacts/ci-repair/settings-native-final
```

Exit 0: 14 groups / 39 cases. Provenance reports the dirty 3ac128c9a tree because this build preceded that commit; those source edits are exactly a444f6586. Desktop settings, touch layout measurements, Hive teardown and the unchanged victory trace pass. There are no simulation-rule, RNG, serialization or acceptance-gate changes.

## Contracts and browser evidence

```sh
"$(python3 tools/package_assets.py --encoder-python)" -m unittest discover -s tests/build_system -v
python3 -m unittest discover -s test/maxima -p '*Test.py' -v
python3 data/check_translations.py --strict
node --test browser/unit/*.test.js
```

All exit 0: 291 build-system cases (3 existing skips), 61 farming cases (1 existing skip), zero translation structural errors, 42 browser unit cases. Contract sources were unchanged by later settings/timing edits.

Browser master runtime artifact: run 37166504960, artifact 11290577364 (79229b3101). Local Playwright 1.63 Chromium against `python3 browser/serve.py 8771 --bind 127.0.0.1 --directory artifacts/ci-repair/web-client-new`. `GLOB2_TEST_URL=http://127.0.0.1:8771 GLOB2_CHROMIUM_ANGLE=swiftshader node browser/node_modules/@playwright/test/cli.js test --config browser/playwright.config.js --project=chromium torus.spec.js` passed all three checks. Hosted master still exposed a startup-zoom timing failure, so these local passes do not establish that the old setup was reliable. The revised torus test is validated separately.

The save-restore database fault test passes with the repair's save-database filter (`storage-after.log`). A wider storage run identified settings Continue flicker. `settings-stability-before.log` demonstrates the strengthened regression fails against master: the real Continue control disappears over an 800 ms sample window. Browser validation of the fixed runtime and full hosted compatibility coverage are recorded in follow-up PR comments.

`hive-valgrind.log` identifies the old fixture lifetime error. The repaired Hive teardown and initialized desktop menu fixture both show zero Valgrind errors (`hive-valgrind-after.log`, `desktop-valgrind-after.log`). Screenshots retain enlarged-text landscape action layout and the tablet room, including the revealed chat input.

## Coverage limits

Local validation is Linux x86_64 only. It does not claim Windows/macOS/Android execution, cross-platform checksums, hosted coverage percentages, or a human playtest. Full hosted CI is requested in PR #676; platform and browser evidence comes from that matrix. No tests or platform selections were removed. Generated logs and screenshots live on this evidence branch, outside the product documentation and repair PR diff.
