# Software rendering

CPU rasterization, retained terrain and backend ownership. See [rendering benchmarks](../development/rendering-benchmarks.md) for timing evidence.

## Software rendering architecture and profiling

`GraphicContext` remains the drawing facade and retains existing capability queries.
It owns the accelerated backend and software backend independently; transformed passes
borrow them through scoped transform/clip state (`RenderStateScope.h`). The CPU backend
in `SoftwareRenderBackend.cpp` implements verified same-format opaque sprite blits and
opaque rectangle fills directly on its borrowed framebuffer. Translucent draws and
mixed pixel formats retain SDL geometry rasterization so platform-specific blending
rounding and source modulation match the reference. General triangles use that same
lazy SDL renderer; its queue flushes before direct writes or target replacement.
Large existing images expanded past 512 pixels retain SDL geometry
rasterization because its fixed-point overflow behavior is visible at some transformed
sizes. Borrowed terrain run views use direct rasterization: they replace small tiles and must not acquire that
large-triangle behavior. Correcting the legacy large-image appearance needs separate
visual acceptance.
`RenderBackend.cpp` contains the accelerated SDL implementation and its texture uploads.

`SurfaceRaster.cpp` owns pixel arithmetic. Unscaled sprites use opaque copies only
when their pixels are verified opaque and draw opacity is 255; other sprites use the
conservative blending path. Native drawing preserves the existing draw-opacity arithmetic. Fully unclipped native
scaling uses SDL's optimized nearest scaler without classifying mutable UI surfaces.
Clipped and transformed blits sample nearest source pixel centers from
the original destination rectangle; clipping cannot change sampling. Transformed
rectangles round both endpoints with `floor(edge + 0.5)` and derive their size afterward,
so adjacent tiles share a boundary at fractional zoom. This can change fractional-scale
sampling and boundary placement by one output pixel. Source blend/alpha modulation is
restored after each operation. Transformed primitives preserve SDL triangle blending
rounding, including independent source/destination truncation for textured draws. Native
rectangle alpha arithmetic retains the legacy `/256` rounding. Sprite modulation uses
exact `/255` arithmetic and zero-alpha sprite pixels leave the destination untouched.

Surface content revisions are independent of texture upload revisions. Each accelerated
backend tracks its own uploaded revision; opacity classification is cached against the
content revision. Code that edits pixels through `getSDLSurface()` must call
`markPixelsChanged()` afterward. This includes raw SDL copies and external rasterizers.

`GameRenderFrame` groups the viewport, assets, visibility and draw options inside the
existing game rendering entry point. Presentation state a view keeps between frames —
animation phases, the cloud field, the overlay scratch buffer and the software terrain
cache — lives in `MapRenderState`, owned by `Game::ViewState`, never on `Game` or `Map`;
the simulation neither reads nor writes it and each view animates independently. The
terrain cache is transient presentation state: 16×16-cell composed pages, a 32 MiB
software storage reservation including pixels, recipes and borrowed views, and a
separate 128 MiB allowance for GPU density selection and resident texture/mip
reservations. The desktop GPU-mode cache has a 256 MiB total ceiling, including
CPU pixels and bookkeeping; Android/browser builds retain the 128 MiB total limit.
On desktop, the additional CPU retention allowance avoids evicting inactive
zoom densities. GPU views retain pixels and textures across sampling
changes, retiring idle textures first when needed. When the visible textures
exceed the allowance but their CPU pages and one upload fit, drawing streams
textures from those retained pixels rather than recomposing the entire view.
Terrain, discovery and material revisions are checked when an old zoom level
returns. Kept coverage masks for mixed cells beside animated materials add at
most 8 MiB in software mode or 32 MiB in GPU mode. Native and
HD rendering share CPU composition; GPU backends upload the resulting pages.
The [terrain authoring guide](../assets/terrain-materials.md) describes the catalog,
boundary resolver, source preparation, budgets and asset pipeline.
Its deterministic world-space displacement continues contours across tiles at
three scales, with shared wrapped control points and a bounded local contour
budget. Prepared tiles hash control points once; pixel sampling interpolates them
at the requested native/HD resolution. This changes coverage only, not terrain
identities, texture selection or simulation randomness.

Within a software page, adjacent opaque tiles become borrowed surface views over
the raw pixels. Fully transparent tiles submit no draw. Partially transparent
coastlines retain individual source blits, avoiding repeated alpha scans over
transparent holes. Views are destroyed before their backing page.
Each page validates the canonical terrain neighborhood, discovery decisions and
revisions of the materials its recipes use. Animation or source changes in unrelated
materials do not invalidate it; a phase change of an animated material (water,
deep water, lava, ember field) recomposes only the pages that use it. Pages store
raw color/alpha. Map replacement (a new `Map::identity()`) clears the cache;
editor terrain changes, wrapped neighbors and visible-team changes are detected
during preparation. Resources, actors, fog and overlays keep their existing
passes. Oversized working sets stream one temporary canonical page at a time
at the same sampling density as the full view. If a page cannot fit the device or
allocation fails, an emergency composed-tile path preserves coverage but can differ
in fractional resampling and HD mip filtering. None of these caches enter saves,
simulation checksums or orders.

Native software fog fills and shade tiles snap shared edges in backing pixels.
Their pixel fill/blit operations retain clipping and flush queued geometry before
direct writes, avoiding fractional zoom and HiDPI seams.

`SoftwareFramePresenter` owns two framebuffers and retains the completed one for exposure
repaint. `beginFrame(FullRedraw)` rotates without a retention copy. Partial updates,
including legacy callers that begin implicitly on their first drawing operation, copy
the completed frame into the next drawing target. Target rotation flushes queued work
and rebinds the software backend. Resize retains the old completed image until the first
replacement frame completes. Letterboxing and minimized-window handling remain in the
window presentation boundary; failed spare-buffer allocation uses the prior frame-cache
copy path. `completedFrame()` provides the retained software image; normal screenshot
requests continue to capture the current drawing frame.

Build the opt-in saved-game benchmark with optimized production objects:

See [software renderer benchmarks](../development/software-rendering-benchmarks.md) for commands and validation.
