# Rendering and presentation

The renderer consumes immutable scenes; presentation state and pacing stay outside authoritative simulation state.

## Batching and geometry cache invariants

The native desktop OpenGL renderer batches ground and air passes with
`UnitDrawBatch`. It keeps the original sprite, fill and line primitives and their
painter order. A draw may join an earlier run only if its conservative bounds do
not intersect any intervening run. Bounds include a physical-pixel sampling
margin and half the requested outline width; they must be expressed in the
current map transform. Capacity limits flush a batch rather than grow it without
bound. Unsupported commands submit pending draws first. Clip and transform
changes also flush, then disable culling and reordering for the remainder of the
scope because the original bounds no longer describe its coordinates.

Team hue and opacity travel with each vertex. Compatible HD textures can share
array pages while retaining their original dimensions, mip levels, format and
sampling parameters. The source textures remain available for fallback drawing.
The array cache caps its additional texture payload at 64 MiB, separately from
cached map geometry. Immutable slots survive source texture invalidation until
context teardown; new textures use the ordinary path when that budget is full.
Pages contain at most 64 layers to bound each driver allocation. `FrameDrawBatch`
defers new array copies until after scene submission, attempting at most eight
sources under a soft 2 ms budget. Original textures draw while preparation is
pending, and mutation/deletion cancels pending IDs. A texture
upload or deletion must flush commands referring to the old pixels and invalidate
array views before the driver can reuse a texture name. Cached geometry must also
be invalidated selectively when its source texture or array page changes;
unrelated uploads must preserve reusable entries. A mutation serial rejects
interrupted captures but does not globally clear the cache. These are
presentation caches owned by the graphics context, released while that context
is current; they are neither saved nor consulted by simulation code.

Cached map rows record each frame's HD or native layer as it stands when they
are captured. Streamed HD frames (decor only above 32 pixels a cell) arrive
later without changing any row's frames or touching a native texture, so each
resource, decor and overlay row also passes the newest
`Sprite::highResolutionRevision(frame)` among its frames as the entry's artwork
revision. An entry whose frames match but whose artwork is older stays
drawable: it is re-recorded within half of the frame's build budget, and until
then, or while a capture is rejected for uploading a new HD texture, it replays
at the previous resolution rather than falling back to a direct draw. A capture
that fails for any other reason drops the old entry, so the row draws the new
artwork directly instead of keeping the old for good. Deleting an HD layer needs
no revision: a standalone layer's texture goes with it, which invalidates the
entries that recorded it, and an atlas page stays alive until the atlas is
replaced, which invalidates them then.

High-resolution sprite layers are shared by gameplay, editors and background
scenes. Changing screens preserves published layers and pending reload work.
Preference changes, a new asset source generation or a different pack directory
start a reload; unchanged requests do not. Explicit synchronous reloads used by
tools still refresh unchanged artwork. Disabling the preference releases the
optional layers through asset polling, rather than through screen destruction.
Every renderer chooses between HD and native sprite frames by the
[HD sprite policy](#hd-sprite-policy) below. Legacy OpenGL loads every
sprite's HD layers with the sprite; every other renderer loads them on demand.
There a drawn sprite loads only the 32-frame blocks its draws reach (a unit's
action), batched into one reload a frame, and demands its team layers with its
base ones; back on native frames (ten seconds below the threshold) it releases
the layers that drawing demanded. Once a frame's HD base layer is uploaded its
CPU pixels go (into the portable renderer's texture or the device's retained
snapshot), unless something samples them through `Sprite::baseFrame` (a
released frame then answers with its native frame and reloads with pixels;
`Sprite::drawFrame` serves draw-only callers such as skin shadows), and a
renderer reset, a lost device or rebuilding the portable renderer reloads frames
that had only textures (a capture's offscreen pass draws such a frame's native
layer). A unit draws HD only once its whole action is; at most eight frames a
frame draw in HD for the first time (each one uploads), and a device renderer
admits new HD frames only while its retained General textures stay under three
eighths of that class's budget, so a full budget leaves frames native. HD layers
sample linearly with premultiplied alpha. Device renderers with the team-colour
material sample the HD team layer directly; elsewhere recoloured HD team layers,
buildings' included, share each sprite's 64 MiB recolour cache; once drawn, a
cached recolour keeps only its texture, and one whose texture a reset took is
recoloured again from the HD team layer, which keeps its pixels for the other
colours. A sprite that only `Sprite::baseFrame` reaches (terrain composition,
skin shadows and editor swatches) loads only its uncolored base layers, once
`baseFrame` or an HD window first asks for them. The first answers can be native
frames, as with any streamed layer. Switching to legacy OpenGL reloads every
sprite; switching away releases layers nothing asked for.

### HD sprite policy

HD artwork is drawn at the zooms where it shows, and native artwork otherwise
and as the fallback. One policy, `GAGCore::HighResolutionPolicy`
(`libs/gag/include/HighResolutionPolicy.h`), held by `Sprite` and fed once a
frame by the map view (`Sprite::setHighResolutionDensity`), decides it for
every renderer; no renderer keeps a threshold of its own.

- **Density** is the screen pixels one logical pixel covers: map scale times the
  display's raster scale (the display's scale alone for interface drawing, and
  before any map view). HD frames hold four texels a logical pixel.
- **Above a density of one** sprite frames draw from their HD layers: up to two,
  at half resolution (two texels a logical pixel), beyond two at full
  resolution. **At or below one** native frames draw: HD would only be shrunk
  back to them. Terrain pages take HD materials at the same threshold
  (`HighResolutionPolicy::wantsHighResolution`).
- **Hysteresis**: a level with more detail is taken at once; one with less only
  after the view has wanted it for ten seconds (`DetailHysteresis`), so zooming
  across a threshold and back reloads nothing.
