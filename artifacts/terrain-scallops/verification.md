Local / VM verification

- Tested commit SHA: `771e73cf33759113453e0909bc5c80cd72a04111`.
- Base revision and integration state: `a1796e346810e0d71e16acd1fbf764ff046343b0`; PR head is directly based on current master. Master fetched before final validation; no newer base changes.
- Environment: macOS arm64; Apple clang 21.0.0 (clang-2100.3.34.2), target arm64-apple-darwin25.6.0. Native host, no VM.
- Dependencies: repository-vendored nlohmann-json and doctest 2.4.11; shipped data/terrain/tileset.json. No dependency changes. Focused build does not link SDL or full engine globals.
- Build configuration: C++20, -O2 -Wall; sanitizer build uses -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer.
- Coverage rationale: presentation-only contour generation and sampling; wrapped seams and weight normalization; halo/fallback/sharp-border behavior; native and HD corner regions; 128 additional look seeds for all nonuniform corner patterns; twelve-pixel displacement bound; deterministic seeded scalloping in both orientations without contour folding.
- Results: optimized and sanitizer runs each passed 5 test cases / 611,554 assertions, exit 0. No sanitizer diagnostics. git diff --check passed.
- Omitted checks: full engine/renderer suite, SDL compositor/cache integration, live game captures, performance benchmarks, Windows/Linux/mobile/browser rendering. Cold full engine build was stopped before completion. This evidence does not claim full engine or cross-platform verification.
- Compatibility scope: only render/terrain source and its tests plus authoring documentation changed. No simulation state, orders, randomness, serialization, replay acceptance or network/sim-version gates changed; simulation checksum comparisons, save/load and replay/network boundary tests were not run because those computations/formats are unaffected.
- Visual evidence: after.png uses the actual runtime coverage masks over repeated shipped grass/sand textures, map look seed 73, 32x32 vertex domain, viewed crop 24x16 cells. It is a diagnostic render, not a live game screenshot (lighting, fringe and seam shading are absent). deeper-comparison.png is the approved preview comparing the prior tuning pass at left with final geometry at right; its left half is retained visual evidence from the earlier uncommitted tuning pass.
- Harness provenance: focused.cpp extracts the first five non-display TerrainMaterials cases from the tested source. It replaces HeadlessGlobals/catalog loading with direct JSON catalog parsing and retains the test assertions. It compiles the actual runtime TerrainMaterials.cpp. preview.cpp uses the same runtime code and recipes to emit an 8-bit grass coverage PGM; preview-images.py applies shipped texture pixels and assembles the comparison.
- Maintainer acceptance: requesting maintainer approved the final deeper preview for merge in the chat (“thats better. approved for merge.”), with the full integration limitation disclosed before approval.

Exact commands (run from repository root at tested revision):

```sh
clang++ -std=c++20 -O2 -Wall -Isrc -Isrc/render/terrain -Ithird_party/nlohmann-json/include -Itest/third_party artifacts/terrain-scallops/focused.cpp src/render/terrain/TerrainMaterials.cpp -o artifacts/terrain-scallops/focused
artifacts/terrain-scallops/focused > artifacts/terrain-scallops/focused.log
clang++ -std=c++20 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Isrc -Isrc/render/terrain -Ithird_party/nlohmann-json/include -Itest/third_party artifacts/terrain-scallops/focused.cpp src/render/terrain/TerrainMaterials.cpp -o artifacts/terrain-scallops/focused-sanitized
artifacts/terrain-scallops/focused-sanitized > artifacts/terrain-scallops/sanitized.log 2>&1
clang++ -std=c++20 -O2 -Isrc -Isrc/render/terrain -Ithird_party/nlohmann-json/include artifacts/terrain-scallops/preview.cpp src/render/terrain/TerrainMaterials.cpp -o artifacts/terrain-scallops/preview
artifacts/terrain-scallops/preview artifacts/terrain-scallops/after.pgm
python3 artifacts/terrain-scallops/preview-images.py
git diff --check
```

The full-build attempt was `scons -j8 release=1 server=0 engine-tests`; it was interrupted intentionally and is not a passing validation result.
