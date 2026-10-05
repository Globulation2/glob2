Final integration addendum

Tested commit: 2da4c8b751d9b3e40f760099f8e40f582e3f276c
Current master base: bc550ae68cba595f6c26fbb690048a73b889e814

Master #759 only changes image codec test inventory. Resolved the overlapping dual PNG decoder loop with master's implementation and updated the browser count from four to five for the new prepared image adoption case. Independent integration reviewer confirmed the five browser cases and CPU-only ownership test assumptions. No production code changes relative to the main verification report.

Same Linux x86_64 toolchain, dependencies and release flags as verification.md.

Commands:
GLOB2_SDL3_PREFIX="$PWD/artifacts/asset-loader/native-prefix" scons -j8 release=1 unit-tests
GLOB2_ASSET_THREADS=4 SDL_VIDEODRIVER=x11 LIBGL_ALWAYS_SOFTWARE=1 python3 test/run_tests.py --binary unit -j4 --display-jobs 1 --filter 'ImageAssets/*' --junit artifacts/asset-loader/review-master-inventory.xml --artifacts artifacts/asset-loader/review-master-inventory
node --check browser/tests/image-assets.spec.js

All pass: six native ImageAssets cases, zero failures. Browser syntax passes; full wasm/browser runtime omission remains as documented in verification.md. Full feature validation applies to unchanged production code at 2071eea.