- **Fallback is always native**: where the policy wants HD but a frame's HD
  layers are not all there (still streaming, evicted, failed, missing from the
  pack, or past a device's budget), the native frame draws, never nothing and
  never a wait. A frame draws HD only once every layer its native frame has
  (base and team colour) is HD (`HighResolutionPolicy::frameDrawsHighResolution`),
  and a unit only once its whole action is. The HD setting off draws native
  everywhere.
- **Room on devices**: the on-demand device renderers upload at most eight new
  HD frames a frame and keep each one as the only copy of its pixels, within
  3/8 of the General budget class (`GraphicContext::deviceHighResolutionRoom`;
  about 96 MiB on desktop, 18 MiB on phones). When a new frame finds no room it
  draws native, and the next frame evicts the sprites' 32-frame blocks drawn
  longest ago, among those idle for at least 120 frames, until there is room
  (`Sprite::evictIdleHighResolution`). A block's base and team layers go
  together, and drawing it again reloads it. Units and buildings share this
  room, so frames that left the view give way to the ones in it. Terrain banks
  retire their frames through their own windows instead.

Every renderer samples the one level the policy names, so all settle to the
same pixels (`DrawableSurface::singleLevelSampling`):

| Renderer | Half resolution (1 to 2) | Full resolution (above 2) |
| --- | --- | --- |
| Legacy OpenGL | detail band 1: the chain's second level alone, sampled linearly | band 0: the first level alone |
| SDL GPU, programmable OpenGL, portable SDL | the frame's half-resolution level, sampled linearly | the full frame, sampled linearly |

The software renderer draws native frames (and native terrain pages) at every
zoom: it has no filtering, and it is exempt from renderer parity.

Filtering is in premultiplied colour everywhere. The devices and the portable
renderer premultiply their textures; legacy OpenGL keeps straight alpha, so
its texture-array program filters a page holding one linear level itself,
premultiplying its four texels, and un-premultiplies the result for its
blending. Hardware filtering of straight alpha would blend the colour of
transparent texels (black in most artwork) into every sprite edge. The
resource sheet draws from standalone frames on legacy OpenGL too, not from a
trilinearly sampled HD atlas. Native frames are sampled nearest; at half zoom
every pixel centre falls on a texel boundary, and legacy OpenGL biases those
ties to the lower texel, as the other renderers resolve them. The portable
renderer biases the texture coordinates of its nearest-sampled quads the same
way (`GraphicContext::drawSurface`): left to the driver, it took the upper
texel in some frames (the resource sheet's algae, some trees) where the devices
take the lower. Team-coloured building frames stay unbiased there
(`nearestTiesUnbiased`), since the driver already resolves their ties as the
devices do, at half zoom and at others such as 0.9. Lines, circles
and rectangle outlines are one logical unit wide on every accelerated
renderer: a quad along the line, circles of such segments, and outlines of
the rectangle's own edge pixels.

Terrain takes its half-resolution level the same way everywhere: an HD
material composed or streamed below its own size is box-filtered in
premultiplied colour, one output texel the average of the block it covers.
The streamed direct pass filters its art tiles so (`DirectTerrain`), and the
page compositor its sources (`Compositor::boxFilter`, mirrored by the
`terrain-compose` shader), rather than sampling every second texel.

Legacy OpenGL's bands load the HD frames with the levels a view needs left out
(band 2, a quarter of the frame, while the policy is native), reloading in the
background on a band change while the current textures draw. The on-demand
renderers take the half-resolution level from the CPU mip chain the asset
workers build with the frame (`DrawableSurface::fromReducedLevel`); a change between half and full resolution
reloads the blocks drawing reached, and the current frames draw until the new
ones are published. Sprites a caller samples on the CPU keep their full frames.
One level sampled linearly shrinks a frame at most twice, which a mip chain
would only blur further.

`tools/rendering/renderer_parity.py` holds scenes on either side of the
threshold (`gui-below-1x`, `gui-above-1x`) besides the zoomed-out, default and
zoomed-in ones, and `SpriteSheetTest.cpp` checks the policy and that each
available renderer draws native at density one and HD at two.

Below one screen pixel per logical pixel the portable renderer's native pages
are reduced to the density the direct pass's art tiles take there: the smallest
power of two at least 32 times the density pixels a cell (16 at half zoom, 8 at
a quarter), and sampled linearly. A uniform cell is box-filtered from its native
composition. A mixed cell composes at that density as the direct pass draws its
masks there: each pixel blends the alpha-weighted mean of its block of samples,
then takes the contact shade, fringe and surf of the block's centre sample
(`Compositor::compose` with a reduction). Box-filtering the composed cell
instead averaged the one-sample dark contact line and pale surf band with their
surroundings into a wider, paler shore. The render devices' page fallback keeps
the box filter, which their own page composition (`terrain-reduce`) mirrors. An unreduced native page is sampled
nearest there, which drew grainy, high-contrast sand where the other renderers
draw filtered terrain.

The portable SDL renderer's terrain pages ask for HD material frames only where
they show: above one screen pixel per logical pixel (map scale times raster
scale, 32 pixels a cell on a 1x display), the threshold legacy OpenGL's direct
terrain uses. Native frames are the HD frames reduced by four, so at or below it
an HD page holds four times the texels, composed and uploaded for nothing. A
view starts on native frames and takes HD at once when it zooms in, but drops back
only after it has wanted native frames for ten seconds (`GAGCore::DetailHysteresis`
on the clock of the legacy OpenGL sprite bands), so zooming across the threshold
and back streams nothing again; pages meanwhile compose the HD frames at the
display density (below). Then pages compose from native frames, the HD pages
are discarded, the streamed banks' frames retire into the shared budget for
[retired frames](../development/rendering-benchmarks.md#direct-terrain) and whole sheets a page demanded through `Sprite::baseFrame`
release their layers until the next demand. Decor HD frames stream there only
while the [HD sprite policy](#hd-sprite-policy) shows HD; below that it draws
decor from native frames and retires them.

Whatever frames they use, the portable renderer's pages compose at the
density the direct pass draws its art tiles and coverage masks at: the
smallest power of two at least the display density, up to four times native.
HD frames compose below their size box-filtered, as the direct pass filters
its art tiles, so at zoom 1.5 a page holds 64 pixels a cell rather than HD's
128, which drawn at 48 without mips aliased where the other renderers draw
filtered tiles. With HD artwork off at zoom 3 a page composes four times
native density from native frames, sampled nearest as the direct pass samples
them, so shorelines and borders are as smooth as on the other renderers
rather than native coverage magnified into steps.
A view's decor window holds only the frames its cells draw (each 16 by 16
cell page's decor frames are kept until the terrain changes, and the view widens
by the decor's reach), not every frame of every material in view. Drawn decor
HD frames free their CPU pixels like other drawn HD frames (the
material banks, which composition samples, keep theirs); retired frames count
only the pixels they still hold, and when a reset takes the textures, the bank
keeps the frames that kept their pixels and declares its window again so the
lost ones decode once more. Captures keep their own density and HD artwork.

The game's compositors load the native sheets of the installed terrain
materials on demand (`TerrainVisual::MaterialSheets`): startup decodes none of
them. A map's materials are found by a scan of its terrain a few rows a frame
(again, keeping those found, after an edit), and their sheets load on the asset
workers through `Toolkit::requestSprite` and publish within the frame's asset
budget; composition, the editor's palette and phase pixels also ask for a sheet
the moment they need one; sheets of materials in view load first, and the rest
of the map's at most two at a time behind them. A page whose cells need a sheet
still loading stays uncomposed (`Compositor::sheetsReady`), so the fallback
chain draws there (another density, the overview, the whole-map base) and the
page fades in once composed, like any other; a page already composed keeps its
pixels. Only the editor palette's icon shows a material's preview colour while
its sheet loads.
Direct terrain builds its tables only once the map's sheets are in (pages draw
meanwhile) and builds them again if an edit baked a preview colour for a sheet
that has since arrived. Complete draws wait explicitly, off the interactive
frame: tiled captures, `MapRender` exports, editor swatches
(`Compositor::prepareForRecipes`), the whole-catalogue `Compositor::prepare` and a
complete direct terrain draw call `Compositor::finishMaterials`, as do a game
while it loads (`GameGUI::loadTerrainArtwork`) and the menu colony while the
loading screen is still up (`MenuColony::load`), so neither first paints with
its sheets still loading. Sheets no map
retains go after the retired HD frames' lifetime, sooner when they and those
frames exceed that shared byte budget. Map asset bundles' own sheets and
compositors built outside the game (tools, tests) still load every sheet at
construction; `GLOB2_TERRAIN_SHEETS=eager` restores that in the game. The shared
decor and overlay sheets still load at startup.

Terrain uses the shared CPU material compositor and bounded 16 by 16 cell pages
on software and GPU backends; see [terrain materials](../assets/terrain-materials.md).
[Direct terrain](../development/rendering-benchmarks.md#direct-terrain)
uses map-space atlases for Original artwork on desktop legacy OpenGL, programmable
OpenGL, SDL GPU, and WebGPU; desktop legacy OpenGL, SDL GPU and programmable OpenGL also draw HD artwork with it. SDL GPU and WebGPU use offline shader assets. The atlas builder packs terrain data independently of the device. The
camera transforms a terrain quad while its fragment shader blends material
variants, coverage and shoreline effects.

SDL GPU and programmable OpenGL draw legacy OpenGL's streamed pass rather than the whole-map native
atlas: the same per-cell tables, artwork streamed at the display density, coverage
masks built by background workers at that density, the retired atlas a zoom
keeps, the whole-map base masks and the corner split, through the
`terrain-direct-hd` shader. Its textures are `StreamedTexture`s
(`libs/gag/include/RenderFrame.h`): device textures written in place with no CPU
copy of their pixels. Writes queue on the texture until a submitted frame applies
them, all before that frame draws; a dropped frame leaves them queued.
Programmable OpenGL applies them with `glTexSubImage2D` as it submits the frame
that draws them, clears a new texture through a framebuffer rather than a CPU
copy, charges the textures to its total budget and never evicts them; it loads
the generated `terrain-direct-hd` GLSL (desktop 3.30 or ES 3.00) and needs 14
texture units. A device
that advertises `directTerrainStreaming` takes this pass at every zoom, with HD
artwork on or off. Legacy OpenGL and the devices share one atlas policy, so
they coarsen alike. The mask atlas stays at 2048 square. The art atlas starts
at 2048 and grows once to 4096 when the view's variants do not fit: at 128
pixels a cell (zoom 2 on a 2x display) a 2048 atlas holds only 256 tiles, fewer
than a typical view's variants. It grows only under the desktop budget, with
4096 textures, and while the renderer's resident bytes leave room in the
platform's `RenderBudget` for twice the new atlas and an eighth of the budget
besides (the old atlas draws while the new one fills); otherwise the view takes
coarser tiles. A game on a 2x display holds 200 to 230 MiB, so the atlas needs a
total of about 400 MiB to grow: under the former 256 MiB desktop total every
renderer drew zoomed-in Retina views from 32-pixel art instead of 64. A device counts its resident resources; legacy OpenGL, which has
no device budget, counts its direct terrain and its other textures up to the
General class a device would keep them in. `DIRECT_TERRAIN_ADMISSION` on stderr records each change of a
view's art atlas, tile density and the reason it is coarser than the display
wants (`device budget`, `atlas size`, `atlas full`), for legacy OpenGL and the
devices alike. An
animated variant holds a pair of art tiles (its current and next phase) and a
static one a single tile, so at 64 pixels a cell (the default zoom on a 2x
display) the 2048 atlas holds about a thousand static variants. Art tiles are box-filtered on the background workers
(`BackgroundWork`), from copies of their sources, up to 192 a batch and one
batch at a time; the graphics thread only copies the sources and uploads the
finished tiles. An interactive view whose artwork density or atlas size
changes fills a second art atlas that way while the old one keeps drawing (its
water stops animating meanwhile), and switches to it with its own variant
table once every visible variant is in it. Animation steps likewise draw their
previous phase until the workers deliver the next. A variant new to the drawing
atlas is filtered at once, with its record, and a complete draw refilters
everything at once.

A variant record names a tile only while the drawing atlas holds that
variant's art in every tile it draws. Every other record holds tile 65535, and
the shader draws that variant from the resident low-resolution art: every
palette variant's first native frame at 16 pixels a cell, built at game load
(`DIRECT_TERRAIN_LOW_ART`, about 7 MB for a 256 by 256 map) and extended when an
edit adds variants. The streamed pass binds it in the native mask index's slot,
which that pass does not use. So while a fast zoom fills the second atlas, the
variants the old atlas never held (most of them, zooming out) draw softer art,
never another variant's tile or an empty one. Uploads build the table from the
atlas's entries, an eviction marks its record absent, and a visible variant the
atlas holds without filled tiles gets them at once. `DirectTerrain::artMetrics`
counts the visible variants drawing the low-resolution art (`absent`) and any
record pointing at a tile its atlas does not hold for it (`stale`, always zero).

The streamed devices always have a low-resolution version of the whole map to draw: only HD
masks and artwork stream in and out. Game loading (`GameGUI::prepareDirectTerrain`,
from `Engine::finalAdjustments`) builds the map's per-cell tables and then the
whole-map base masks, the masks on up to eight threads (`DIRECT_TERRAIN_PRELOAD`
and `DIRECT_TERRAIN_BASE` report the time), so the first frame finds them and no
frame draws a mixed cell as a corner split for want of a base mask. Nothing in
the render loop blocks on them: a view without that load, or one whose device
lost its streamed textures, builds the tables in 3 ms slices and the base on the
mask workers (ahead of the view's masks, with 2 ms of recipes a frame), and
draws the overview, with its always-resident whole-map base beneath, until every
base mask is back. A complete draw (a capture, the editor) at the base's density
or below builds the base first and draws from it alone; one finer than the base,
such as a torus capture's own view, builds its visible masks at its density and
not a whole-map base it would never sample, unless its cells overflow the mask
atlas. Workers take base jobs (cells an edit invalidated) ahead of
the view's masks. View masks queue 64 per worker, nearest the view centre first,
and upload within a few milliseconds a frame (at most 16 MiB) rather than a fixed
count. When the device loses a streamed texture (a reset, or staging that stays
over its budget for 30 frames; a texture already drawing defers the frame until
then) or its writes stop draining, the view starts its streamed textures again
(`DIRECT_TERRAIN_RESTART`) and never falls back to pages; only a map whose tables
exceed the device's texture size does. `GLOB2_BENCH_DEVICE_RESET_AT` resets the
device at one benchmark frame, and `fallback_scenario.py --reset-at` reports the
restarts and the cells drawn as corner splits. Discovery updates
revisit only the 16x16 sectors whose discovery stamps moved
(`SceneMap::discoveryStamps`) and rewrite only the changed rows of the
discovery table.

The page renderer handles HD artwork on
WebGPU, backends without direct terrain or the streamed pass, tiled captures outside legacy
OpenGL and the streamed devices, streamed-device maps too large for its direct terrain tables, and the browser's WebGL2 renderer (legacy OpenGL through Emscripten's WebGL2
bridge), where pages draw native artwork until the view's HD families arrive.
Interactive page and overview views never compose on the graphics thread and
never block on it: until wanted terrain arrives they draw the best real terrain
they have, in painter order beneath it: the same page at another density, then
the overview limited to the missing pages, then the overview's whole-map base,
which a worker composes as the map loads, so no transparent or flat patch shows.
On a device renderer a frame whose pages cannot prepare at all (the device
refused page composition) draws the overview too, never uncached pages composed
on the graphics thread.
Pages and overview pages that arrive over a fallback fade in over 150 ms with
presentation-only alpha on their ordinary draw. On a presentation device at most
eight new overview page textures upload a frame (`DrawableSurface::takeDeviceUpload`):
past that a page draws its previous upload over the base, or the base alone. An
overview page covers 64 by 64 cells (16 textures for a 256 by 256 map), and the
pages prewarmed at the farthest zoom's density are admitted to the device adapter
ahead of the zoom out with whatever allowance the frame leaves
(`OverviewTerrainCache::uploadAhead`, through `DrawableSurface::prepareDeviceTexture`,
which keeps the adapter's texture snapshot; the GPU copy happens in the first frame
that draws the page), each content revision once, so a fast
zoom out on SDL GPU or Programmable GL draws every overview page, its contours
and resource squares, on its first frame, as Legacy GL and the portable renderer
already did; a page the device evicted since uploads again within a frame or two.
Likewise a changed HD surface (a terrain page animation rewrote) builds its mip
chain on the graphics thread within 8 MiB of level-zero pixels a frame; past
that a surface the device adapter already holds draws its previous upload. Pages and overview cells ahead of
the camera compose on the workers' lower priority lane. Each frame's
`TerrainFallbackMetrics` (in `MapRenderState`) counts the visible cells drawn
from each fallback; `tools/rendering/fallback_scenario.py` reports them for a
scripted game of jumps, pans and zooms, see
[terrain rendering](../assets/terrain-rendering.md).
Resources, decor and ground overlays draw from canonical map rows of recorded
frames (`ResourceRowCache`) on every renderer: Legacy GL replays each row's cached
geometry, and renderers without that cache draw the recorded frames sprite by
sprite, checking partial discovery per drawn cell, so a frame does no per-cell
decor, overlay or resource lookups. On Legacy GL, fully revealed resources use canonical map rows, with sorted source-tile indices
selecting the contiguous visible vertex range. Translation places a canonical row
at its wrapped-map position, so camera panning does not change its vertices.
Cached rows reuse frame data until their resource or terrain revisions change. Animated cells update their selected frames as the scene tick advances.
Partial-discovery resources keep the ordinary drawing path there. The resource geometry budget
is 32 MiB of buffer payload with at most 4096 entries and least-recently-used
eviction; CPU metadata and driver allocation overhead are additional. Each scene
attempts at most 16 geometry builds under a separate soft 2 ms budget; validated
cache hits remain unrestricted. Deferred rows retain ordinary sprite batching.
These time limits are soft because an individual driver call can exceed them.

Native sprite atlases keep a one-texel extruded border around every frame,
including equal-size terrain tiles. Fractional zoom and camera offsets can put
a covered pixel arbitrarily close to a frame edge; the nearest-sampling tie
bias must land in that frame's border rather than a neighboring frame. Keep
the border copy unblended so transparent edges retain their original RGBA.
On the device renderers a draw checks only its own frame's stamp against the
sheet's atlas, and a sheet that cannot have one (team-coloured or rotated
frames, frames without pixels, too large, no atlas memory) tries again after
120 frames rather than on every draw.

The portable SDL renderer merges its blits, fills and triangle lists into
`SDL_RenderGeometry` runs keyed by texture, blend mode, scale mode and SDL's
per-call clamp or wrap choice. A draw may join an earlier compatible run only
when no later pending run covers any cell of a coarse 16-unit occupancy grid
under its output bounds, so overlapping draws keep painter order whatever their
blending. Draws that only share an edge may reorder on accelerated renderers,
whose fill rules rasterize a shared edge once; SDL's software renderer keeps a
whole output pixel between them because it rounds rectangle edges. At most 64
runs are pending; clip, render scale and target changes, texture uploads or
destruction, readback, flush and present submit them first. Lists of 384 or
more vertices (terrain, bars) follow the pending runs as their own call. On
SDL's software renderer each run carries indices that reproduce SDL's per-call
duplicate-vertex detection, so triangle pairs still become the same rectangle
copies. Fully opaque fills replace the target instead of blending (alpha 255
gives the same pixels): accelerated renderers draw them with
`SDL_BLENDMODE_NONE`, and the software renderer fills their rectangles directly.

Native array/cache optimizations require supported desktop OpenGL features.
Software, portable SDL and unsupported native contexts retain their existing
rendering paths. Desktop measurements must not be presented as phone performance.
When changing this code, compare immediate and batched output at several zooms,
across wrap seams and clip boundaries, with overlapping translucent sprites,
wide outlines, carried icons, texture mutation/deletion and context recreation.
Advance an AI match between comparisons to exercise resource invalidation, and
check that each render leaves its simulation checksum unchanged. Keep commands,
seeds, binaries, captures and timing data under `artifacts/` for review.

## Skin materials

Procedural pattern repeats are set in `skinMaterialRepeat` in the shared GLSL:
classic glossy uses 4×, wood 3×, leather 5×, and woven fabric, stone, scales and
honeycomb 2×; other materials use 1×. The scale applies to material detail after
sampling the paint and material atlas, so painted markings retain their placement.
Studio, native rendering and sprite baking use these same settings. Worker and
warrior meshes retain the established paint UVs and carry a separate bounded
GUV1 sidecar for procedural detail, so rebuilding the surface does not move saved
paint. Both renderers use the paint UVs when a sidecar is absent; malformed
sidecars are rejected. See the [unit asset pipeline](../../tools/unit-animation/README.md#experimental-live-colony-skins).

Colony-skin materials are declared once in `libs/gag/shaders/skin-materials.json`
(ids, keys, display names, picker groups, which materials grow fur shells, the
shell count and the fur length and depth bias every renderer uses) and shaded
once in `libs/gag/shaders/skin-material.glsl`. `buildsys/skin_materials.py`
compiles both into the generated `include/glob2/SkinMaterials.h`
(`SKIN_MATERIAL_COUNT`, `SKIN_MATERIAL_SHELLS`, the `SkinMaterials` table and the
GLSL text) for the desktop, web and mobile builds; Colony Studio imports the GLSL
raw, and the protocol package carries a mirrored `COLONY_SKIN_MATERIALS` list that
`packages/protocol/test/skinMaterials.test.ts` pins to the JSON.

Every material fills a `SkinSurface` (albedo, perturbed normal, roughness,
specular, metal, wrap, rim, cel, emissive, alpha) and one `skinLight` lights them
all, so the catalogue stays consistent. Meshes carry no tangents: perturb normals
with `skinTilt` from a UV-space height gradient (`SKIN_GRADIENT`), never from
tangent-space maps; scale micro-frequency octaves by `s.detail`, which fades to
0 as texels shrink below pixels, so grain shows in the studio but never aliases
in the baker's 128 px tiles. The wrappers only declare varyings and call
`skinShadeAtlas` (mesh renderers, with `SKIN_TEXTURE` defined per dialect) or
`skinShadeSphere` (swatches). Materials with `shells: true` are drawn
`SKIN_MATERIAL_SHELLS` extra times with vertices pushed along the camera-space
normal; their shader sets `alpha` to 0 where a shell carries no strand. Tiles
are cached per pose, so no material can animate over time.

To add a material: append it to the JSON, add `skinMaterial_<key>` and its
dispatch line to the GLSL, mirror the entry in `services/online/packages/protocol/src/skins.ts`,
then run `test/build_system/test_skin_materials.py`, the `SkinMesh` display
suite with `GLOB2_UPDATE_SKIN_FINGERPRINTS=1` once (it rewrites
`test/fixtures/skins/material-fingerprints.json` and writes contact sheets under
`artifacts/skins/materials/`) and review the sheets. While iterating on the
GLSL, `tools/skins/material_spheres.mjs` (run from `services/online/apps/web`) renders
every material on a sphere through headless Chromium in seconds, and the
`skins-materials.spec.ts` e2e captures each material on the worker at studio
resolution. Any shader edit changes the
sprite render revision and re-bakes every published skin.


## Scene renderer

Drawing reads an immutable `PresentationFrame` (`src/render/scene/`). It retains
the shared world snapshot, view request, captured timing and derived presentation
results. Unit, building, team, relationship, entity-index, effect and map data are
borrowed from standard snapshot components. There is no second entity extraction
or map-array capture. `MapDrawer::drawMap` requires an explicitly supplied frame;
`MapDrawer::drawSceneMap` accepts it directly. The draw functions are declared in
`src/render/MapDrawer.h` and the per-viewer `ViewState` in `src/render/ViewState.h`,
so `Game.h` carries no rendering declarations.

- Interactive engines capture the standard simulation preset and all graphical
  payloads at each completed observation: session, effects, statistics, history,
  telemetry, entity diagnostics and annotations. These inputs are frozen even
  while presentation preparation is busy. Camera movement and opening a panel
  derive another view from that observation; they never add live data to it. AI,
  map jobs and presentation borrow the same captured data through standard views.
  `SceneExtractor::prepare` accepts an immutable handle and request, never live
  simulation objects. Preparation runs on the shared compute executor. A complete
  frame is published through `SceneBuffer`; drawing keeps the previous complete
  frame while work is in flight. Simulation barriers do not join presentation.
  Interactive sessions open the next completed world's shared read boundary
  before waiting for tick pacing. AI polling and order delivery stay at their
  existing deadlines and reuse that publication; presentation does not wait a
  whole tick interval before starting. The native mailbox admits at most one
  preparation per world/client request and can replace an unconsumed completed frame.
  A camera refresh consuming a client request does not block presentation of a
  newer tick. If preparation overlaps a tick, retained-world camera refreshes
  leave the slot free until a fresh boundary is admitted, preventing continuous
  panning or zooming from repeatedly publishing stale animation state.
- Serial hosts and browsers use the same preparation path. Hosts without a compute
  worker pump bounded chunks between simulation work; cancellation does not wait
  on the browser event loop. Camera and selection updates can prepare from a retained
  world without capturing again. Its timing and executed-order revision remain
  associated with that exact world.
- Map views retain standard terrain, resource, occupancy, area and visibility
  components; vertex terrain is the authoritative, editable map state. Optional
  script areas use tracked chunks. Display-area masks, material availability,
  connections and overlays are derived in resumable preparation chunks. Cached
  display data is keyed by world identity, relevant revisions and view settings.
- Unit animation and building shooting fields live in the normal pointer-free state
  records. `UnitState` groups position, targets and movement modes at the front,
  followed by health, hunger and cached abilities. Live units and captures use
  the same naturally aligned record; save and checksum encoders visit named
  fields explicitly, independently of its in-memory declaration order.
  Selected panels borrow those records and immutable catalogs; presentation
  calculations and formatting belong in frame storage. Catalog definitions are frozen
  once per revision, so a later owner reconfiguration cannot change a retained frame.
  AI telemetry, debug gradients, failure histories and statistics histories use
  optional snapshot components retained at the boundary. A frame selects the
  components its view needs. Statistics histories share immutable samples
  between observations until the next history revision; the compact HUD sample
  ring remains independent. Alliance controls read session player identities and
  team masks; objective and hint dialogs retain immutable narrative payloads that
  are reused until mutation. Opening these dialogs does not park simulation.
  Shift-click diagnostic dumps select the displayed generation and serialize it
  only if that same entity still exists at the explicit owner boundary.
- The editor and standalone tools explicitly obtain an owner-boundary snapshot before
  preparing a frame. The editor keeps one `SceneExtractor` and its current scene
  (`EditorSceneCache`) and prepares again only when the boundary's
  `boundarySerial` or the scene request changes. Every capture by a snapshot
  store takes a new process-unique serial, kept by reuse and projection and never
  repeated after `Store::reset()`. Map and owner edits therefore rebuild the scene,
  as do selection, overlay, active team and zoom; panning and idle frames redraw it.
  Offline map images and diagnostic PNGs use the same drawing
  passes and graphics-thread asset ownership. Diagnostic preparation consumes the
  published boundary rather than recapturing from the live game. A bounded diagnostic
  batch owns its fields and snapshot independently; PNG preparation and export do
  not park the simulation. Occupied publication slots stop further diagnostic
  admission, with skipped or superseded output reported explicitly.
- The animated menu colony captures the presentation preset with its simulation
  boundary and uses the same bounded preparation producer. Drawing and resizing
  never capture another world; headless menu simulations admit no presentation.
- `test/build_system/test_scene_boundary.py` checks the rendering dependency boundary;
  compile-time preparation tests reject live `Game`, `Map`, `Unit` and `Building`
  inputs. New authoritative values belong in the owning standard snapshot component,
  with capture/lifetime tests; view-dependent calculations belong in preparation.
- Routine selection resolves generation-checked snapshot identities. Command
  admission still validates targets on the simulation owner. Exceptional editing,
  saving and diagnostic actions retain explicit owner access.

### Simulation tick rate

Normal speed is 30 simulation ticks per second, independently of render FPS.
Speed presets scale relative to that rate. Engine deadlines accumulate
nanoseconds before rounding host waits to milliseconds; the relay advertises
30,000 millihertz and clients use its negotiated interval. Game clocks,
statistics rates and newly configured minute-based winning conditions use 30 TPS.
Autosaves retain approximately one-minute real-time spacing, and camera panning
retains its presentation cadence.

Saved tick counters, pending orders and timers resume through the selected game-speed configuration. Sim-version admission prevents incompatible engines from sharing an online match.

### Target render FPS

**Settings > Display & graphics > Advanced graphics > Target render FPS** sets a
local drawing ceiling for games, replays, menus, dialogs and the editor. Presets
are 25, 30, 60, 90, 120, 144, 165 and 240 FPS, plus Unlimited. The default is
60 FPS, including profiles without the new `targetRenderFps` preference; `0`
means Unlimited. Unsupported or malformed values load as 60. Changes apply at
once and are independent of graphics detail presets. Lower render ceilings reduce drawing work and can reduce camera and interpolated animation smoothness.

`RenderFramePacer` uses nanosecond drawing-start deadlines associated with the
graphics context. Screen hosts skip painting while still dispatching input,
advancing jobs and servicing game/network updates. Browser hosts paint separately
on animation-frame callbacks, including while timer-driven jobs are running.
Sub-frame scheduling jitter retains the target clock phase; a missed full frame
discards the backlog. Foreground resume and graphics recreation
reset pacing. Display refresh, rendering cost and existing slower screen cadences
can keep the actual rate below the selected ceiling. Unlimited removes this
limiter, without overriding display synchronization or screen update scheduling.
Threaded games and the native map editor wait `GAGCore::pacedScreenWait`: until
the pacer's next frame and at most `InputWakeMilliseconds` (8 ms) for input, so
a frame slower than the target period is followed at once. Screens opt in with
`Screen::followsRenderPacer()`. With Unlimited, a game redraws continuously; the
editor does so while it has input or a new scene, and otherwise waits the input
wake (`Screen::executionIdle()`), so an idle editor draws at most about 125 FPS rather
than spinning.

Simulation speed, save/replay formats and network contracts are independent of
this preference. Headless runs, offline image exports and renderer benchmarks do
not opt into the interactive limiter. Recording output FPS remains independent;
recordings cannot gain new visual detail from frames the application did not draw.

### Device profile and adaptive defaults

`DeviceProfile` (`src/app/DeviceProfile.h`) classifies the device once at startup
as low, mid or high and records its inputs. Everything that adapts to the device
reads this profile; nothing probes hardware itself, and nothing in the simulation
path reads it, so simulation checksums and `SIM_REVISION` are unaffected.

Inputs: `SDL_GetSystemRAM` (on iOS this is `hw.memsize`, the same figure as
`NSProcessInfo.physicalMemory`; browsers use `navigator.deviceMemory`, because SDL
reports the WebAssembly heap limit there) and the hardware thread count, which
`AssetLoader::Options::environment` also budgets from; whether the device is a
phone or tablet (`GAGCore::platformIsMobile`; in a browser `userAgentData.mobile`,
an Android, iPhone or iPad user agent, or a Mac user agent with touch points,
which is iPadOS); on Android
`ActivityManager.isLowRamDevice()`, `getMemoryClass()`, `getLargeMemoryClass()`
and `MemoryInfo.totalMem` through `Glob2Activity.getMemoryProfile` (totalMem
replaces SDL's figure when known); on Windows and Linux windowed runs, the largest
device-local heap of a discrete Vulkan GPU, probed on a background thread and
folded in by `DeviceProfile::poll()`. Phones share memory between CPU and GPU,
integrated GPUs use system memory and legacy GL has no portable query, so they
have no VRAM rule.

The thresholds are provisional until phone measurements exist, and all of them
live in `deviceThresholds`:

| Tier | Rule | Auto artwork | New-map default |
| --- | --- | --- | --- |
| Low | under 3 GiB or an Android low-RAM device; or at most 2 hardware threads (map size only) | original (high resolution when only the cores are low) | 128×128 |
| Mid | 3 to 6 GiB, or a phone that reports nothing | high resolution | 128×128 on phones, 256×256 elsewhere |
| High | over 6 GiB, or nothing reported off phones | high resolution | 256×256 |

A phone or tablet that reports no memory is Mid: Safari, every iOS browser and
Firefox have no `navigator.deviceMemory`, and most such phones hold 4 to 8 GB.
A discrete desktop GPU with less than 1 GiB of device-local memory also starts
with original artwork. `GLOB2_DEVICE_TIER=low|mid|high` or
`play --device-tier low|mid|high` overrides the tier for testing.

**Auto semantics.** Adaptive settings have an Auto state, and Auto is the
default; Original and High resolution are explicit choices that the device never
overrides. Settings > Display shows what Auto resolved to and why, for example
"Automatic: original on this device (2.7 GB RAM)". Preferences store
`highResolutionArtworkChoice=auto|off|on` and keep writing the legacy
`highResolutionArtwork` key (0 only for Off) for older builds. Profiles from
before Auto wrote `highResolutionArtwork=1` whether or not the player chose it,
so `1` migrates to Auto and `0`, which departs from the old default, stays an
explicit Off; on mid and high devices Auto resolves to the old value.

**Browser graphics detail.** In the browser, the eight detail effects (clouds,
cloud shadows, building particles, full magic effects, translucent panels and
path lines, smooth progress indicators, decorative animations) start on a
shared adaptive default: all on, except all off on a Low device. Changing any
one of them in Settings, or launching with `--graphics-detail`, ends the
default for good, and preferences store it as `graphicsDetailAdaptive`. Native
builds start with every effect on and no adaptive default.

The map size is gameplay, so only the defaults adapt: a custom game without a
saved lobby, Reset to defaults, and a new map in the editor start at the tier's
size where the landscape can host the colonies at it (otherwise they keep 256×256,
so the lobby stays startable), and the player can still choose any size. A saved lobby keeps the size it
was played at. Online rooms use the host's map; a low-tier device in a room whose
generated map is larger than its default sees a warning, and the match never
changes.

**Session step-down.** With artwork on Auto, high resolution turns off for the
rest of the session, through the asynchronous `Sprite::requestHighResolution(false)`
path, when the OS reports memory pressure or the game sustains hitches: 40
presented frames over 50 ms within 15 seconds, outside the first 20 seconds of a
match and ignoring gaps over a second (an O(1) ring of timestamps). The player is
told once in game, and Settings > Display offers to keep original artwork as the
default. Memory signals respond by severity (`DeviceAdaptation::respond`):

| Signal | Response |
| --- | --- |
| iOS memory warning (`SDL_EVENT_LOW_MEMORY`) | trim caches; step down at once |
| Android `TRIM_MEMORY_RUNNING_MODERATE` | trim caches |
| Android `TRIM_MEMORY_RUNNING_LOW` | trim caches; step down at once |
| Android `TRIM_MEMORY_RUNNING_CRITICAL`, `onLowMemory` | smallest budgets; step down at once |
| Android `TRIM_MEMORY_UI_HIDDEN`, `BACKGROUND` and higher | background trim; no step-down |
| Browser Wasm heap grown to four fifths of its 2 GiB maximum | smallest budgets; step down at once |

The browser has no OS memory signal, and its Wasm heap only grows, so
`DeviceAdaptation::serviceFrame` checks the heap's size every frame and sends
`browser-heap-high` at most once a page load. An allocation past the maximum
stops the match ("The game ran out of memory", `Script::MemoryFailure`); legacy
GL reached it in long matches with HD artwork, programmable GL and Software stay
well below it.

SDL 3.4's `SDLActivity` turns every `onTrimMemory` level into one ungraded
`SDL_EVENT_LOW_MEMORY`, so `Glob2Activity` also passes the level to native code,
where a lock-free queue hands it to the game thread once a frame; on Android the
ungraded SDL event only feeds `ScreenStack`'s existing handling. An explicit
artwork choice is never stepped down, but caches are still trimmed. All trims go
through `DeviceAdaptation::trimPresentationMemory`, which forwards to the
renderer's bounded `GraphicContext::trimPresentationMemory` (a background trim
for `Background`, a low-memory trim otherwise) and keeps the retired HD frames'
budget at zero after a critical signal. Render budgets stay as the device was
created with. The tier, its inputs and every step-down with its OS signal are
logged in the [performance telemetry](../development/performance-telemetry.md).

### Simulation thread

Interactive sessions run the simulation on its own thread (`src/engine/sim/SimulationRunner.h`)
on native platforms; there is no setting. Both browser runtimes, and any platform where
creating the simulation thread fails, run the same session serially (`Engine::stepSession`), which
also remains the headless default and the equivalence reference.

- The simulation thread paces itself with the speed presets and runs ticks
  (`Engine::simulationStep`: orders, network, `Game::advanceLiveSimulation`). Before pacing,
  the engine publishes the completed world's standard simulation snapshot,
  freezing optional graphical inputs even when preparation is busy. Paused
  camera, selection and panel changes project from that retained observation
  without copying entities or consulting a newer live world. Headless observation
  options remain minimal. Presentation preparation
  borrows that publication and runs on the compute executor. A worker publishes
  the completed `PresentationFrame` into a `SceneBuffer` (lock-free triple buffer);
  only one preparation is in flight, and a newer complete frame can supersede an
  unconsumed one. Neither capture nor presentation admission depends on drawing
  completing first.
- The main thread draws the newest complete frame when the render ceiling permits.
  Routine input, selection, client events and script highlights use the frame and
  client-owned state without parking the simulation. Exceptional live-state work
  (save capture, owner-backed settings, viewpoint changes and diagnostic dumps) uses
  `GameGUI::parkForClient` / `SimulationRunner::withGame` at a tick boundary.
  Alliance, objective and hint dialogs read immutable snapshot payloads.
  Autosave scheduling publishes an atomic pending request; the client owns the
  writer and captures the save at that explicit boundary. Telemetry windows cross
  a locked mailbox after simulation work finishes.
- Only state both threads use is shared: `ClientRequests`' view is locked; `gamePaused`,
  `hardPause`, `isRunning` and the CPU-load history are atomics. A pause order or the local
  player leaving takes effect on the simulation thread in the same tick, as in serial
  execution. Scene requests (selection, local team and view options) cross a locked
  latest-value mailbox; the simulation fills in its own tick timing at capture.
- GUI orders cross `ClientOrderQueue`, which atomically takes orders and coalesces
  flag moves without exposing iterators. Locally issued entity orders carry the
  displayed incarnation and world identity; admission drops obsolete targets before
  sending or recording them. These guards are not serialized and do not alter the
  wire protocol. Building actions and threaded selection read the displayed Scene;
  touch gestures retain incarnation identities. Area previews are client-owned layers
  over the Scene: active strokes and queued paint remain visible until an execution
  acknowledgement is included in the acquired Scene. Farm paint eligibility reads
  retained growth/rules inputs when the experiment is enabled.
- Simulation RNG state belongs to units, buildings, map operations, stories and AI
  controllers. Explicit ownership keeps draws independent of the executing thread.
  `GLOB2_SIM_THREAD=1` runs headless sessions on the simulation thread for
  `check_sim_thread.py --candidate-env GLOB2_SIM_THREAD=1`; `GLOB2_SIM_THREAD=0` keeps
  any session serial, for tests that count frames against a scripted host clock.
- The simulation thread paces on the host's clock (`Engine::sessionClock`): the clock the
  host last passed in, advanced by real time. Time the application spent in the
  background is therefore not caught up after resuming, as in serial execution.
  GUI updates use `SDL_GetTicks()` instead: touch event timestamps and momentum
  must share the SDL clock, including after the session clock has been suspended.
- Threads carry a scheduling role (`GAGCore::ThreadSupport::ThreadRole`) in three tiers.
  *Interactive* is the core loop (the windowed client's main thread, which also draws,
  and the simulation thread) plus the audio mixer, which SDL then raises to high
  priority so playback does not stutter. *Worker* is every other thread: the compute
  executor (including the simulation barriers the core loop waits on), parallel
  presentation, asset streaming, the gradient coordinator, previews, recording, network
  transports and helper threads. `ThreadSupport::launch` starts threads as workers, and
  threads created directly set the role themselves. *Background* is work nothing waits
  on: `render/BackgroundWork.h` composition and the background file writer. Roles map to
  QoS classes on Apple platforms (user-interactive, user-initiated, utility; utility
  also prefers efficiency cores), thread priorities on Windows (above normal, normal,
  below normal) and nice values on Linux and Android (−4, +2, +5). Unprivileged Linux
  processes cannot go below nice 0, so there the core loop and audio stay at 0, still
  ahead of workers. Headless commands leave the main thread at the default, so batch
  runs do not crowd the desktop. Roles never change what a tick computes;
  `GLOB2_THREAD_PRIORITY=0` turns them off for comparisons. Under contention the core
  loop can wait on workers that run behind it (priority inversion); the tiers are an
  architectural choice that favours frame and input latency in that case.
- Values the client sets while drawing and preparation reads (viewport, drawn map size,
  overlay, observed building) go through `ClientRequests`, never through `Game` or `Map`
  fields. To check for races, build with `CXXFLAGS="-g -fsanitize=thread"
  LINKFLAGS="-fsanitize=thread"` and run a windowed `dev random-games --display` session or a headless
  `game run` with `GLOB2_SIM_THREAD=1`. Build against the pinned SDL3 prefix
  with `GLOB2_SDL3_PREFIX`; sanitizer builds use the same native SDL3 dependency set.
  `.github/workflows/thread-sanitizer.yml` runs both games under ThreadSanitizer nightly,
  through the main build workflow and on demand. The risk selector includes it
  for code shared by threads; add boundaries in `.github/scripts/ci_policy.py`
  when new code becomes shared between them. Drafts defer it. It does not report thread leaks, because SDL3
  leaves its own startup threads unjoined at exit, and uses the dummy audio driver, because
  PulseAudio's uninstrumented mainloop thread reports races inside libpulse.
  The windowed fixture explicitly uses the game's software renderer and disables SDL's accelerated
  framebuffer presentation, keeping uninstrumented Mesa worker threads out of the sanitizer run.
  GPU rendering remains covered by the renderer suites. Narrow, explained suppressions for
  library shutdown races live in `test/tsan.supp`; never suppress game code there.
  Draft PRs skip it.
- `SceneBuffer<T>` (`src/render/scene/SceneBuffer.h`) hands Scenes between the threads without
  either waiting for the other.

### Smooth unit motion

The **Smooth unit motion** graphics setting (`Settings::unitInterpolation`,
on by default on all platforms) draws units between ticks, so threaded play at display rate uses all
32 animation frames per direction instead of repeating one pose per tick.

- A unit's drawn position and animation frame follow `delta`, which the simulation
  advances by `unitActionStepSpeed` computed from the captured unit record each tick. Each frame, `GameGUI::drawAll` sets
  `MapRenderState::unitMotion` to the elapsed fraction of the tick interval since the
  captured tick (`PresentationFrame::tickTime`, `PresentationFrame::tickInterval`; `src/unit/render/UnitMotion.h`).
  Unit drawing, path lines, off-screen markers and worker circles add that fraction of
  `stepSpeed` to `delta`, stopping at the end of the current action.
- Motion is 0 when the setting is off, when the game is paused, and when the simulation
  runs uncapped. At 0, drawing is identical to drawing the ticked state; keep it that way
  so captures with the setting off stay comparable.
- A unit that turns or stops at the next tick can jump back by at most one tick of motion.
  Serial execution draws right after each tick, so the setting has almost no effect there.

### Smooth fog of war

The simulation's fog of war is binary per tile, and its buffers swap every
`FOW_SWITCH_TICK_MASK + 1` ticks (`Map::switchFogOfWar`), so every tile that left sight
during one window darkens on the same tick. The **Smooth fog of war** graphics setting
(`Settings::smoothFog`, on by default) fades that change in the renderer only; the
simulation, saves and replays are unaffected.

- `FogFade` (`src/render/FogFade.h`), kept per view in `MapRenderState`, records for each
  tile whether it is fogged, its fade level when that last changed and the tick it changed
  on. `MapDrawer::drawMap` updates it once per frame from the drawn Scene, and resets it while
  the fade is not drawn (setting off, or `DRAW_WHOLE_MAP`), so it is `active()` exactly when
  the frame draws the fog faded. A tile changing state fades linearly from wherever it had
  reached, into the fog over `FogFade::DARKEN_TICKS` (37.5, or 1.25 seconds at normal speed)
  and out of it over `FogFade::REVEAL_TICKS` (4). A new map, other visible teams, a step back in time, a jump
  forward of more than `FogFade::SETTLE_JUMP_TICKS` (64) or a reset settle every tile
  without fading.
- Fades run in game time: the Scene's tick plus the elapsed fraction of the tick interval
  (`unitMotionFraction`), independently of the smooth unit motion setting. They stop while
  paused and follow the game speed.
- The shade draws each square from its four corner levels. The level all corners reach is
  a fill (`FogFade::fillAlpha`); each higher corner level adds the shade sprite masked to the
  corners reaching it, drawn with `FogFade::layerAlpha` of the difference. The alphas are
  chosen so the layers compose to the fill of the top level (within rounding, where the
  sprite's pixels are at their peak; the derivation is in `FogFade.cpp`), and fully fogged
  or fully clear corners draw the same single fill or sprite as with the setting off.
- Enemy units fade with the clearer of their tile and the tile they come from, and so does
  everything drawn for them: bars and status badges (through the `opacity` parameter of
  `MapDrawer::anchorBars`, `drawStatusPip`, `drawPointBar` and `drawHealthBar`; queued bars take
  it from their anchor), selection circles, the level-up number and magic effect, the
  carried resource and the accessibility label. A fading unit's sprite is translucent and so
  leaves the unit sprite batch; only the few units at the edge of the fog do.
- Still binary: undiscovered black, the minimap, mouse and touch picking, bullets and
  explosions, and remembered enemy buildings. Bullets near a fading unit therefore vanish
  at the fog swap.

### Unit effects at buildings

When a building produces or changes a unit, accelerated renderers play a short effect
on it; the software renderer draws the unit as before, and **Detail effects: reduced**
(`Settings::buildingParticles`) turns them all off. Timing and shapes are in
`src/render/UnitEffects.h`.

When a swarm produces a unit:

- The swarm glows in a light version of its team colour for about 0.4 seconds, at 20% of
  full strength.
- The unit fades in over 10 ticks, sliding 2 pixels out of the swarm, as a team-colour
  silhouette that settles to normal shading over one second while a few twinkles drift
  up over it.
- A cloud of team-coloured dust twinkles around the unit, with motes flashing white,
  and fades out within 0.75 seconds. It travels with the unit as it walks away, its
  motes spreading outward on top of the unit's own motion.

When a unit comes out of a building that healed, fed or trained it:

- It shimmers as a silhouette in the service's colour (heal green, feed yellow, training
  white-gold) that settles over one second from 90%.
- Seven copies of that building's own map icon (`presentation.iconFrame`, the glyph on
  its zoomed-out chip) burst out of it at random angles, distances, sizes and start
  times, then drift and fade within about 0.9 seconds. Like the dust, they are placed
  around the unit's drawn centre each frame, so they keep pace with it rather than
  being left behind. They never fly into the sector above the unit, where level
  numbers rise.
- A trained unit floats its highest new level, displayed as in the unit panel, rising
  like a combat level-up number.

When a hungry unit defects to the team whose inn will feed it, it shimmers in its new
team's colour, brightening over two thirds of a second and settling over one more.
What else plays depends on who watches:

- The team that gained it sees a cloud of motes in its colour gather onto the unit (the
  swarm's dust played backwards) and a soft heart in its colour pop up, wobble and rise.
- The team that lost it sees motes in its own colour fall off the unit, and a heart in
  its colour appear above, tremble, crack down the middle and fall in two halves.
- Anyone else, and every replay viewer, sees both clouds of motes and no heart.

Because conversion is caused by hunger, the effect starts where the unit stood when it
switched, not at a building, and follows the unit as it walks on; it is hidden on a
fogged tile and, unlike the others, dropped rather than left behind when the unit
is not drawn.

`Building::swarmStep` records spawns in `GameAnimations` beside death animations. A
successful feed, heal or training (some level rose) at the end of a stay
(`Unit::handleDisplacement`) records a pending service by unit gid; it plays when the
unit actually comes out (`Unit::handleMovementExitingBuilding`, or `expelFromBuilding`
for a unit already on its way out). A conversion (`Unit::handleActivity`) records its
effect under the unit's new gid with the old team, dropping any effect left under
the old gid. A unit expelled mid-stay, a trainee leaving while
upgrades are disabled, or one whose course became invalid plays nothing. Records are
render-only, absent from checksums, saves and replays, and kept only with a display.
They are captured into the snapshot's effects (`UnitEffectRecord`), and `MapDrawer::drawMap`
indexes them by unit and producing swarm for the frame. Each part runs from the
record's age plus the drawn tick fraction, so it stops while paused. Dust, icons,
twinkles and level numbers are drawn after the buildings, which would otherwise cover
them.

Sprites get the silhouette from `Sprite::silhouette`, a cached copy of the native frame
filled with one colour, whose light is added over the sprite
(`GraphicContext::drawSurfaceAdditive`); the swarm uses its team-colour layer alone.
Dust motes add the light of their white cores and glints the same way, while their team
colour blends normally, since added red turns yellow over grass. The icons are the white
`mapicon` glyphs filled with the service's colour and blended normally, picking the
largest pre-sized frame that fits on screen. Skinned meshes are
brightened instead by adding their composited tile again, tinted (the `glow` arguments
of `GraphicContext::drawSkinMesh`); pre-rendered skin sprites draw without it. Dust
stars, glows and hearts are drawn procedurally by `UnitEffectArt`, one set per team hue, so the
effects ship no new image assets.

### Additive draws

`GraphicContext::drawSurfaceAdditive` adds a surface's light (its colour times alpha) to
what is already drawn instead of blending over it, so effects can brighten rather than
tint. It first flushes queued batches to keep painter order, then uses a filtered,
additively blended blit:

- **Device renderers** (SDL GPU, programmable GL, WebGPU): a `DrawPacket` blit with
  `Blend::Additive`.
- **Portable SDL renderer:** `SDL_BLENDMODE_ADD` on the texture for that run only;
  additive draws batch separately from ordinary draws of the same texture.
- **Legacy GL:** an immediate quad with `GL_SRC_ALPHA, GL_ONE`.

The software renderer draws the surface normally. A backend opts in through
`RenderBackend::blitAdditive`.

See [adaptive zoom detail](zoom-detail.md).

### Swimming units

The swim artwork is seen from above with the body lying flat, so a swimmer is placed in
the water rather than on it by fading its limbs, not by cutting it at a waterline.
`MapDrawer::drawUnit` sets a `RadialFade` (`libs/gag/include/RadialFade.h`) on the graphic
context with a `RadialFadeScope` (`libs/gag/include/RenderStateScope.h`) around a
swimmer's sprite: the torso stays opaque and opacity falls to a floor towards the
outstretched limbs. Every unit draw path honours it (classic sprites with or without the
team-colour shader, pre-rendered skin sprites, and live skin meshes on the legacy GL and
device renderers) by drawing the quad as a grid whose vertex opacity carries the fade;
only the native software surface without a renderer backend draws unfaded.
Each arm stroke of the swim clip sends a ring out from the limb tips, timed from the
unit's animation phase so ripples follow the visible stroke. A moving swimmer's ring fades
to nothing ahead of it, so the ripple spreads backwards; one treading water ripples all
round. The ring is drawn as short line segments whose opacity carries that crescent; the
constants, ring timing and segment geometry are in `src/unit/render/SwimmingPresentation.h`.
All of it is presentation: it reads the unit's action, heading and animation phase and
changes no simulation state.

## Clouds, bars and resource batches

Cloud patches in the flat game and editor views use a coarser, world-anchored
lattice when zooming out to half size or smaller. Patches retain at most their configured
1:1 size on screen; normal zoom keeps the original sampling. The field and animation
time remain unchanged, but distant clouds have less fine detail. The torus view
retains its separate sampling budget.

Renderers without a cloud shader, including the portable SDL renderer, sample
the shadow and cloud lattices on the CPU. A cloud thread of its own samples the
next lattices, with a margin of three cells around the view, while frames draw;
the graphics thread draws the newest finished lattices at their world position
and samples on the graphics thread (with a few helper threads) only when none
covers the view, as on the first frame or after a jump. The portable SDL
renderer draws each alpha map as one small lattice texture stretched with linear
filtering, texel centres on lattice points, instead of four triangles per cell.

Point bars batch opaque fills within each bar using bounded OpenGL or SDL geometry
submissions. OpenGL outlines and translucent fills preserve their original order;
software surfaces retain their existing path. Full-map terrain and resource passes
skip fog discovery queries when `DRAW_WHOLE_MAP` already makes every tile visible.

Flat-map resources use a bounded OpenGL/portable SDL sprite batch, including
standalone frames from partial HD packs. Draws sharing a texture and alpha can join an earlier run
only when their rectangles do not overlap intervening runs. Conservative bounds
preserve the order of overlapping artwork while reducing draw submissions and
texture switches without changing sampling or allocating another texture atlas.
OpenGL texture uploads flush pending draws, and the scope flushes before leaving
the resource pass. Software surfaces and dynamic team-color sprites retain their
existing paths; cache-backed team-color surfaces cannot be deferred safely.

### Portable shader build artifacts

`libs/gag/shaders/renderer-manifest.json` declares skin/rig, world/cloud, smooth-point and 2D primitive
vertex layouts, uniform blocks and resource bindings. Generate build artifacts
with `python3 tools/shaders/generate.py --output artifacts/shaders/generated`.
The output contains GLSL ES 3.00, desktop GLSL 3.30 and Vulkan GLSL 4.50 sources,
`metadata.json`, and `RendererShaderLayouts.h` with matching C++ constants.
Uniform layouts are column-major std140; SDL3 primitive vertices retain their
position, float colour and texture-coordinate layout. Primitive colours remain
straight alpha. The fragment `textured` flag disables sampling for solid fills. `uvBounds`
clamps sampling to the selected source rectangle's texel centres, preventing
linear filtering from bleeding adjacent atlas entries; generic triangles use
the full texture bounds.

The `team-color` material draws a sprite's team layer on the device renderers
(SDL GPU and programmable GL) with the CPU recolour computed per texel in the
shader: `DrawableSurface::shiftHSV`'s hue-only shift through
`RGBtoHSV`/`HSVtoRGB` and `Color::setHSV`'s truncation, with unpremultiply and
the upload's integer premultiply around it for premultiplied (linear, HD)
textures. It fetches texels itself and recolours each before its own bilinear
or trilinear filter, as the CPU recolours its copy before upload. Vertices use
the primitive layout with a grey tint in colour x and the hue shift, in degrees,
in colour y, read through a `flat` varying, so draws of one texture batch across
teams. A `DrawPacket` with `Material::TeamColor` and `teamHue` carries it, and
devices advertise it with `RenderDeviceCapabilities::teamColor`; SDL GPU loads
its shaders and both blend pipelines with the device. `DrawableSurface::drawSprite`
then draws the uncoloured team layer through `GraphicContext::drawTeamColorSurface`,
so `Sprite::getColoredSurface` makes no clone: rotation maps and the unit recolour
cache stay empty and `Sprite::highResolutionStats().coloredFrames` stays zero.
A radial fade (a swimmer) draws its faded grid through the same material.
Without the material (older shader sets, a driver that rejects it, WebGPU, or
`GLOB2_DISABLE_UNIT_SHADER` set) team layers keep the CPU recolour, as do sprites
drawn into a CPU surface rather than the screen; the
memory report's `cpu_team_color_bytes` and `team_color_frames` count those copies.
Sector and truncation boundaries make the recolour discontinuous, so the shader
follows the CPU's single-precision operations exactly: it divides with integers
(GPU division is not correctly rounded) and multiplies each product that feeds an
addition by an opaque uniform one, so contracting it into a fused multiply-add
rounds the same. On SDL GPU the result equals the CPU recolour drawn by the same
device; linear filtering differs by at most one level (fixed-point hardware
weights), and minified mip chains differ where a level averages different hues
before recolouring rather than after (`SDLGPUDeviceTest`, `DeviceTeamColor`).
Mesa's programmable GL moves a few channels by one level.

Pass `--glslang /path/to/glslangValidator` to compile native SPIR-V ahead of time.
The manifest pins build-only glslang **15.1.0** and Vulkan 1.0; another compiler
version fails instead of silently producing different binaries. Shader source
regeneration invalidates older SPIR-V files. Shader compilers are not runtime
dependencies. Generated `metadata.json` records `shaderArtifacts[family][variant][stage]`
for each produced vertex/fragment artifact, with its relative path, shader format,
entrypoint, SPDX licence and SHA-256. The generated shaders retain the game’s
`GPL-3.0-or-later` licence; build-only translators are not shipped. Vulkan entrypoints are read from compiled SPIR-V;
WebGPU entrypoints are read from translated WGSL. Factories must reject missing
format receipts instead of assuming a Metal or Direct3D entrypoint. Generating
without native tools advertises only the generated GL source formats.
Optional `--spirv-cross` and `--dxc` generate ahead-of-time Direct3D 12 DXIL
from the same validated SPIR-V. The manifest pins the Linux SPIRV-Cross package
and binary digest, and DXC release **v1.8.2505.1** with its archive and binary
digests. That official Linux release reports a `1.9(dev;4950-b106a961)` compiler
version; verification checks its actual version rather than assuming the tag is
the executable's version. DXIL entrypoints come from compiled reflection.
Translation also emits Metal 2.1 sources and records their actual entrypoints,
without advertising `.metallib` support. Apple offline compilation must produce
and validate libraries before the Metal factory can accept their receipts.
Generating without these tools removes stale native-format outputs.
On an Apple build host, compile the translated output separately with
`tools/shaders/generate.py --output OUTPUT --metal-sdk macosx
--metal-tool-lock LOCK.json` (or `iphoneos` / `iphonesimulator`). The JSON lock
contains the exact `xcodebuild -version` output (`xcodeVersion`), SDK name and
version (`sdk`, `sdkVersion`), and SHA-256 digests of the resolved `metal` and
`metallib` executables (`metalSha256`, `metallibSha256`). It must match the existing
Xcode pin in `platforms/mobile/toolchain.json`; `--developer-dir` selects a bundle without
changing the system selection. This separate step compiles MSL to AIR, links
`.metallib`, checks the library magic and compiled function name, and records
source and library hashes with the entrypoint. Failed or incomplete builds leave
Metal unavailable. A verified Apple tool lock and actual SDK compilation are
required before producing release libraries; mock tests verify invocation and
receipt handling only.
Apple builds run all of this automatically; see [Apple Metal shaders](#apple-metal-shaders).

#### Apple Metal shaders

Every macOS app and iOS device build compiles SDL GPU's Metal libraries as part
of its ordinary shader step, with no environment or manual translation: the CMake
build passes `--apple-sdk macosx` on macOS hosts, and iOS device builds use
`iphoneos` (the pinned SDL GPU does not run in the Simulator, so simulator builds
have none). `buildsys/renderer_shaders.py` then:

1. provisions pinned glslang (`buildsys/glslang.json`) and SPIRV-Cross
   (`buildsys/spirv-cross.json`, tag `sdk-1.3.239.0`) from hash-verified source into
   the development store, once per machine (`python3 buildsys/spirv_cross_tool.py`
   does it ahead of time). The source-built SPIRV-Cross translates byte-for-byte
   like the pinned Ubuntu `2021.01.15+1.3.239.0-1build1` binary, so a Mac produces
   the same `metalSources` receipts Linux does; `generate.py` accepts either the
   pinned Ubuntu binary or a receipted build of the pinned source;
2. compiles the translated sources with `xcrun metal` and `metallib`, checked against
   the committed lock `buildsys/metal-tool-lock.json`. iOS shaders use the deployment
   target pinned in `platforms/mobile/toolchain.json` so they can load on older supported devices;
3. ships the runtime artifacts: `buildsys/bundle.py` copies the shader set,
   Metal libraries included, into `Glob2.app/Contents/Resources/shaders`, which the
   runtime finds through the file manager, and `platforms/mobile/ios.py` into the
   iOS app. Translated sources, AIR and the layout header stay behind.

The lock lists every reviewed Apple toolchain: the exact `xcodebuild -version`
output and, per SDK, the SDK version and SHA-256 digests of the resolved `metal`
and `metallib` executables:

```json
{"toolchains": [{"xcodeVersion": "Xcode 27.2\nBuild version 27B52",
                 "sdks": {"macosx": {"sdkVersion": "27.2", "metalSha256": "…", "metallibSha256": "…"},
                          "iphoneos": {"sdkVersion": "27.2", "metalSha256": "…", "metallibSha256": "…"}}}]}
```

A build compiles with a listed toolchain when one matches this Mac. iOS entries
also require the Xcode pinned in `platforms/mobile/toolchain.json`; macOS entries
cover each release runner image's own Xcode. Otherwise:

- **Development builds** record this Mac's toolchain as
  `BUILD/metal-development-lock-SDK.json`, warn, and compile with it, so a developer
  Mac on another Xcode still gets SDL GPU. The receipt marks the libraries
  `metalToolLockKind: development`. Without full Xcode, or when the translators
  cannot be provisioned, a development build warns and carries on without Metal
  (SDL GPU then reports itself unavailable).
- **Release builds** set `GLOB2_METAL_REQUIRE_COMMITTED_LOCK=1` (the macOS release,
  Mac App Store, GOG and TestFlight workflows do) and fail instead: on an unlisted
  toolchain, on missing translators, or when the packaged app lacks the complete
  Metal set.

`tools/shaders/metal_tool_lock.py` writes a Mac's entry (`--output`), merges entries
(`--combine`) and checks the committed lock against a Mac (`--check`, printing the
nearest entry's differing fields and the entry that would match). The lock changes
only through a reviewed pull request. The **Apple Metal tool lock** workflow
(`.github/workflows/apple-metal-tool-lock.yml`) runs on every release runner image
(`macos-15`, `macos-15-intel`, `macos-26`, and `xcode-27` with the pinned Xcode for
iOS) on manual dispatch and on pull requests that change the lock, the Xcode pin or
the lock tooling. It fails while any release runner's toolchain is missing, and
uploads each runner's entry plus a combined candidate lock to review and commit.
When a runner image moves, its release job fails with the differences; dispatch the
workflow, review the candidate, and commit it, dropping toolchains no release job
uses any more.

`GLOB2_RENDER_SHADER_INPUT` with `GLOB2_METAL_SDK` and `GLOB2_METAL_TOOL_LOCK`
remains for importing an externally translated bundle (below); an Apple build that
imports one without them compiles it with the automatic lock choice.


Native skin shaders convert GL clip depth to Vulkan's 0..w range;
material logic and rig deformation still come from the existing authoritative
GLSL and `SkinDeformation.h`, including Studio and baker consumers.

`tools/shaders/prove_webgl.mjs artifacts/shaders/generated` executes the generated
programs in Playwright Chromium: every registered atlas material, rest and posed
GPU rig, fur shells, depth occlusion, solid/textured primitives and alpha. Use
`PLAYWRIGHT_MODULE` for an existing Playwright package and `PLAYWRIGHT_CHROMIUM`
for an explicit installed Chromium executable. Its JSON result belongs in the
artifact directory. This proof does not establish native rendering or cross-driver
pixel equivalence. MSL and DXIL translation and platform hardware execution
remain separate acceptance gates.

WGSL generation additionally uses build-only Naga **27.0.0** (install with
`cargo install naga-cli --version 27.0.0 --locked`). Pass `--naga /path/to/naga`
alongside `--glslang`. The generator derives separate image/sampler bindings
from the manifest: group 2 image bindings 0/2 and sampler bindings 1/3. It
extracts the atlas sampling wrapper from the shared skin source, leaving the
material catalogue, lighting and deformation authoritative. Optimized SPIR-V
is translated with coordinate conversion disabled, preserving canonical depth
and vertical orientation. Native Vulkan and WebGPU wrappers reverse `dFdy` to preserve the shared
material lighting in downward framebuffer coordinates. Compiler version mismatches fail the build; generating
without the translator removes previous WGSL artifacts.

`tools/shaders/prove_webgpu.mjs` creates every generated pipeline and renders
all skin materials, rest/posed rigs, derivatives, fur shells and depth occlusion.
It uses the same Playwright environment overrides as the WebGL proof and takes
an optional generated output directory. A local HTTP server on port 8784 is
required to provide a secure browser origin; `SHADER_PROOF_URL` overrides it. These translation and render
checks establish shader feasibility; platform default selection still requires
the application's hardware, lifecycle and performance validation. Metal library
production requires an Apple offline compiler; Direct3D and Metal hardware
validation remains a separate release gate.

## Runtime backend selection

`RendererBackend` stores stable family identifiers: `auto`, `sdl-gpu`,
`programmable-gl`, `webgpu`, `portable-sdl`, `software`, and `legacy-gl`.
Availability depends on the build, shader assets, device and presentation surface.
A family listed as unavailable cannot be selected.

Auto tries an ordered list (`GraphicContext::automaticRendererOrder()`) and starts
on the first family that is available and starts. Measurements choose the order,
not the renderer's architecture; the per-platform rows live in one table,
`PreferredRendererOrder` in `libs/gag/src/GraphicContextRenderer.cpp`:

| Platform | Auto order |
| --- | --- |
| Linux, Windows | programmable GL, Legacy GL (builds with OpenGL), SDL GPU, Portable SDL, Software |
| macOS | programmable GL, SDL GPU, Legacy GL, Portable SDL, Software |
| Android, iOS | SDL GPU (Vulkan, Metal), programmable GL (OpenGL ES), Portable SDL, Software |
| Browser | on a probed WebGL2 adapter: programmable GL, Legacy GL (builds with it), Software; otherwise Software. WebGPU stays opt-in |

The Linux row comes from a 1x perf build (master `d6488cac13`): the honeycomb-256
memory scenario (3,600 pan and zoom frames) and the fast wheel zoom of the motion
parity timing run.

| Renderer | Frame p50 | p99 | Max | Peak RSS | Peak GPU driver | Zoom median | Zoom p99 | Zoom max | Zoom frames over 16.7 ms |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| programmable GL | 5.4 ms | 26.9 ms | 64 ms | 539 MiB | 217 MiB | 5.0 ms | 19.9 ms | 50 ms | 11 |
| Legacy GL | 6.2 ms | 23.5 ms | 50 ms | 626 MiB | 233 MiB | 4.8 ms | 20.6 ms | 48 ms | 7 |
| SDL GPU | 6.0 ms | 33.3 ms | 146 ms | 604 MiB | 202 MiB | 6.1 ms | 27.2 ms | 163 ms | 45 |
| Portable SDL | 8.5 ms | 34.7 ms | 75 ms | 867 MiB | 177 MiB | 5.7 ms | 19.3 ms | 76 ms | 7 |

Programmable GL has the lowest median and resident memory with a tail close to
Legacy GL's, so it leads; SDL GPU's tail (p99, maximum and zoom frames over budget)
is the worst, so it follows both GL renderers. Windows runs the same GL renderers
and has no measurements of its own yet, so it shares the Linux row. The macOS row
comes from the same memory scenario on a Retina Mac (master `5524479f28`, a
3840x2160 drawable; footprint counts unified GPU memory):

| Renderer | Frame p50 | p99 | Max | Peak RSS | Peak footprint |
| --- | --- | --- | --- | --- | --- |
| programmable GL | 11.2 ms | 29.3 ms | 108 ms | 1346 MiB | 1562 MiB |
| SDL GPU (Metal) | 11.3 ms | 33.7 ms | 173 ms | 1163 MiB | 1419 MiB |
| Legacy GL | 11.5 ms | 27.6 ms | 163 ms | 1218 MiB | 1651 MiB |
| Portable SDL | 4.5 ms | 57.7 ms | 418 ms | 2347 MiB | 2730 MiB |

Programmable GL has the best tail, SDL GPU the smallest footprint, and Legacy GL
the largest footprint. Phones put SDL GPU first (user decision, 2026-11-17): it is the fully GPU
renderer on Vulkan and Metal, and the packages ship and verify its shaders
(Android requires the Vulkan set, iOS device builds the Metal set). Programmable
GL follows where the device or shaders are unavailable or the device is lost
(user decision, 2026-11-19): on a Galaxy A06 (Mali-G52) it plays far better than
Portable SDL. Portable SDL follows where neither starts, as in the iOS Simulator.
Revisit the rows when phone measurements land.

SDL GPU opts out of the Vulkan features SDL requires by default but the renderer
does not use (clip distances, first-instance indirect draws, anisotropic
sampling), so it starts on GPUs without them. It asks for depth clamping, which
keeps the world pipelines' depth behaviour on Metal and Direct3D 12, and on a
Vulkan device without it (Bifrost Mali, such as the Mali-G52) retries with depth
clipping instead.

In the browser, programmable GL leads on a probed WebGL2 adapter (user decision,
2026-11-17): Legacy GL there runs on Emscripten's fixed-function emulation, which
warns that it may not work, and builds without it (`web_legacy_gl=0`) previously
fell to Software even with WebGL2 available. There are no real-GPU browser
measurements yet; revisit with them.

The game asks for this order with the `PREFERDEVICE` startup flag, which
`startupScreenFlags()` adds without persisting it; tools and tests that create a
context without it keep the historical mapping (`USEGPU` to Legacy GL,
`PORTABLEGPU` to Portable SDL, otherwise Software, Portable SDL on mobile).

A family that is unavailable, or whose device, shaders or surface fail at startup,
falls through to the next; Software ends every order, so startup never leaves a
black window. Each skipped family adds `family: reason` to the switch status reason,
which the game logs at startup and benchmarks print as
`BACKEND requested=... active=... fallback_reason=...`. Auto falling through is not a
failed choice: the status phase stays idle and Settings shows the active renderer
without an "unavailable" notice. An explicit choice is tried first and, if it
cannot start, falls into Auto's order with a failed status. A device lost while
running falls to the next available family in the same order (WebGPU still falls to
programmable GL), not straight to Software.

Startup precedence is an explicit command-line option or browser query, then the
native `GLOB2_RENDERER` environment variable, persisted preference, and Auto. Existing
`gpu`, `webgl2`, `opengl` and `sdl` aliases remain accepted. Startup overrides are
session state. A new profile uses Auto. Settings without the `rendererBackend`
preference migrate their previous renderer flags: on desktop, where the old toggle
offered only Software (the default, no GPU flag) or OpenGL, both answers migrate to
Auto, since either meant "what this machine draws best with" rather than a family;
a saved `PORTABLEGPU` flag stays Portable SDL. Mobile keeps mapping `USEGPU` to
Legacy GL; the browser maps it to Automatic, since there it meant "draw with WebGL"
and Legacy GL is Emscripten's fixed-function emulation. Both map no flag to Software. A saved `rendererBackend`, including
`legacy-gl`, is an explicit choice and is kept.

Graphics settings request a transition through `requestBackendSwitch()`. The status
reports requested and active families, phase and failure reason. Requests coalesce
before a transition starts; an active transition finishes before another begins.
At a completed frame boundary the context retains the completed image, suspends
old-device capture admission, delivers ready captures, cancels remaining captures,
and replaces the presentation device and surface. Logical CPU assets survive; their
backend allocations and context generation are replaced. The first presentation on
the new device commits the preference. Failure restores the previous family, with
software as the final fallback; failure does not save the unsuccessful preference.
Rendering preference changes do not require restarting the game.

After the loading screen, the first screen's first 30 frames (the main menu
and its colony, or a first-run question in front of them) are drawn but not
shown (`GraphicContext::holdPresentation`, from `Application`): every renderer
streams terrain, HD artwork and atlases and settles while the last loading-screen
frame stays up, about half a second. A held frame is recorded, submitted and
completed like any other; only the final step differs: SDL GPU acquires no
swapchain image and waits for the frame's work (the swapchain wait normally
paces it), programmable GL skips its screen blit and swap, the portable renderer
flushes instead of presenting, and legacy OpenGL and the software renderer skip
their swap. `PRESENTATION_HOLD released ms=` logs when it ends.

Switch status also reports the completed transition duration. Context recovery
recreates the device and shader objects before restoring logical resources. A fatal
submission error schedules fallback at a presentation boundary; an unavailable
frame is never committed as a successful backend change.

The shared skin adapter records posed and rigged geometry into bounded atlas pages,
with explicit depth passes and premultiplied atlas composition. Paint and material
revisions remain immutable for submitted draws. Source, geometry and atlas caches
are rebuilt after switching or recovery.

HD sprite frames are block-compressed on SDL GPU, programmable GL and legacy GL,
in a quarter of the memory of RGBA. The 4x4 blocks of premultiplied colour step
slightly along the edges where a frame's base and team layers meet (about 2% of the
zoomed-in game screen differs from the portable renderer in the parity scenes).
`GLOB2_HD_BLOCK_COMPRESSION=0` draws them uncompressed instead. SDL GPU and programmable GL sample
block-compressed HD artwork where the device reports the format (`RenderDeviceCapabilities::bc3Textures`, `astc4x4Textures`;
programmable GL reports BC3 with `GL_EXT_texture_compression_s3tc` and ASTC 4x4 on
GL ES contexts with `GL_KHR_texture_compression_astc_ldr`, never in the browser). On entering the device,
asset workers prepare each HD chain as premultiplied BC3 (`PremultipliedBC3`,
etcpak) where the device samples BC3 (desktop GPUs), otherwise as premultiplied
ASTC 4x4 (`PremultipliedASTC4x4`, astcenc) where it samples ASTC (phones), and
otherwise not at all (`preferredDeviceBlockCompression`); the draw passes the
blocks with the RGBA levels, and the adapter stores a
compressed `TextureRevision` holding only the blocks of the levels at least 4x4,
charged at their size. The device uploads them unchanged into a
`BC3_RGBA_UNORM` or `ASTC_4x4_UNORM` texture, a quarter of the RGBA chain on the GPU and in the
restoration snapshot, and restores trimmed or lost textures from them. Workers
keep only the blocks of those levels, and a chain prepared earlier releases its
RGBA levels once the blocks reach the device; a device that
cannot sample them (or legacy OpenGL, which never uploads premultiplied blocks)
prepares the chain again from the surface pixels, as RGBA. Devices with neither
format sample RGBA. Only asset workers encode blocks: a chain the draw must
prepare itself (mutated content, or released pixels on a device without the
blocks' format) gets RGBA levels and no blocks, so the render thread never runs
an encoder and never waits for one; the HD loader keeps drawing the native
frame until the worker's chain arrives. `GLOB2_TEXTURE_COMPRESSION=none|bc3|astc`
restricts the formats SDL GPU and programmable GL report, for comparisons (`astc` exercises the
phone path on a desktop GPU that samples ASTC, such as Mesa's lavapipe). The class budgets stay
byte budgets, so the same budget holds about four times as much compressed artwork.

ASTC encoding uses the vendored astcenc
([inventory](../../third_party/README.md#astc-texture-compression)) with its
fastest preset trimmed further (one partition, one candidate, one refinement
pass: about 1.6 times faster than the plain preset for about 1.3 dB of PSNR at
about 50 dB on the shipped frames), the same premultiply rounding as the BC3
path, and one child codec context per worker thread sharing one table set. The
`ImageAssets` case `HD frame block encoding timing [artifacts][benchmark]`
measures every 32nd shipped HD frame's chain. On a desktop x86-64 core (perf
build) ASTC takes about 6 to 7 ms per frame (worst about 200 ms for the largest
padded chains), about 200 s for all 31,541 shipped frames on one thread, while
etcpak's BC3 takes about 0.6 ms per frame; that tenfold gap is why BC3 stays the
choice wherever it is sampled. Phones encode with NEON on slower cores, so expect
a few times the desktop time per frame on each asset worker; the HD loader
requests only visible frames and a short lookahead, and keeps drawing the native
frame meanwhile. The added code is about 205 KB on Android `arm64-v8a` and
`armeabi-v7a` and 230 KB on `x86_64` (stripped, `-O2`), and about 225 to 260 KB
on desktop x86-64 Linux and Windows (`-O3`). Coverage: native Linux, Windows and
macOS builds and Android and iOS compile the encoder (NEON on arm64, SSE4.2 on
Android x86_64, SSE2 on desktop x86, where BC3 is chosen anyway, scalar on
32-bit ARM); the browser build leaves it out.

Native texture restoration uses bounded upload slices when a full authored mip
chain cannot coexist with retained depth atlases and its transfer buffer. Upload-only
preparation can complete independently of a deferred draw frame; sampling waits for
all levels to finish. GPU terrain composition also checks aggregate memory headroom
before promotion and uses the CPU compositor under pressure. Individual cache class
budgets do not imply that all classes fit simultaneously. Torus captures use the requested
terrain density when the full tiled layout fits the terrain-target and aggregate
resource budgets. SDL GPU charges texture and depth allocations once over their
owned lifetime, including pending submissions and captures, instead of rebuilding
the complete texture lease set during each admission check. Its bookkeeping is
constant time per frame: running totals for live textures (per budget class),
geometry, coverage, retiring work and readbacks; cache admissions since the last
submission listed apart, so a submitted or failed frame marks or drops only
those; and dead CPU revisions retired 128 cache entries per call from a cursor
(all of them only when a budget class is full). Staging buffers remain
charged separately until their work completes; upload and readback staging buffers
and each frame's vertex buffer, up to 16 MiB, are then kept for reuse in
power-of-two classes (at most 24 MiB idle, a tenth of the total budget on phones),
and released colour textures for an allocation of the same size and format (at
most 32 MiB, an eighth on phones; every allocation is uploaded, cleared or copied
whole before it is sampled). Idle memory (`RenderTelemetry::idleBytes`) is kept only
while it fits beside the resources within the total budget, and an admission that
would fail and an explicit trim release it first. Creating a Vulkan buffer for
every frame's vertices, uploads and the benchmark's completion readback, and
freeing it, cost more CPU in driver calls than filling it; SDL's Vulkan backend
also submits a command buffer for every texture it creates, so in a game frame
a submission creates at most 96 new cache textures for packets drawn to the
screen: past that it keeps the uploads made so far and defers the frame, and
the previous frame shows while later frames create the rest (a resume after a
background trim, a first zoom out). Only frames their owner marks deferrable
(`allowDeviceFrameDeferral`, which `GameGUI::drawAll` calls; device captures
in game are asynchronous) are capped; a synchronous capture ends the mark
before it flushes, and other frames, such as menus, tests and complete draws,
are never capped. Offscreen composition, such as terrain pages, is exempt,
since a page left half composed would start again. Programmable GL caps a
deferrable frame the same way, at 96 new textures or 16 MiB of their uploads,
and keeps the same constant-time bookkeeping: sampled textures on intrusive
least-recently-used lists by budget class, and dead revisions retired 128 cache
entries per call from a cursor. Explicit trimming releases completed GPU copies that can be
restored from logical CPU snapshots, while preserving in-flight work, unfinished
uploads and externally owned rendered targets. Actual pressure can reduce density,
rather than a fixed low pixel cap.

On the presentation devices (SDL GPU, programmable GL, WebGPU), a surface's CPU
pixels and upload staging can go once the device adapter holds a restoration
snapshot of its current revision (`DrawableSurface::releaseDevicePixels`, the
counterpart of the portable renderer's release). The adapter then *retains* that
snapshot: it leaves the least-recently-used eviction list (an intrusive list, so
admission evicts in constant time) and draws with `blitCached` in the sampling it
was recorded with. Retained snapshots are capped at half of each budget class, so
ordinary admission always has room, and at most 16 surfaces are released a frame.
A device reset keeps them, and context recovery moves every snapshot to the
replacement adapter (`adoptDeviceTextures`), so neither reloads anything. A backend
switch or a failed recovery drops them; `Sprite::discardUnbackedHighResolution`
then drops the HD layers that had only those snapshots, which draw native frames
until their background reload publishes. HD base layers are released this way
after a direct draw (`Sprite::releaseDrawnHighResolution`: skin shadows through
`Sprite::drawFrame`), never when the CPU samples them: a released layer that
`Sprite::baseFrame` reaches answers with its native frame and queues a background
reload with pixels, while the released layer keeps drawing until it publishes.
Terrain materials and decor, which CPU page composition samples, keep their pixels.
Offscreen CPU exports cannot draw a device-released surface.

SDL GPU, programmable GL, the CPU restoration registry (`RenderResources`) and
the terrain page and torus planners share one `RenderBudget`
(`platformRenderBudget()`). Desktop keeps 512 MiB in total, General 256 MiB, SkinAtlas
64 MiB, TerrainPage 128 MiB, TerrainMask 32 MiB and 64 MiB of geometry. On-demand
renderers (every renderer but legacy OpenGL) keep uploaded HD sprite frames within
3/8 of General (`GraphicContext::deviceHighResolutionRoom`); 256 MiB holds all
3,680 HD unit frames (about 77 MiB block-compressed with mips), so every unit
animation stays HD however many the game has shown. Under the former 64 MiB the
first frames drawn filled the room and the rest (harvesting, warriors, explorers)
drew native until restart; `Sprite::highResolutionDrawCounts` counts HD draws and
native ones by reason (`FRAME_HD_SPRITES` in the torus benchmark). Phones
and tablets (`GAGCore::platformIsMobile`: the Android and iOS builds, and the
browser build on Android, iPhone or iPad) use 160 MiB, 48, 32, 64 and 16 MiB and
32 MiB of geometry: phones
share one memory pool between CPU and GPU, and the system kills a large
backgrounded game first. Each mobile class still holds twice its largest single
texture (the 22.4 MiB authored HD terrain block, 16 MiB skin atlas pages, sixteen
downsampled 1024x1024 terrain pages), since a class limit is also the largest
texture it admits; GPU terrain composition falls back to CPU pages when the view's
pages exceed the TerrainPage class. `GLOB2_RENDER_BUDGET=mobile` (or `desktop`)
overrides the choice to measure one platform's budget on another, and
`GLOB2_RENDER_BUDGET_TOTAL_MIB` replaces the total alone (the class limits stay),
for tests and memory experiments that need the combined limit to bind. WebGPU keeps its own limits.

`SDL_EVENT_LOW_MEMORY` and backgrounding (`SDL_EVENT_WILL_ENTER_BACKGROUND`,
`SDL_EVENT_DID_ENTER_BACKGROUND`) call `GraphicContext::trimPresentationMemory`,
which frees what can be rebuilt without a renderer reset: the device's completed
GPU copies (`trimRetiredResources`), the adapter's CPU restoration snapshots whose
owners keep their pixels (`trimDeviceTextures`, which walks the adapter's
least-recently-used list from its oldest end, so each step is constant time;
retained snapshots are not on it), terrain pages off the view
(through `PresentationTrimSubscription`) and the retired HD frames of streamed
banks. Backgrounded nothing is drawn, so all of it goes at once. In the
foreground, presentation devices trim instead of resetting, which would upload
the whole view again in one frame: at most 64 snapshots a frame, only those not
drawn in the last two frames, eight idle terrain pages a frame and 64 retired
frames a poll, so the view keeps drawing from what it holds. Erased snapshots and
pages are re-created from their owners' pixels or recomposed in the background
when next drawn. Other renderers still reset on low memory, and trim terrain
pages and retired frames when backgrounded. `GLOB2_BENCH_TRIM_AT=<frame>` (or
`memory_scenario.py --trim-at`) sends a low-memory trim during a benchmark.

The command adapter preserves adjacent painter order and flushes at 4096 commands.
Cloud alpha maps retain complete cell fans in painter order, submitting at most
4092 vertices per chunk instead of allocating a packet for each cell (the
portable SDL renderer uses its lattice texture instead). Legacy GL
float alpha maps also group complete fans into bounded submissions, retaining
their integer centers and existing clamped colors. Software nearest blits use
direct coordinate offsets on unscaled axes; scaled axes retain the original
sampling calculation and all blend, modulation and color-key rules. SDL GPU
keeps compatible primitive draws in the same color pass and merges consecutive
vertex ranges only when their effective state matches. Barriers, clears, world
geometry, terrain composition and target changes end that pass.
Consecutive unreduced terrain cells on the same SDL GPU page share a terrain
pass, with each cell retaining its own uniforms, samplers, viewport and scissor.
Other packet kinds and attachment changes close it. Scratch composition and
reduction remain separate passes so the reduction samples completed writes.
GL primitive state reuse is local to one bounded vertex-upload segment; barriers,
other passes and the next segment reset it rather than assuming global driver state.
Immutable texture revisions retain the pixels that queued draws referenced, even
when a surface changes or is deleted. The device retires allocations after submission
completion. Budget deferral retains logical target handles so sliced uploads can
finish. Aborted frames invalidate composed skin and terrain readiness separately
from the device generation; fatal device failures still enter graphics recovery.
Resource caches and devices belong to presentation, outside saves,
orders, synchronized randomness and simulation checksums. Capability queries control
filtering, torus, cloud layers, alpha-map coverage and live material rendering rather
than the legacy `USEGPU` flag. CPU offscreen exports temporarily expose software
capabilities, even when the window uses a GPU device. Graphics settings are also
available from the in-game and editor menus; the running engine or editor is retained
while settings are open.

The browser WebGPU adapter (`platforms/web/webgpu-device.js` and `WebGPUDevice.cpp`)
prepares adapters, devices and shader pipelines asynchronously before changing
presentation ownership. `glob2_webgpu_prepare(shaderDirectory)` and
`glob2_webgpu_poll()` return pending (0), ready (1), or failed (-1), with
`glob2_webgpu_error()` supplying the reason. The directory contains generated
WGSL and has `metadata.json` in its parent. A synchronous main-thread helper
reads the preloaded filesystem and transfers bounded, owned shader text to the
presentation worker; that worker creates the GPU device and pipelines. Threaded
builds do not read the worker-local JavaScript filesystem for shaders. Factory
creation then attaches the
managed canvas synchronously. The coordinator handles fallback and persisted
preferences; the adapter reports device loss through the same polling API.

Within one WebGPU submission chunk, identical immutable uniform bytes and
bindings share buffers and descriptors. Uniform comparison storage is bounded
to 512 entries and 1 MiB; binding reuse is bounded to 4096 groups. Buffers remain
owned until completion, and allocations are never rewritten for later chunks.
Texture views follow physical allocation lifetime; presentation bindings reset
with output resize or device recreation. Adjacent draws to the same attachment
share a render pass and skip unchanged binding and raster state. Clears, barriers,
terrain composition, target changes and submission-chunk boundaries end that pass;
all draw calls and their painter order remain intact.

Ordered packets cross Wasm in fixed-width packed records. Shader vertex layouts
come from generated metadata; skin and posed-rig uniforms use the same generated
std140 offsets as the native backends. Uploads copy current Wasm views into ordinary
owned arrays, supporting shared memory and memory growth. Logical resource
revisions use independent device allocations; submitted frames retain snapshots
until queue completion. Discard fences also cover uploads prepared before packet
submission; backend recreation shares the adapter’s issued completion timeline.
Texture/depth allocations are bounded to 512 MiB in total on desktop, with class
budgets of General 256 MiB, SkinAtlas 64 MiB, TerrainPage 128 MiB, and TerrainMask 32 MiB;
persistent geometry to 32 MiB. Terrain imagery the CPU composes (view pages,
overview pages and animated tiles, `TerrainVisual::Surface`) is admitted in the
TerrainPage class on every device, matching the page cache's own 128 MiB plan;
in the General class they would compete with HD sprites, and on phones a view's
high-density pages alone could exceed its 48 MiB, deferring every frame. Eight pending presentation frames and three capture
requests bound GPU admission. Recorder flushes within one presentation share a
fence and admission slot; all transient buffers also count toward that
aggregate device limit. Capture maps complete asynchronously in admission
order; recording timestamps and context remain in the originating C++ request.
Submission, presentation and capture CPU times and completed capture latency
are accumulated in shared telemetry. Browser screenshots must use polled capture or CPU export rather than blocking
Wasm waiting for a promise.

`tools/shaders/prove_webgpu_device.mjs` exercises the packed adapter with owned
shared-memory uploads, offscreen rendering, indexed geometry, queue bounds,
capture ordering and device recovery. Its Playwright overrides and secure-origin
configuration match the shader proof. SwiftShader canvas testing requires Vulkan
and matching ANGLE configuration; shader-only tests do not establish presentation
support. Unit tests additionally inject asynchronous completion and loss without
requiring a GPU. Application switch tests remain required for full feature and
browser acceptance.

The packed C++ wrapper has a separate regression proof. Build it with
`tools/shaders/build_webgpu_cpp_proof.py`, supplying `--emxx` from the pinned
Emscripten SDK, the browser SDL prefix with `--sdl-prefix`, and generated shaders
with `--shader-directory`. Serve its output with `platforms/web/serve.py`, then run
`NODE_PATH=platforms/web/node_modules node tools/shaders/prove_webgpu_cpp.cjs URL`.
It checks all four textured skin atlas offsets, straight and premultiplied
render-target sampling, and pressure eviction with live restoration sources and
an unpresented frame lease. Exercising only JavaScript records would miss C++
validation and packing defects.

The programmable GL C++ lifecycle proof uses
`tools/shaders/build_programmable_gl_cpp_proof.py` with the same pinned inputs,
then `tools/shaders/prove_programmable_gl_cpp.cjs URL`. It recreates WebGL2
contexts around SDL software surfaces and checks weighted terrain composition,
world geometry interleaving, asynchronous capture, trimming and resize. This
isolates device behavior; full application switching and torus tests are still
required for browser acceptance.

For cross-API shader conformance, set `SHADER_PROOF_PIXELS=1` on both shader
proof commands, then run `python3 tools/shaders/compare_pixels.py` with the output
directory. The comparison normalizes row orientation and permits at most one
channel value (1/255) for SwiftShader translation rounding. This focused shader
limit does not replace platform scene tolerances or release measurements.

GPU memory pressure defers the whole presentation instead of displaying partial
recorder chunks. A retained main-output texture restores the previous image;
queued work is fenced, while already admitted captures keep their original
metadata and buffers. The renderer reports `RenderAdmissionDeferred` and
implements `discardFrame()` for the shared adapter. The coordinator invalidates
skin and torus promotions for an interrupted presentation. Persistent blit
buffers make normal presentation and rollback independent of transient buffer
admission. Resource requests individually larger than their static limits remain
errors. Retained output, pending captures and presentation buffers count toward
the device's aggregate budget.

Torus map captures use bounded offscreen regions across presentation turns.
The capture owns its immutable presentation frame and freezes its animation,
unit motion, team, options and sampling scale until all regions are complete.
Each turn can submit up to four regions, yielding after a soft four-millisecond
CPU budget or admission pressure. Region pixel and source limits remain bounded.
Final overlays start with a fresh source budget after a turn that submits several
regions; completed region cursors survive this yield. A completed atlas remains
owned by the pending presentation until its world submission is accepted.
Deferred world work reuses that atlas without recomposing overlays or advancing
preview opacity again.
Region culling preserves the complete target coordinate system, and disjoint
physical scissors prevent repeated blending at fractional zoom. Skin preview
ownership is borrowed only within a draw call and restored before yielding.
The complete main image remains visible during preparation; capture admission
and main presentation pause, while simulation, audio and input continue.
Source presentation phases advance on every unpaused draw turn; only the
capture's pixels and phases stay frozen, so holds do not slow animation clocks.
An intentional hold fences completed offscreen work without invalidating the
healthy graphics generation. Admission pressure retries unpublished target
content; a fatal device failure invalidates the generation.
Map or team changes cancel the pending capture; it never publishes a partial
atlas. The visual pause lasts several presentation turns and grows with capture
size, so playback feel and transition latency require hardware review.

Terrain composition uses authoritative integer shaders derived from the CPU
compositor contract. The producer retains CPU-generated coverage masks with an
8 MiB lookup cache. A global 40 MiB limit covers owned coverage objects and sample
bytes, including leases held by queued submissions and retiring devices; bounded
lookup and shared-ownership bookkeeping is accounted separately. Exhausted
admission falls back to CPU composition. GPU coverage allocations retain their
independent 32 MiB budget. `terrain-compose` reads four straight RGBA materials,
RGBA16_UINT coverage weights/margin and R8_UINT palette selectors. Its 352-byte
fragment block holds composition parameters, per-material seam and surf constants,
and the cell's world origin and animation time for surf.
`terrain-reduce` performs the reference alpha-weighted box reduction for divisors
1, 2, 4, 8, 16 and 32 with a 16-byte parameter block. Straight intermediates
retain packed-ARGB wrapping; final premultiplied targets apply the same byte
rounding as CPU uploads before hardware filtering. WebGPU coverage and scratch
caches each admit at most 32 MiB and count against the device's aggregate budget.
Changes between CPU and GPU composition retain reusable CPU terrain pages at
other densities within the existing cache and texture budgets. Device-composed
pages are discarded when their targets are released; graphics generation or
resource registry changes still invalidate the cache.
GPU page admission reserves new target storage and the bounded coverage lease
allowance alongside retained CPU pages, retiring idle storage before composition.
`tools/shaders/prove_terrain_webgpu.mjs` checks exact integer pixels, material
presence, alpha, each reduction divisor and premultiplied output. The packed-device
proof additionally checks owned shared-memory coverage uploads, target gutters,
ordered capture and cache cleanup.

Ordinary sprite frames share CPU atlas pages across device backends. Pages use
one-pixel gutters, child lifetime/revision stamps, a conservative 2048-pixel
maximum and a shared 64 MiB CPU ownership budget. Mutations publish a new page;
queued draw packets retain their original uploaded pixels. HD frames retain
standalone authored mip chains.

Authored HD texture mip chains retain CPU-generated RGBA levels, including padded
level zero. The WebGPU adapter allocates the complete chain, uploads owned copies
of each level, and charges every level to resource budgets. Linear packets use
trilinear sampling; nearest packets use level zero. Premultiplied source bytes
are uploaded without a second alpha multiplication. No backend generates mip
pixels from the base level.

Cross-host shader packages use a verified prebuilt input rather than rerunning
Linux translators on Apple hosts. Generate the complete set at the exact source
revision, then import it with `tools/shaders/generate.py --output OUTPUT
--prebuilt INPUT`. Import verifies the source fingerprint, manifest layouts,
generated header, every artifact hash and entrypoint, authoritative GL sources,
and translated Metal source receipts before replacing the output atomically.
The build supports `GLOB2_RENDER_SHADER_INPUT=INPUT` and never invokes local
translators on this route. For Apple offline compilation, additionally supply
`GLOB2_METAL_SDK` and `GLOB2_METAL_TOOL_LOCK`; the verified translated sources are
retained through preparation and compiled libraries are reused only while the
input and tool lock remain identical. Source, layout or artifact changes reject
reuse. Runtime packaging excludes translated HLSL/MSL sources, AIR and headers.
## Texture preparation

Generated or edited power-of-two HD surfaces use an integer GPU reduction pass
on WebGL2 and desktop OpenGL contexts with integer shader and framebuffer support
(OpenGL 3+ or `GL_EXT_gpu_shader4`). Each mip texel retains the
CPU filter's alpha-weighted color and integer rounding; authored, worker-prepared
mip chains keep their existing upload and compression path. Unsupported contexts
(including older contexts without integer shaders), non-power-of-two surfaces and failed pipeline
creation retain CPU preparation. Native scratch textures and shaders are released
before their owning context is destroyed.

Unless `GLOB2_HD_BLOCK_COMPRESSION=0` (see above), asset workers also compress each authored level into 4x4
blocks with the vendored etcpak encoders (`AssetImage::compressBlocks`,
[inventory](../../third_party/README.md#texture-block-compression)), so the
graphics thread uploads blocks and the driver never encodes. Desktop legacy
OpenGL picks the format at context creation: BC3 where the device has S3TC, as
desktop GPUs do, otherwise ETC2 with EAC alpha where the context promises the
OpenGL ES 3 formats (`GL_ARB_ES3_compatibility`), which GPUs without S3TC decode
natively; both take 16 bytes for 16 pixels, a quarter of RGBA. On x86 processors
with SSE4.1 the workers use a second build of the encoders compiled for it. A level whose
blocks are in another format than the context's (artwork decoded before the
context existed) uploads uncompressed. `GLOB2_TEXTURE_COMPRESSION=bc3|etc2|none`
overrides the choice for tests. The browser path and the device renderers still
upload RGBA, so ETC2 on phones awaits a renderer that takes compressed blocks.
On little-endian desktop OpenGL, the CPU fallback for tightly packed,
power-of-two ARGB surfaces uploads the original pixels directly and reuses
one scratch buffer for the smaller levels.

The portable SDL renderer used on mobile samples standalone HD frames without
mip chains, so those requests decode only base pixels. In-flight HD requests retain
their original mip policy across backend changes. A switch to a mip-capable
backend requests the missing chain independently; a downgrade can consume the
existing chain. Supplied atlas levels and standalone generated chains remain
separate requests. Backend changes also schedule a bounded reload for already
published images when mip preparation or CPU restoration requirements change.
The previous artwork remains visible until publication, and failed upgrades keep
a valid existing layer. Its linear textures need
premultiplied alpha: accelerated SDL renderers prepare them by blending a
straight-alpha staging texture onto a transparent target. The renderer keeps up
to four staging textures, one per texture size, since pages, HD sprite frames and
animated tiles of different sizes upload in the same frame, and releases each
after 300 presented frames unused; compatible cached
textures are updated in place when pixels change. SDL's per-target viewport, clip, scale and logical presentation
state is restored before drawing resumes. A bounded 256×256 startup probe checks
all byte colour/alpha pairs against the CPU integer result. Drivers whose blend
units differ retain CPU conversion and log their conformance mismatch count.
Software renderers and unsupported target/blend combinations also keep CPU
conversion. Region updates (`RenderBackend::prepareTextureRegions`, used by
terrain pages that rewrote a few cells) patch only the dirty rectangles of a
texture holding the immediately preceding revision, with the same bytes a whole
upload stores: the CPU path premultiplies each rectangle with the same integer
rounding, and GPU-prepared textures clear and re-blend just those rectangles from
the staging texture. A region update keeps the sampling mode the texture's last
draw chose, and pending batched draws are submitted before it. Preparation uses the existing
SDL renderer APIs across OpenGL ES, Metal and other accelerated SDL backends.

For a native preparation comparison, set
`GLOB2_DISABLE_GPU_TEXTURE_PREPARATION=1` to retain CPU mip generation and SDL
premultiplication. An unset or empty value enables supported GPU preparation.
These paths change presentation work only; simulation and saved state are unchanged.

### Bounded renderer command core

`RenderFrame` records ordered packets with explicit clips, transforms, targets, blending and depth. Adjacent compatible packets batch without reordering. The recorder flushes at 4096 commands and bounds geometry storage; a rejected submission abandons the incomplete frame rather than exposing partial output.

`RenderResources` supplies generation-checked logical texture and buffer handles, immutable content revisions, retained restoration pixels and per-class budgets. Mutation preserves the content referenced by older submissions. Deletion retires a logical identity while its snapshots remain owned by submitted work. Backends maintain their own allocation generations and upload revisions.

`RenderDevice` owns submission, completion, capture and presentation. The software device and existing drawing adapter exercise the contract without hardware. Capture tickets retain frame identity, recording generation, dimensions, timestamps and semantic context; cancellation and telemetry stay bounded. A fake asynchronous device tests admission, retained revisions, deletion, cancellation and recovery.

Shutdown releases sprite and font consumers before the graphics context. File and
string services remain alive until the context has delivered completed captures
and retired its presentation surface. A completed asynchronous screenshot can
still need these services while the context is being destroyed.

`RendererSelection` defines stable backend-family identifiers, aliases, capabilities and startup precedence. The application coordinator applies this policy through the live-switch transaction described above. Auto's per-platform order is described in [Runtime backend selection](#runtime-backend-selection), and renderer selection stays outside simulation state.

### CPU presentation batches

The graphics owner can synchronously dispatch bounded CPU terrain composition
and software raster tiles through `PresentationWork` on the existing compute
executor. This is independent of resumable scene preparation and does not add
an asynchronous frame queue or camera cancellation pipeline. Simulation joins
never pump graphics work. Draw ordering, cache installation, SDL/backend access,
frame capture and presentation remain on the graphics owner. See
[bounded CPU presentation work](software-rendering.md#bounded-cpu-presentation-work)
for scheduling, ownership, admission, serial defaults and development controls.
