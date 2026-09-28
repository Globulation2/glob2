# PR #208 cleanup evidence

Implementation revision: `dccb26a5f581040b0d5c85cfb52b43af84af95a7`.

This branch contains review evidence only. The implementation branch has no generated captures, logs, profiles, or validation reports.

## Checks

- 203 CppUnit cases.
- 119 newly registered keys across 33 supported catalogs; strict structural, numbered-placeholder, shared-vocabulary and font-coverage checks.
- 24 UTF-8 wrapping cases, 30 build-system checks, 15 browser unit tests and four structural checks.
- Native mobile input, responsive menu, mobile presentation, gameplay touch/editor, portable renderer and text-raster harnesses. Menu-colony isolation and actual navigation also pass; the harness targets the composed Cancel control and has a 60-second watchdog.
- Linux software-GL text-raster reproduction and correction, including fractional scales and nearby cache reuse. The original pixel-accuracy threshold is unchanged.

All nine focused Chromium/WebGL2 cases passed on the final implementation revision. They cover Unicode editing through rotation, mixed touch/mouse resizing, quota and transaction-failure recovery, tutorial/results return paths, campaign editor navigation, editor cancellation and successful saving. The browser captures were inspected as well as the native localized captures. These nine cases were rerun successfully after the final preview-status and navigation follow-ups.

Hosted CI is still running: [desktop/browser build](https://github.com/Globulation2/glob2/actions/runs/36488613318) and [Android builds](https://github.com/Globulation2/glob2/actions/runs/36488613170). No completed hosted-validation claim is made yet.

## Captures

The gameplay harness renders the production views in German and Japanese at 320×568 and 568×320 host points. Retina captures contain twice as many pixels. Keyboard workspace captures reserve only 120 host points for the editor; the black remainder represents the simulated keyboard occlusion, not a physical keyboard capture.

The localized file-dialog fixture intentionally supplies the literal title `Save map`, a test filename, and bundled map names. Those fixture strings are not localization keys; production callers provide translated titles. Other labels and controls use the real catalogs.

`before/` preserves the initial clipped German results actions and the earlier keyboard-caption captures. Final captures show wrapping within the original action bounds. Test assertions retain action identities, chart expansion, existing editor dispatch, and unchanged game checksum/order count across localized rendering.

Native frontend fixtures use a borderless window to fit 844-point tall views on the laptop screen. Exact viewport-size assertions remain enabled.

## Scope and limits

No new physical Android/iOS or real-device IME qualification was performed for this cleanup. Previous device and full browser evidence is linked in the PR and identifies its own tested revision. Translations were authored and checked during this pass; this is not a claim of review by native speakers of every language.

The four pre-existing untracked files remain outside the commits. Reusable gallery sources, app icons, certificate fixtures and pinned SDK metadata are retained.

## Reproduction commands

Run from the repository root at the implementation revision. Each graphical harness needs an isolated `GLOB2_USER_DATA_DIR` under ignored `artifacts/`.

```sh
scons -C test -j6
./test/TestsRunner
python3 data/check_translations.py --strict
python3 test/test_translations.py
python3 test/test_font_coverage.py
python3 test/test_text_area_layout.py
python3 test/TouchPresentationStructureTest.py
python3 -m unittest discover -s tests/build_system
node --test browser/unit/*.test.js
scons release=1 -j6 gameplay-touch-test responsive-menu-test mobile-presentation-test mobile-input-test portable-renderer-test text-raster-test
```

On macOS, run the three application harnesses from `build/darwin/client/release/src/`, and `MobileInputHarness`, `PortableRendererHarness`, and `TextRasterHarness` from `build/darwin/client/release/libgag/src/`. The text-raster harness accepts a capture directory as its argument. The Linux reproduction used software GL under `xvfb-run -a -s "-screen 0 1600x1400x24"`, with `LIBGL_ALWAYS_SOFTWARE=1`.

The frontend safe-area captures also use fixture landscape titles and deliberately capture before asynchronous preview generation completes; they demonstrate bounds, labels and fixed actions rather than the generated terrain.

The focused browser command, run from `browser/` after `scons target=web release=1 -j6`, was:

```sh
GLOB2_CHROMIUM_ANGLE=metal GLOB2_TEST_RENDERER=webgl2 npx playwright test tests/responsive-presentation.spec.js tests/editor-storage.spec.js tests/single-player.spec.js --project=chromium --workers=1 --grep 'native text editing|touch setup and gameplay|editor save before quit|tutorial sessions|game rules and AI|editor setup and campaign|map editor frames|editor save cancellation'
```

`editor-save-failure.png` captures deliberate fault injection in passing recovery tests, not an unexpected test failure.

The editor-setup fixture includes a deliberately undersized generation request. Its preview can fail validation; the captures verify the real error-state labels and their wrapping inside the smallest landscape slot. This does not indicate a failed presentation test. `before/new-map-ja-568.png` preserves the initially clipped status text.
