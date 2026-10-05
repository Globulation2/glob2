# WebP-only runtime artwork verification

Worktree base: b58be00a245f801442ab330bf0e4b9e14dfa431b.
Fetched master before validation: f2f636ebf539d56f796bdf52c01d3e9c072719a9.
Final fetch: 84ca8851653fb0414f9dd6c87ed5bcccd7f172e9; only platform map-preview
and documentation changes followed. `master-final-changes.txt` records them.
Inspected newer translations, CI and test registry changes. Browser language-name
characters are unchanged; affected loader/rendering test registrations and native
codec configuration are unchanged. No merge or rebase was needed.
`master-integration.json` and `master-test-integration.diff` record the checks.

Ubuntu 26.04.1 x86_64, GCC 15.2, Python 3.14, pinned Pillow 12.2.0/libwebp 1.6.0,
fontTools 4.64.0, Emscripten 4.0.15. Native dependencies use the repository pins
through build/sdl3-ci/prefix. Xvfb and Chromium SwiftShader provide software GPU
coverage; physical GPUs/devices and other OSes are unavailable.

The exporter validates dimensions/alpha and full lossless RGBA. It verifies sprite
sheet and HD terrain/resource atlas placement against the original source before
encoding. Source PNGs remain unchanged. Runtime artwork and override names are
WebP; SDL PNG/JPEG decoding stays enabled for imports/previews/skin textures and
PNG/JPEG output features. Original exports are offline measurement baselines.

Pinned build-system suite: 322 tests, 321 passed, one skipped (NSIS unavailable).
Full native and web builds pass. Independent exports equal build-export audits.
SDL_image pair verifier: 4354 Linux pairs, 4353 web pairs and 4354 lossless-profile
pairs pass exact dimensions/alpha and full lossless RGBA. Source/output hashes and
browser package byte ranges/content-addressed filenames verified by measure.py.

Native image/sprite/skin suites: 15 cases pass, plus the retained PNG material-map
case. Browser tutorial/resize/team color cases: 2 pass (software and WebGL2).
Browser review captures both menu and advancing tutorial (>50 ticks) in both
renderers; no page errors. Inspected the captured artwork: legible lettering,
clean sprite boundaries/alpha, team red/cyan, and continuous grass/water terrain.
Native engine cases: 14 pass across ThemeCatalog, RuntimePack,
UnitHighResolutionCache and HighResolutionIntegration. Initial HD assertions
incorrectly assumed optional terrain/resource atlases existed; corrected the
fixture to explicitly activate HD after staging setup and validate the available
HD resource frames/native terrain fallback. Final HD cases pass in software and
OpenGL; native-hd-final.xml and screenshots record the result.

Added synthetic Q90 atlas fixture (9,422 bytes): independently encoded RGB differs
from the frame while alpha matches. The native SpriteSheets display case passes,
proving actual atlas acceptance, exact per-frame alpha and logical dimensions.
Final loader run passes 12 cases; prior SkinMesh run adds 5 distinct cases.

Captured 80 native animation frames (native-animation.mp4) and 20 lossless-profile
comparison frames. Inspected native menu/wordmark, resource/team-color sheet,
HD gameplay at 200%, and seam-corners capture, alongside browser menu/tutorial
captures in both rendering paths. Lettering stays readable, visible alpha edges
remain clean, recoloring is coherent and grass/water seams show no new gaps.
The bundled pack has selected HD resources and unit artwork; complete HD terrain
and prepacked HD atlases are absent. The atlas fixture supplies that loader coverage.

Runtime artwork: 4,353 WebP images = 2,638 lossy + 1,715 lossless, 8,987,802 bytes.
Browser derivatives reduce packaged artwork to 8,887,278 bytes. Explicit lossless
WebP artwork totals 21,426,776 bytes. Linux alone adds its original 963,139-byte
public PNG screenshot. Full native export: 150,923,071 bytes; web export:
149,959,932 bytes. Fonts, mesh/music bytes and required icon containers retain
existing behavior. `sizes.json` records package/category measurements.

Unavailable: native Windows/macOS builds, Android/iOS devices/emulators, Firefox,
WebKit, physical GPU comparison and maintainer hands-on play. NSIS test skipped
because its compiler is absent. No simulation/save/replay/network version changed;
the HD integration suite checks identical simulation checksums between artwork modes.

