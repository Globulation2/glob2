Local integration verification for PR #1149

Tested revision: `45854f74ccbede782c7507c8ae7b35018ad16c6a`; base revision: `3510b4c4ee860428561d561b959e6c89e96c4b37`. Latest master was fetched and integrated because it touched the same gameplay-touch components. No conflicts.

Environment: macOS 26.6.2, ARM64, Apple Clang 21.0.0 (clang-2100.3.34.2), Python 3, SCons development build with dev_fast=1 (-O0 -g1), compiler caching, linker=auto. Existing managed dependency prefix build-dd8d40b0b2e044b88374d32e; exact compiler/include/link invocations and dependency paths are retained in the compressed build log. No dependency changes.

Commands run from repository root:

```sh
PYTHONPATH=$PWD python3 tools/dev_build.py engine-tests mobile-gallery -j6
GLOB2_UI_ONLY=hand-preference python3 test/run_tests.py --build-dir build/darwin/client/debug/dev-dev_fast-true-linker-auto --binary engine --filter 'SettingsGraphics/*hand*' --filter 'UIPresentation/*small portrait*' --filter 'UIPresentation/*small landscape*' --filter 'GameGUITouch/actual gameplay touch*' --display-jobs 1 --artifacts artifacts/hand-controls-merge-tests --junit artifacts/hand-controls-merge-tests.xml
python3 data/check_translations.py --strict
git diff --check
```

Results: native engine-tests/mobile-gallery build passed; 6 tests passed, 0 failed, 0 skipped (128.0 seconds); strict translation audit passed, zero structural errors; whitespace check passed. Tests cover preference migration and persistence, restoring state after write failure, actual illustrated-choice activation for both sides, portrait/landscape at 100%/150% text with safe insets, upgrade/repair/cancel order dispatch, duplicate suppression, and unchanged simulation checksums around HUD interactions. Final screenshots are lossless PNG conversions of BMP captures from this exact revision.

Coverage rationale: shared native touch UI, settings persistence and existing order dispatch are the changed boundaries. No simulation code, game save format, replay format or network format changes, so no SIM_REVISION bump or cross-platform simulation trace is needed for these UI changes. Native input, layout and real gameplay order harnesses exercise the risks directly.

Limits: Android APK packaging, iOS, browsers, Windows/Linux, physical touch comfort, release packaging and representative performance were not reverified. These are native macOS touch previews, not Android device captures. New translations use approved English fallback. An earlier full Python translation suite retained 320 pre-existing Discord fallback-value failures; baseline had 384 (the change also repairs 64 missing GPU settings entries). Strict translation structure passes. Hosted PR checks are cheap contracts only, not full platform verification.

Feel: construction controls move into the radial lane, and first touch launch asks the holding hand. The user reviewed and approved the four-pass refined hand graphic before requesting merge. No gameplay pacing or rules change.
