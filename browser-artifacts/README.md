# Browser artifact, platform and Windows fixture repair

Tested repair revision: `6c035e68f` (clean checkout), based on master `c4d2a4dc85bf89552ccd53668ff570dd3314b023`. Newer master `a05d6cd8c` only adds deploy build-container binutils; full hosted integration is still required after merge.

## Failures and fixes

- Master run [37174723828](https://github.com/Globulation2/glob2/actions/runs/37174723828), Chromium job 111361155269: eight recording failures. The uploaded web-client artifact omitted every recording worker/runtime file. Failure trace network entries show HTTP 404 for recording-worker.js. Add all five worker/runtime assets and the generated license directory to both CI browser artifacts; also complete the manual development artifact's Hive worker set.
- Master [37177326417](https://github.com/Globulation2/glob2/actions/runs/37177326417): platform lint rejects apps/web/art/README.md after the source move. Apply pinned Prettier to that file.
- Continuing platform tests locally exposed stale generated schema descriptions referring to src/Version.h. Regenerate fixtures from the current schema source. A structural comparison confirms all 37 JSON changes are descriptions only; no validation constraints change.
- Full manual run [37175401816](https://github.com/Globulation2/glob2/actions/runs/37175401816): Windows shared JavaScript corpus fails only the environment restoration fixture. Its supposedly SDL-only setup called GAGCore::setProcessEnvironment, which also writes the CRT value. Use SDL_setenv_unsafe and verify both distinct original values before testing restoration. Production environment handling is unchanged.

## Local validation

Ubuntu 26.04 x86_64, GCC 15.2, pinned SDL 3.4.16, Emscripten 4.0.15, Playwright 1.63.0. Platform final tests use official Node 22.23.3, matching hosted CI, with a dedicated PostgreSQL 16 container on 127.0.0.1:55433. Existing databases were not used.

The original master browser runtime was downloaded from artifact 11292863616 (source 5428cc3d295dc254067e48cc34944853304fb9fc). Serving it unchanged reproduced the serial x264 recording failure. Build the missing workers from repair revision 7a0c1e045 with:

```
scons target=web release=1 -j8 build/emscripten/client/release/recording-runtime.js build/emscripten/client/release/recording-worker.js build/emscripten/client/release/recording-storage.js build/emscripten/client/release/recording-video.js
```

Copy those five generated assets into an otherwise identical downloaded runtime bundle. Worker sources are unchanged by subsequent fixture/formatting commits. Full repaired-bundle recording tests pass in Chromium, Firefox and WebKit (10 each, 30 total). Commands:

```
GLOB2_TEST_URL=http://127.0.0.1:8774 GLOB2_TEST_RENDERER=software node browser/node_modules/@playwright/test/cli.js test --config browser/playwright.config.js --project=chromium recording.spec.js --output artifacts/ci-repair/recording-after-test
GLOB2_TEST_URL=http://127.0.0.1:8774 GLOB2_TEST_RENDERER=software node browser/node_modules/@playwright/test/cli.js test --config browser/playwright.config.js --project=firefox recording.spec.js --output artifacts/ci-repair/recording-firefox-after
GLOB2_TEST_URL=http://127.0.0.1:8774 GLOB2_TEST_RENDERER=software node browser/node_modules/@playwright/test/cli.js test --config browser/playwright.config.js --project=webkit recording.spec.js --output artifacts/ci-repair/recording-webkit-final
```

This compares artifact completeness using the actual failing main runtime, not a fresh current-master main runtime build. Full hosted final integration remains necessary. No timeout, recording assertion or failure condition was weakened.

Platform: npm run lint, npm run typecheck, npm run build -w @glob2/web pass. TEST_DATABASE_URL=postgres://glob2:glob2@127.0.0.1:55433/postgres npm test: 439 pass, 6 existing skips across 2 files. DATABASE_URL=postgres://glob2:glob2@127.0.0.1:55433/glob2 npm run migrate -- latest is recorded separately. Fixture generation uses official TypeScript-capable Node; the system distro Node lacks its TypeScript compiler.

Native clean final repair build:

```
GLOB2_SDL3_PREFIX=/home/bradley/glob2-verify/integrator/sdl3/prefix scons release=1 -j8 build/linux/client/release/test/glob2-engine-tests
LD_LIBRARY_PATH=/home/bradley/glob2-verify/integrator/sdl3/prefix/lib GLOB2_TEST_ARTIFACTS_ROOT=artifacts/ci-repair/windows-environment-final python3 test/run_tests.py --build-dir build/linux/client/release --binary engine --filter 'JavaScriptCompatibility/*' --artifacts artifacts/ci-repair/windows-environment-final --junit artifacts/ci-repair/windows-environment-final.xml
```

All seven Linux compatibility cases pass. The changed Windows-only setup must still pass hosted Windows; local Linux execution does not establish Windows correctness.

Build-system contracts tested at 7a0c1e045, with workflow content unchanged afterwards: encoder Python from tools/package_assets.py --encoder-python, then -m unittest discover -s test/build_system -v: 297 tests pass with 3 skips (NSIS unavailable, fontTools unavailable in encoder Python, expected SDL_ttf archive absent). Raw log retains reasons.

No simulation rules, save format, replay/network version or deterministic computation change. No game-feel change is intended. Windows execution and full current-master integration remain explicit hosted validation requirements.
