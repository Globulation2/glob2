# PR #220: WebP integration and pipeline verification

Tested head: **a2dfe14031c45f1725839993129abc1199d7ceea**. Rebased directly onto
master **e444bebb05c9c3db8522bf453984c858c06b643a**; master was fetched again
before final verification and publication. The native test binaries report this
exact clean head in [their build provenance](native-render/build-provenance.json).
The PR remains unmerged. This evidence is intentionally kept on a separate branch.

Environment: macOS 26.6.2 arm64, Apple clang 21.0.0, Python 3.14.7. Build flags:
`release=1 server=0`, `-g -std=gnu++20 -Wall -fPIC -O3`, OpenGL enabled, eight
build workers. Dependencies were built with the repository's pinned helper:
SDL 3.4.16, SDL_image 3.4.6, SDL_ttf 3.2.2, SDL_net 3.2.0 and libwebp 1.6.0.
The shared encoder uses Pillow 12.2.0/libwebp 1.6.0. Optional validation/candidate
venv: Pillow 12.2.0, NumPy 2.5.3, SciPy 1.18.1.

## Results and coverage

- Approved sources: 2,287 frames, 3,370 layers, eight atlas mips; production hashes,
  paired layers, complete legacy world coverage, manifest/index geometry and
  provenance all pass. All 6,756 PNG blobs in the source pack and production
  folders are identical to the previous PR head, `13dc865444ea9fbeb41538d8f6d19d25d7136978`.
  That head is preserved on `codex/archive-pr220-before-webp-20261005`.
- Source terrain: **91,136 directed joins**, all corner junctions and padded
  borders through four mip levels pass. All 65 resource frames and atlas mips,
  recovered-source pixels, constrained alpha and periodic opaque water pass.
- Complete release and lossless exports both build through the shared encoder.
  Every one of the **3,378 HD WebP layers/atlases** passes decoding, source/output
  hashes, shared encoding policy, exact alpha, geometry and runtime-index checks.
  Lossless selections additionally preserve exact RGBA. The actual SCons runtime
  tree passes the same independent check. Source PNGs and provenance metadata do
  not leak into the shipped HD directory.
- **19 pipeline regression cases pass**, normally and with `python -O`: seven
  standard-library production checks, six optional candidate checks, and six
  decoded-export checks. They cover failure before writes, duplicate/invalid
  layers, unknown recipes, stale/symlinked exports, timestamp stability, candidate
  staging, fake paired-inference failure, alpha, geometry, model-size errors,
  encoding policy, runtime naming and generated terrain audit records.
- Final build contracts: **334 cases run, 333 passed, one skipped**, exit 0. Five
  music-encoding cases were explicitly excluded because this machine's FFmpeg
  cannot start: it references missing Homebrew `libjxl.0.11.dylib`. An earlier
  unfiltered attempt ran 338 cases, with 335 passes, two FFmpeg errors and one
  skip; those failures are retained. `fontTools` is unavailable, so the optional
  core-font glyph check is skipped. The affected artwork contracts are included
  in the final successful run. No unrelated music code was changed.
- Native release client and both test binaries build. **37 native cases pass**,
  zero failures/errors/skips, across ImageAssets, SpriteLoad, SpriteSheets,
  AssetLoader, UnitHighResolutionCache, HighResolutionIntegration and TerrainRuntime.
  The runner groups them into 18 jobs. Software/OpenGL integration covers game,
  editor, replay, camera/zoom, hue recoloring, effects, cache bounds and fallback.
  Drawing/RNG/checksum assertions pass. Source continuity confirms no changes to
  engine/renderer/library code or classic artwork against current master.

The maintained renderer/exporter retains ownership of decoding, recoloring,
atlas admission and WebP encoding. The candidate tool is optional offline
staging, with no inference during builds and no automatic promotion. Read the
[code and architecture review findings](review.md) for the refactoring rationale.

## Exact commands

Run from the repository root at the tested head. Initial dependency/build commands:

```sh
python3 -m venv artifacts/pr220/python
artifacts/pr220/python/bin/pip install Pillow==12.2.0 numpy scipy
python3 scons/sdl3_dependencies.py --prefix build/sdl3/prefix --work build/sdl3/sources --jobs 8
GLOB2_SDL3_PREFIX=build/sdl3/prefix scons -j8 release=1 server=0 tests build/darwin/client/release/src/glob2
```

The initial build was repeated on the final revision to refresh the compiled
provenance header and relink the test binaries. Both build logs are retained.

