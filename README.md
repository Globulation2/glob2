# Generator Studio local verification

Tested head: 01f60b5d50ca96279232c21f4337ef4be00b3603. Base: 2f3db465080eb9b9e1025c220edc8a0c4f5aa0fc; clean merge of current master. Original feature base: 6f8cf442fe5e35aa7e7773f51f3854fa3c8e0ccf. No worktree source changes remain. Master was fetched immediately before the final validation; no further changes were present. Shared map wrapping, fog rendering and save-version-150 area effects were integrated and the affected native/browser builds and tests refreshed.

## Environment

Linux x86_64; GCC 15.2; Emscripten 4.0.15; Node 24.19.0; existing pinned SDL3/recording libraries; installed Playwright Chromium, Firefox and WebKit. Native release uses -O3; browser release uses -O2 -fwasm-exceptions and -pthread for the threaded variant. Build logs record compiler, flags, include/link inputs. Existing packages were used; no dependencies were added. Optimized runtime assets were regenerated for the final engine.

## Results and provenance

- All three TypeScript configurations, lint and Vite release build passed; focused platform: 14 files / 75 tests passed; browser unit tests: 76 passed. These checks cover unchanged platform/bridge source before the final C++ master integration and native-reference-only test changes.
- Native client and both test harnesses built; final frozen-preview test passes 34 assertions; isolated generator/preview/screen/area-effects suite passes 52 cases. Final native test output records the clean tested commit and source digest.
- Native generator and loading its initial save produce the same 64-tick, 197652-byte checksum trace: SHA256 40ff4af71d2304d27a1429200fa17305b0352038fc978e97b03e2ef11bc8af07. Native AI Studio's 1024-tick, 1819354-byte reference remains SHA256 9bc4ef614d6950d53682c4bf8c0c909734d6380f85d3059c4cc56d17d6bc3621. CLI trace computation was performed immediately before committing its derived references; native production source/flags/dependencies are identical at the tested head.
- Save version 150 / simulation revision 38 from upstream #980 moves the generator snapshot/trace references. The native refresh records before/after values. Browser checks additionally require native engine/simulation versions, package hash, world fingerprint and initial checksum. No browser result was used to bless a reference.
- Deployment update-host: 11 passed; online deployment: 14 passed; browser packaging: 18 passed; browser assets: 21 tests, one explicit fontTools skip.
- Final browser matrix: 23 passed, one documented interactive threaded-WebKit skip (5.6 minutes). Final Generator/AI Studio desktop and phone flows: 4 passed (3.4 minutes), including real isolated generator validation and library publication. Earlier reviewed-base runs also passed before upstream integration. Final integration-browser-tests.log and integration-ui-tests.log supersede earlier runs. Development failures remain under logs; they are not passing evidence. The isolated native fixture-path failure was repaired using sourceRoot(), with reviewer confirmation and a passing final isolated run.
- Six-job builds were resumed with more parallel jobs; one browser build received SIGTERM without compiler failure and was resumed. Completed builds exit zero. Hosted cheap PR contracts passed; expensive hosted jobs were skipped.

## Coverage and limits

Focused contracts cover strict atomic generator replacements, incomplete/stale responses, recovery/Undo/restore, cancellation, account isolation, separate billing/idempotency/provider recovery, single-module imports, invalid-manifest preservation, declaration isolation, stale reports and publication receipts. Desktop/phone flows cover generation, settings changes, frozen preview/Watch, isolated validation, diagnostics, repair, Generator Library publication and package export. Paid provider/Stripe behavior uses existing fakes; no live paid request was sent.

Generator CLI checksum comparisons cover generation and initial-save loading across native and six browser/runtime combinations. Live preview tests separately compare frozen metadata and exercise Watch tick progression, pause and teardown. The native preview test checks continuation of its exact frozen world. Cross-platform per-tick checksum parity through the live Watch path has not been established. AI Studio browser references cover all six browser/runtime variants.

Interactive threaded WebKit falls back to serial because this installation cannot share worker memory; that case is explicitly skipped, while headless threaded WebKit is covered. Native Windows/macOS/Android and a complete deployed Docker/Caddy stack were not exercised. Production deployment and human maintainer playthrough remain pending; Generator Studio stays disabled by default. The feature changes no simulation computation, save/package format or runtime dependency; upstream's save/simulation revision and golden changes are retained.

## Exact commands

Run from repository root with the bundled Node 24.19 binary on PATH:

```sh
CCACHE=1 GLOB2_RECORDING_PREFIX=$PWD/artifacts/generator-studio/recording-prefix GLOB2_SDL3_PREFIX=/tmp/glob2-sdl3/prefix scons release=1 -j16 --build=artifacts/generator-studio/build artifacts/generator-studio/build/src/glob2 engine-tests unit-tests
CCACHE=1 scons target=web release=1 -j12
npm --prefix platform run typecheck
npm --prefix platform run lint
npm --prefix platform run build -w @glob2/web
node --test browser/unit/*.test.js
GLOB2_TEST_ARTIFACTS=$PWD/artifacts artifacts/generator-studio/build/test/glob2-engine-tests --test-suite=MapPreview --test-case='generator Studio*' --no-breaks=true
python3 test/run_tests.py --build-dir artifacts/generator-studio/build --filter 'ScriptGenerator/*' --filter 'MapPreview/*' --filter 'ScreenExecution/*' --filter 'BuildingAreaEffects/*' --jobs 8
node browser/node_modules/.bin/playwright test -c browser/playwright.config.js browser/tests/generator-studio.spec.js browser/tests/studio.spec.js
```

The archive includes focused-test command lists, exact native CLI commands, desktop/phone E2E environment and result counts in commands.md. Saves, replay checksum sidecars, packages, settings, reports and screenshots support the claims above. SHA256SUMS records the archived files. No assertion of hosted full-matrix verification or human playthrough is made.

