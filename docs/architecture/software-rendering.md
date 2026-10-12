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
most 16 MiB in software mode or 32 MiB in GPU mode. Native and
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

## Bounded CPU presentation work

`GLOB2_RENDER_EXECUTION=parallel` enables experimental CPU terrain groups and
software raster tiles on native hosts. `serial` traverses the same CPU kernels
on the graphics owner. The default `auto` retains immediate serial drawing and
terrain preparation, using the shared composition and regional pixel kernels;
it does not enable concurrency or recording until representative release
measurements justify selection thresholds. These are development overrides,
not gameplay settings; set them before launch. The recorder reads execution
mode when its backend is created. `GLOB2_RENDER_WORKERS=N` accepts 1–64 and caps presentation
concurrency at the smaller of the request, two, and the available compute workers.
Scene preparation consumes a slot in the same ceiling. Neither override changes
simulation worker configuration. Browser graphics batches execute serially.

The engine installs a scoped `PresentationWork` dispatcher around drawing and
presentation. Render code sees a synchronous CPU interface, without a `Map` or
simulation reference. `ComputeExecutor` admits one graphics batch independently
of the existing resumable scene mailbox. Required jobs retain due/lane order.
After eight required claims, the designated presentation worker alternates
eligible scene chunks and graphics jobs. Other workers claim graphics jobs only
when no required job is claimable. Workers never join, call the backend, or wait
for the graphics owner. Zero-job tickets complete immediately; cancellation
stops new claims and drains callbacks. Finish, abandoned-ticket destruction,
reconfiguration and shutdown release captures before publishing final status.
Finished tickets retain failure information, but no job inputs.

CPU terrain preparation separates owner planning, private cell composition and
canonical installation. Published materials contain only immutable composition
pixels for the page's referenced materials and catalog definitions. Unchanged material revisions share storage;
the compositor keeps bounded weak records per material revision, allowing reuse
while any snapshot still owns unchanged pixels. Each cache charges its retained
lease against subsequent admission. Current-page masks remain protected until
the synchronous group finishes. Workers reuse bounded scratch and never create
`HeldMask` or drawable objects. Jobs sample at most 16K pixels; a group admits
up to 32 changed cells under a 4 MiB additional reservation and available cache
headroom. Queued raster sources and active terrain work share an additional
8 MiB owner reservation ceiling; raster bookkeeping reserves 2 MiB within it for the backend lifetime,
including retained vector capacity. Captured source reservations release when
the segment finishes or is discarded.
The reservation includes material revisions, catalog/control storage,
results, masks, metadata and two scratch slots. Oversized HD cells, reductions,
and groups without headroom use existing serial composition. GPU composition and emergency fallback retain their existing paths. Streamed
CPU pages reuse the bounded page builder while keeping one temporary page alive. Installation
keeps the page invalid until its pixels, masks, revisions and opaque runs are
consistent. Allocation failure discards the invalid cache rather than publishing
partial output.

The software recorder captures supported opaque nearest blits, backing-pixel
triangle-style blits and rectangle fills. Unsupported geometry, linear sampling,
color keys, mixed formats and legacy large-image cases flush the preceding
segment before owner execution. Arbitrary borrowed source pixels are copied at
capture time, including terrain views. Overlapping byte ranges between distinct
source/target views flush earlier drawing before the source snapshot. Source
mutation or destruction cannot change a captured command. Each segment is
bounded by 1,024 commands and 4 MiB of copied sources; bin bookkeeping is bounded
by 4,096 tiles and 65,536 references. Excessive tile work uses serial command traversal in regions at most
128 pixels wide, bounding sampling scratch independently of target width. Current candidates are 128-pixel tiles and at most sixteen command
references per tile; these are conservative experimental bounds, not measured
performance thresholds.

The owner drains earlier SDL geometry, locks the target once, and captures its
format/pitch before dispatch. Jobs write disjoint half-open regions in painter
order and sample from the original complete destination. They never touch SDL
surface state. `GraphicContext::getSDLSurface()` fences active CPU drawing before
returning pixels; accelerated contexts retain their existing CPU facade and need
explicit capture for readback. Callers holding a base-class alias or an earlier
raw target pointer must flush the owner before reading or modifying it.
The owner unlocks after all jobs return, before fallback drawing,
capture, target rotation or presentation. Failed work blocks capture and commit.
`GraphicContext::discardFrame()` discards recorded drawing and restores the
presenter's previous completed image, permitting a subsequent full or partial
redraw. A capture of an offscreen target still fails with its drawing operation;
the caller owns that target's disposal.