## Reproduction commands

```sh
GLOB2_SDL3_PREFIX=build/sdl3-ci/prefix scons -j12 release=1 server=0 unit-tests engine-tests build/linux/client/release/test/MenuColonyHarness
scons target=web release=1 -j8
"$(python3 tools/package_assets.py --encoder-python)" -m unittest discover -s test/build_system -v
"$(python3 tools/package_assets.py --encoder-python)" tools/package_assets.py --platform linux --output artifacts/webp-only/native
"$(python3 tools/package_assets.py --encoder-python)" tools/package_assets.py --platform web --output artifacts/webp-only/web
"$(python3 tools/package_assets.py --encoder-python)" tools/package_assets.py --platform linux --lossless-images --output artifacts/webp-only/lossless
"$(python3 tools/package_assets.py --encoder-python)" artifacts/webp-only/measure.py
artifacts/q90/verify-decoder artifacts/webp-only/native-pairs.txt
artifacts/q90/verify-decoder artifacts/webp-only/web-pairs.txt
artifacts/q90/verify-decoder artifacts/webp-only/lossless-pairs.txt
python3 test/run_tests.py --binary unit --filter 'SpriteSheets/*' --filter 'ImageAssets/*' --filter 'SkinMaterialMap/*' --artifacts artifacts/webp-only/native-loader-final --junit artifacts/webp-only/native-loader-final.xml
python3 test/run_tests.py --binary engine --filter 'HighResolutionIntegration/*' --filter 'UnitHighResolutionCache/*' --filter 'RuntimePack/*' --filter 'ThemeCatalog/*' --artifacts artifacts/webp-only/native-render --junit artifacts/webp-only/native-render.xml
python3 test/run_tests.py --binary engine --filter 'HighResolutionIntegration/*' --artifacts artifacts/webp-only/native-hd-final --junit artifacts/webp-only/native-hd-final.xml
GLOB2_ASSET_DIR="$PWD/artifacts/webp-only/native" GLOB2_USER_DATA_DIR="$PWD/artifacts/webp-only/native-profile" SDL_AUDIODRIVER=dummy xvfb-run -a build/linux/client/release/test/MenuColonyHarness record "$PWD/artifacts/webp-only/native-animation" 80
GLOB2_ASSET_DIR="$PWD/artifacts/webp-only/lossless" GLOB2_USER_DATA_DIR="$PWD/artifacts/webp-only/lossless-profile" SDL_AUDIODRIVER=dummy xvfb-run -a build/linux/client/release/test/MenuColonyHarness record "$PWD/artifacts/webp-only/lossless-animation" 20
python3 browser/serve.py 8874 --bind 127.0.0.1 --directory build/emscripten/client/release
node artifacts/webp-only/browser-review.js
# From browser/:
GLOB2_TEST_URL=http://127.0.0.1:8874 GLOB2_CHROMIUM_ANGLE=swiftshader npx playwright test tests/team-colors.spec.js --project chromium --output ../artifacts/webp-only/browser-team
```

The first combined native run retains its failed assumption in native-render.xml;
use native-hd-final.xml for the repaired HD cases. Early attempts and failure logs
are retained, not counted as successful verification. Changes after production
validation only strengthen native test fixtures; the synthetic atlas test and
final HD cases cover those edits.


## Committed revision and master integration

Tested source is commit 7d1e47dd54d0d9c5c23ad91345b9b5d17b39c268. Every changed
file matched tested-worktree.json before committing; git diff --check passed.
Master 84ca8851653fb0414f9dd6c87ed5bcccd7f172e9 merged without conflicts.
Synthetic merged revision 508c85199716ff95bb7ff8f291e279932052d11f has both
revisions as parents. Its only overlapping files are documentation and the test
registry; native/browser production code is unchanged from the tested source.
The pinned build-system suite was rerun against that merged tree, with output in
integration-build-system.log. Command: pinned encoder Python -m unittest discover
-s test/build_system -v.

An initial integration command accidentally selected all test/ discovery instead
of test/build_system; it was interrupted while running unrelated tournament SSH
fixtures. That incomplete broad run is retained as integration-python.log and is
not passing verification. The correctly scoped build-system run supersedes it.
