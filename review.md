# Independent terrain-cache review

Reviewed revision: `bd7b3710e070368cd1474927862d188673ebc876`
Base: `7e54a3fc581d22d32e7b97a6aee76dbfeae58e9f`
Reviewer: independent sub-agent, read-only code review (apart from this ignored evidence file).

## Finding requiring correction

**P2 — Respect the active offscreen raster scale when selecting terrain density.**

`src/render/SoftwareTerrainCache.cpp:67-69` multiplies map zoom by the window's drawable-to-logical ratio. This ignores the existing offscreen override: `GraphicContext::setRenderTargetScale`, exposed through `getRasterScale()`. `src/render/torus/TorusViewRender.cpp:610` deliberately sets that override to `pixelsPerCell / (32 * shownZoom)`, keeping native capture at one texel per map pixel even when the map transform is zoomed out. On a 1x window with map zoom .25 and a native capture, the current cache interprets density as .25 instead of 1. A large capture can therefore select pages reduced by two or four and magnify them beyond the documented sqrt(2) bound. Capture detail also becomes dependent on the desktop display's DPI despite the independently sized capture texture.

Suggested fix: calculate density from `target.mapTransformScale() * target.getRasterScale()`, with a comment explaining that this includes the offscreen target override. Add a regression proving that zoom .25 with target scale 4 retains native page density for a bounded tiled capture, then exercise a genuinely reduced target and verify that narrow horizontal and vertical capture edges retain the whole capture's selected downsample factor and reuse pages. The existing HD tiled test exercises the resolution factor but does not cover this new downsample factor.

## Other reviewed boundaries

- Whole-view density propagation to streamed pages and tiled capture edges is structurally correct.
- Page crop coordinates stay tile aligned, and all allowed powers-of-two divisors divide the native 32-pixel tile.
- Alpha-weighted reduction retains straight alpha and has safe accumulator bounds even at divisor 32.
- Density/backend changes invalidate existing pages; unchanged warm pages allocate no scratch surface.
- Fixed recipe/bookkeeping allowance is retained at reduced densities, and `bytes()` shares the same accounting function as admission/eviction.
- Software pages remain at native density, preserving their opaque-run paths.
- Documentation clearly distinguishes warm-frame improvements from cold composition cost and calls out softened distant texture grain.
- `git diff --check origin/master...HEAD` passed.

No additional blocking findings. Native/browser builds and tests are being run by the parent agent; this review did not duplicate those expensive runs. The raster-scale issue above should be fixed and its regression run before merge.

## Follow-up resolution review

Re-reviewed commit `a1e2c59a392ef7523656fda2747c8d34ccdd65f7` after the author implemented the feedback.

The finding is resolved: density now uses `mapTransformScale() * getRasterScale()`, with an explicit comment describing the offscreen override. The new `offscreen terrain density follows the target and stays stable across capture tiles` case exercises native-density and reduced-density offscreen targets at both 1x and 2x window DPI, verifies narrow horizontal/vertical subregions reuse the whole-capture density without rebuilding, checks the budget, and restores drawing state with RAII. This directly covers the identified regression and the new tiled reduction propagation. Documentation now describes the active target's raster scale.

**No remaining blocking findings after the fix.** Final compilation/runtime evidence remains the author’s validation responsibility; this follow-up assessment is based on independent inspection of the exact code changes and their regression assertions.