```sh
python3 tools/artwork/package_runtime.py --check
python3 tools/artwork/runtime_provenance.py --check
python3 -O tools/artwork/package_runtime.py --check
python3 -O tools/artwork/runtime_provenance.py --check
artifacts/pr220/python/bin/python tools/artwork/validate_runtime.py
artifacts/pr220/python/bin/python -O tools/artwork/validate_runtime.py
python3 tools/package_assets.py --output artifacts/pr220/runtime-release --platform macos
python3 tools/artwork/package_runtime.py --runtime-output artifacts/pr220/runtime-lossless --platform macos --lossless-images
artifacts/pr220/python/bin/python tools/artwork/validate_runtime.py --export artifacts/pr220/runtime-release
artifacts/pr220/python/bin/python tools/artwork/validate_runtime.py --export artifacts/pr220/runtime-lossless
artifacts/pr220/python/bin/python tools/artwork/validate_runtime.py --export build/darwin/client/release/runtime-assets
artifacts/pr220/python/bin/python tools/artwork/ai/upscale.py --frame inn0b0 --output artifacts/pr220/prepared-inn --prepare-only
artifacts/pr220/python/bin/python -m unittest discover -s test/build_system -p test_artwork_package.py -v
artifacts/pr220/python/bin/python -O -m unittest discover -s test/build_system -p test_artwork_package.py -v
artifacts/pr220/python/bin/python -m unittest tools.artwork.ai.test_upscale tools.artwork.test_validate_runtime -v
artifacts/pr220/python/bin/python -O -m unittest tools.artwork.ai.test_upscale tools.artwork.test_validate_runtime -v
```

All commands above exit 0. The first lossless-export validation attempt exposed
an over-broad duplicate-source audit check in the new validator; it was corrected
because the shared terrain compiler emits several outputs from one catalog.
The final validator and a regression case distinguish that from duplicate HD
layer records; final release, lossless and native validations all pass.

```sh
python3 -m unittest discover -s test/build_system -v
python3 artifacts/pr220/run_build_contracts.py
python3 test/run_tests.py --filter 'ImageAssets/*' --filter 'SpriteLoad/*' --filter 'SpriteSheets/*' --filter 'AssetLoader/*' --filter 'UnitHighResolutionCache/*' --filter 'HighResolutionIntegration/*' --filter 'TerrainRuntime/*' --display-jobs 1 -j4 --junit artifacts/pr220/native-tests.xml --artifacts artifacts/pr220/native-render --write-inventory artifacts/pr220/native-inventory.json
```

The unfiltered build-system command exits 1 for the two FFmpeg errors described
above; the supplied [final contract runner](run_build_contracts.py) explicitly
excludes all five music cases and exits 0. It was run with the shared encoder's
Python interpreter. The final artwork package and optional pipeline cases were
rerun after the last edits. The broader successful contract run used the same
package/exporter inputs before the last validator-only correction. Native
commands and final complete-export validation target the final head.

## Evidence and limits

[Native JUnit](native-tests.xml), [selected-case inventory](native-inventory.json),
[final build log](build-final.log.gz), [native test log](native-tests.log.gz),
[final build contracts](build-contracts-final.log.gz),
[original broad failures](build-system-tests.log.gz),
[FFmpeg dependency failure](ffmpeg-environment.log.gz),
[release validation](validate-release-export.log.gz),
[lossless validation](validate-lossless-export.log.gz),
[native-export validation](validate-native-export.log.gz),
[release audit](runtime-release.json.gz), [lossless audit](runtime-lossless.json.gz),
[export sizes](export-metrics.json). Logs and audits use deterministic gzip;
uncompress them to read the complete records. Full runtime trees can be rebuilt
from the committed approved sources with the commands above.

The HD directory, including its index, is **12,674,911 bytes in release** and
**40,729,987 bytes in lossless**, from 60,675,369 source bytes. This compares the
same source selection under two runtime profiles, not old versus new releases or
store download sizes. Release chose lossy WebP for 2,290 HD files. Geometry and
alpha are exact; RGB may differ. No performance improvement is claimed.

Visual inspection covered matched source/HD comparisons, approved PNG versus
release WebP, normal gameplay/editor captures and repeated map boundaries.
The inspected scenes show intact silhouettes, shadows and team recoloring, with
no obvious broken terrain/water joins. This is automated capture inspection,
not a human hands-on gameplay/performance acceptance.

[Classic versus approved HD](source-comparison.png),
[approved PNG versus release WebP](release-comparison.png),
[PNG versus lossless WebP](lossless-comparison.png), and the full
[native render captures](native-render/HighResolutionIntegration/HD_artwork_cursor_scaling_and_replay_fixture_in_OpenGL_display_1024x768_artifacts_writes-preferences/runtime-check/).
Comparison scripts are supplied beside the images. Prepared candidate inputs and
[source/tool version recipe](prepared-inn/recipe.json) are also retained.

Omitted: real external model inference, Windows/Linux/mobile/browser engine runs,
physical low-memory GPU/device measurement, fullscreen transitions, full engine
suite, live multiplayer, and human play/performance acceptance. Shared exporter
and browser-package contracts do not establish browser gameplay coverage.
No simulation/save/replay/network format or revision change is made by this PR;
engine sources are identical to its master base. Cross-platform simulation trace
comparisons are not needed for this artwork/tooling-only update and are not claimed.
Experimental terrain beyond the 272 legacy connected HD tiles continues through
the shared compiler and native fallback. Exact source seam equality is checked
before encoding; lossy WebP is not claimed to preserve exact RGB seam equality.

Maintainer acceptance: prepared for review; no human acceptance is attributed.
