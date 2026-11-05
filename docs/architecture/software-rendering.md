# Software rendering

CPU rasterization, retained terrain and backend ownership. See [rendering benchmarks](../development/rendering-benchmarks.md) for timing evidence.

## Software rendering architecture and profiling

`GraphicContext` remains the drawing facade and retains existing capability queries.
It owns the accelerated backend and software backend independently; transformed passes
borrow them through scoped transform/clip state (`RenderStateScope.h`). The CPU backend
in `SoftwareRenderBackend.cpp` resolves ordinary sprite draws and rectangle fills
into immutable raster commands. Opaque copies, transparent and draw-opacity-modulated
blits, mixed packed pixel formats, nearest scaling and premultiplied linear sampling
share the same kernels whether executed immediately or through a recorded segment.
Uniform axis-aligned quad pairs in triangle batches enter that same command stream,
including their vertex tint and opacity; arbitrary triangles keep SDL's rasterizer.
Color-keyed sources, non-sRGB textured inputs and unsupported formats retain a lazy SDL
geometry fallback; its queue flushes before direct writes or target replacement.
Large existing images expanded past 512 pixels also retain SDL geometry because its
fixed-point overflow behavior is visible at some transformed sizes. Borrowed terrain
run views use direct rasterization: they replace small tiles and must not acquire that
large-triangle behavior. Correcting the legacy large-image appearance needs separate
visual acceptance. `RenderBackend.cpp` contains the accelerated SDL implementation
and its texture uploads.

`SurfaceRaster.cpp` owns pixel arithmetic. The software backend uses portable integer
source-over arithmetic for nearest sprite blending and translucent fills, including
zero-alpha no-ops and destination alpha. This deliberately replaces SDL's varying
channel-rounding rules; it does not change coverage, sampling or filtering semantics.
Exact opaque native-size copies preserve their stored pixels. Sprite draw opacity is
captured in the command; geometry draws continue to ignore mutable SDL surface tint
and alpha modulation, matching their existing vertex-modulation semantics.
The owner resolves coverage and sampling according to the historical drawing entry
point before recording. Opaque direct draws use shared rounded endpoints and rational
nearest pixel centers. Draws formerly handled as SDL uniform quads retain truncated
integer origins/extents and the truncated 16.16 nearest step, including their source
rectangle conversion. Translucent geometry fills retain the same quad coverage rule.
These distinctions preserve existing geometry rather than choosing a different render
pipeline. Every worker samples from the original complete destination, so clipping and
tile boundaries cannot change source coordinates. Unsupported out-of-bounds textured
quads remain ordered owner fallbacks. Linear sampling retains fractional pixel-center
geometry and filters in premultiplied space, preventing invisible RGB from bleeding
into edges. Scalar and SSE2 nearest-blending kernels use identical integer arithmetic;
other platforms retain the portable scalar implementation. Native bitmap/fog helpers
outside this command backend retain their existing arithmetic and SDL state-restoration
contracts.

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

See [software renderer benchmarks](../development/software-rendering-benchmarks.md) for commands and validation.

## Bounded CPU presentation work

`GLOB2_RENDER_EXECUTION=parallel` enables experimental CPU terrain groups and software
raster tiles on native hosts. `serial` traverses the same CPU kernels on the graphics
owner. The default `immediate` uses direct serial drawing and terrain preparation,
without command recording or packed terrain jobs. These are two execution paths:
immediate drawing, and batching with either serial or parallel job execution. Both
share composition and regional pixel kernels, so shared kernel improvements also
benefit immediate drawing. There is no automatic selection policy. The former `auto`
spelling remains an alias for `immediate` for existing launch scripts. CPU execution
selection is independent of renderer backend selection (`--renderer`).
These are development overrides, not gameplay settings; set them before launch. The
recorder reads execution mode when its backend is created. `GLOB2_RENDER_WORKERS=N`
accepts 1–64 and caps admitted scene/graphics concurrency at the smaller of the
request and two.
Native worker availability can further limit achieved concurrency. Scene preparation
consumes a slot in the same ceiling. Neither override changes simulation worker
configuration. Browser graphics callbacks run serially on the owner within the same
ceiling and use the same admission and ticket lifecycle as native batches.

The engine installs a scoped `PresentationWork` dispatcher around drawing and
presentation. Render code sees a synchronous CPU interface, without a `Map` or
simulation reference. `ComputeExecutor` admits one graphics batch independently of the
existing resumable scene mailbox. Required jobs retain due/lane order. After eight
required claims, the designated presentation worker alternates eligible scene chunks and
graphics jobs. Other workers claim graphics jobs only when no required job is claimable.
Workers never join, call the backend, or wait for the graphics owner. Zero-job tickets
complete immediately; cancellation stops new claims and drains callbacks. Finish,
abandoned-ticket destruction, reconfiguration and shutdown release captures before
publishing final status. Finished tickets retain failure information, but no job inputs.
The owner obeys the same scene/graphics concurrency ceiling as workers. Completion
publishes under the scheduler mutex and wakes admission waiters, including when a
cancelled owner batch finishes while an unrelated scene chunk still occupies the
ceiling. Captured inputs are destroyed outside both scheduler and batch locks.

