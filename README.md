# Generator Studio post-merge timing verification

Tested head: ef67fd2b32e6801da8e1e99d63ac20ddbc12aeb6. Base: 93273e6361b7a27a8ed251e9182a9099f24ab4dc (merged Generator Studio #977, including #981 normal 30 TPS). This is evidence for simulation revision 39 / save format 150, not later master revisions. Follow-up #984 refreshed two native-derived simulation metadata fields, with no runtime changes.

During verification, master advanced to a1796e346810e0d71e16acd1fbf764ff046343b0 (#974 persistent random streams, simulation revision 40 / format 152). That change already refreshes Generator Studio's native references, superseding #984. The older reference correction is closed rather than merged over those newer values. The original feature and independent review remain recorded on #977. This report does not claim browser verification of simulation revision 40.

## Environment and checks

Linux x86_64, GCC 15.2, Emscripten 4.0.15, bundled Node 24.19.0, existing pinned SDL3 and recording libraries, installed Playwright Chromium/Firefox/WebKit. Native release -O3; browser release -O2 with wasm exceptions, plus pthread for threaded build. No dependencies added. Build logs preserve flags and inputs.

Native release client, engine/unit harnesses, serial and threaded browser builds pass. All three platform TypeScript configurations and lint pass. Focused platform tests pass 14 files / 75 tests. Frozen native preview passes 34 assertions; isolated generator/preview/screen/area-effects/session/speed suite passes 65 cases. Final native preview reports clean tested commit and sourceTreeSha256 f8cefa7c5b5865b7a1fa9bbbccb91a0970be9c29234ba8713aa366aae0a64673.

Native generation and initial-save loading produce byte-identical 64-tick traces (197652 bytes, SHA256 40ff4af71d2304d27a1429200fa17305b0352038fc978e97b03e2ef11bc8af07). AI Studio's 1024-tick native reference remains 1819354 bytes, SHA256 9bc4ef614d6950d53682c4bf8c0c909734d6380f85d3059c4cc56d17d6bc3621. CLI traces were recorded at the merged runtime source before the reference-only commit; runtime inputs are identical at the tested head.

Browser matrix: 22 passed, one interactive threaded WebKit skip, one Firefox threaded Watch failure (Emscripten _emscripten_thread_profiler_init assertion). Isolated repetition of that Firefox case: two passed, one failed with the same assertion. This is an unresolved intermittent runtime failure; these runs are not represented as fully green. Headless trace comparisons pass across all six browser/runtime combinations. The runtime failure must be investigated before enabling the feature. Exact rerun: node browser/node_modules/.bin/playwright test -c browser/playwright.config.js browser/tests/generator-studio.spec.js --project firefox --grep 'watches its colonies \(threaded\)' --repeat-each 3.

## Commands

Run from repository root with bundled Node on PATH. Exact native CLI commands and isolated test flags are included as tickrate-native-traces.sh and tickrate-native-suite.sh. commands.md includes the focused platform test file list.

```sh
CCACHE=1 GLOB2_RECORDING_PREFIX=$PWD/artifacts/generator-studio/recording-prefix GLOB2_SDL3_PREFIX=/tmp/glob2-sdl3/prefix scons release=1 -j16 --build=artifacts/generator-studio/build artifacts/generator-studio/build/src/glob2 engine-tests unit-tests
CCACHE=1 scons target=web release=1 -j16
npm --prefix platform run typecheck
npm --prefix platform run lint
node browser/node_modules/.bin/playwright test -c browser/playwright.config.js browser/tests/generator-studio.spec.js browser/tests/studio.spec.js
```

Final incremental builds used -j8. The archive contains fresh native/browser reports, packages, saves, replay checksum traces, screenshots and logs with SHA256SUMS.

## Limits

Live Watch tests establish frozen metadata, tick progression, pause and teardown; CLI generation/save-load comparisons establish cross-platform per-tick trace parity. Per-tick parity through the live Watch path has not been established. Interactive threaded WebKit cannot share worker memory in this installation; the case is explicitly skipped while headless threaded WebKit is covered. Windows/macOS/Android native execution and full deployed Docker/Caddy stack were not exercised. Full desktop/phone authoring flows passed on the feature's previously recorded tested head; they were not repeated for this metadata-only correction. Paid providers/Stripe use fakes. Production deployment and human maintainer playthrough remain pending; Generator Studio remains disabled by default.
