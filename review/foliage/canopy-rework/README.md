# Canopy rework evidence

Code commit: `39243cc853e3c986c262ba2119798d9d72345c49`. Tested base: `ddfa81a405e0c47442e1787ccdf7d4e95ca57d6e` (master, unchanged at publication).

This rework replaces the 4×4 canopy block from `53110681a3` with corner Wang tiles, a crown hierarchy, a raised lit outline, a ground shadow and brighter palettes. The evidence one level up covers the earlier commit.

- `final-gallery.png`: native renderer output of `TerrainValidation/connected foliage corners, strips and filled patches display artifacts` at the tested commit.
- `canopy-before-after.png`: the earlier final gallery (left) next to this one (right), same test case.
- `canopy-before-after-detail.png`: the large hedge and thicket patches, enlarged.
- `native-tests.log` / `.xml`, `decor-tests.log`, `tileset-tests.log`, `package-check.log`, `diff-check.log` (empty: no issues), `native-build.log`.
- `validation.json`: commands, environment, coverage and limits.

All 53 selected native cases, 7 artwork tests and 3 catalogue tests passed on the tested commit with no reruns, as did packaging and `git diff --check`. Validation used macOS arm64, Apple clang 21, pinned Pillow 12.2 and a fast development build. Other platforms and release performance were not tested. Known limits: a faint cell-sized grain remains visible in large hedge patches at full zoom, and the hedge/thicket boundary is still a straight colour change. Maintainer play review has not been recorded.