CPU terrain preparation separates owner planning, disjoint cell composition and canonical
metadata installation. Published materials contain only immutable composition pixels for the
changed cells' referenced materials and catalog definitions. Planning finds changed
cells before acquiring material snapshots, so a sparse edit does not copy unchanged
materials. Unchanged material revisions share storage; the compositor keeps bounded weak
records per material revision, allowing reuse while any snapshot still owns unchanged
pixels. Each cache charges its retained lease against subsequent admission. Current-page
masks remain protected until the synchronous group finishes. Hidden cells release
obsolete discovery masks during canonical installation. The owner locks the unpublished
cache page once and supplies disjoint cell pointers and its pitch to workers. Workers
write complete cells directly, including clearing hidden cells; no intermediate pixel
slab or installation copy is needed. The page remains invalid until the join and
metadata installation finish. Failed groups cannot publish partially written pages;
subsequent preparation rebuilds every cell.
Opacity classification selects an opaque or empty predicate from the first pixel and
scans once; cells starting with partial alpha need neither uniform scan. Workers reuse
bounded scratch and never create `HeldMask` or drawable objects. Jobs sample at most 16K
pixels; a group admits up to 64 changed cells under a 4 MiB additional reservation and
available cache headroom. Queued raster sources and active terrain work share an
additional 8 MiB owner reservation ceiling; raster bookkeeping reserves 2 MiB within it
for the backend lifetime, including retained vector capacity. Source allocations are
charged once even when both the source cache and a segment retain them. Uncached
captures release on segment completion; cached revisions release on eviction, forget,
reset or discard. The reservation includes
material revisions, catalog/control storage, results, masks, metadata and two scratch
slots. Oversized HD cells, reductions, and groups without headroom use existing serial
composition. GPU composition and emergency fallback retain their existing paths.
Streamed CPU pages reuse the bounded page builder while keeping one temporary page
alive. Installation keeps the page invalid until its pixels, masks, revisions and opaque
runs are consistent. Allocation failure discards the invalid cache rather than
publishing partial output.

The software recorder captures ordinary nearest and premultiplied linear blits,
backing-pixel sprite draws and opaque/translucent rectangle fills. Unsupported geometry,
color keys and legacy large-image cases flush the preceding segment before owner
execution. Owner execution uses the same raster commands and arithmetic for supported
operations; the execution override changes scheduling rather than their appearance.
Triangle batches preserve SDL's greedy quad pairing even when a recognized pair needs
fallback. Logical blits that previously used geometry retain its null-key untextured
contract; direct backing-pixel blits still accept anonymous pixel sources.
Keyed sources follow the existing content-revision and forget contract: raw writes
require `markPixelsChanged()`, and destruction forgets the source key.
The first eligible recorded draw copies the requested source region into immutable
ARGB pixels, converting supported packed formats on the owner. Subsequent draws of
the same revision reuse that snapshot when it covers their requested crop. Opacity
and sampling remain command state rather than being baked into cached pixels.
A different crop takes a new regional snapshot; queued commands keep previous
snapshots alive across replacement or eviction. This also supports keyed terrain run
views, which are forgotten before their backing page changes. Regions up to the
1 MiB warm-cache limit can reuse revisions regardless of the complete image size.
Larger regions and anonymous borrowed sources take fresh regional copies per draw.
No capture copies unrequested offscreen pixels. Overlapping backing ranges, including
negative pitches, flush earlier drawing before a fresh snapshot and never use the
revision cache. Source mutation or destruction cannot change a captured command.
Supported fully clipped or zero-opacity commands are discarded before recording or
allocating source storage. Synchronous owner execution can borrow supported packed
source pixels directly; when snapshot admission fails, nonaliased packed inputs also
retain that same-kernel owner path.

Owned source arrays are allocated without initialization and completely filled before
publication. Each segment is bounded by 1,024 commands. Cached and in-flight snapshots
share a 4 MiB allocation bound within the shared 8 MiB reservation. Presentation trims
the warm source cache to 1 MiB so retained pixels do not crowd out next-frame terrain
work. Cache entries are bounded by the command limit; bin bookkeeping is bounded by
4,096 regions and 65,536 references. Tile, reference and job vectors retain bounded
capacity between segments. Segment cleanup destroys commands before releasing source
leases.

Parallel drawing initially partitions output into 128-pixel tiles. Dense regions split
along their longest dimension until their summed clipped work fits 262,144 sampled
pixels; linear filtering charges four samples per output pixel. Each region retains
original painter order. Sparse neighboring regions share jobs while respecting the
same sampled-work ceiling and a sixteen-reference packing target. Dense regions can
carry more references when their smaller area bounds the actual work. Metadata overflow
completes ordered command prefixes rather than serializing an entire dense frame.
A single-command overflow uses serial traversal in regions at most 128 pixels wide,
bounding sampling scratch independently of target width. These are conservative work
and storage bounds, not measured performance thresholds. Terrain preparation and raster
jobs use the existing shared compute executor; no operation-specific pools or handoff
queues are introduced.

The owner drains earlier SDL geometry, locks the target once, and captures its
format/pitch before dispatch. Jobs write disjoint half-open regions in painter order and
sample from the original complete destination. They never touch SDL surface state.
Verified opaque native-width copies with positive strides use row copies only when the complete source and
target backing ranges are disjoint; aliased views retain ordered per-pixel execution.
`GraphicContext::getSDLSurface()` fences active CPU drawing before returning pixels;
accelerated contexts retain their existing CPU facade and need explicit capture for
readback. Callers holding a base-class alias or an earlier raw target pointer must flush
the owner before reading or modifying it. The owner unlocks after all jobs return,
before fallback drawing, capture, target rotation or presentation. Cancellation drains
claimed callbacks and raises an error for synchronous drawing. Any drawing exception,
including owner capture or binning failure after a completed prefix, abandons the frame
and blocks further drawing, capture and commit.
`GraphicContext::discardFrame()` discards recorded drawing and restores the
presenter's previous completed image, permitting a subsequent full or partial
redraw. A capture of an offscreen target still fails with its drawing operation;
the caller owns that target's disposal.
