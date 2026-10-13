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
be invalidated selectively when its source texture or array page changes; unrelated
uploads must preserve reusable entries. A mutation serial rejects interrupted
captures but does not globally clear the cache. These are
presentation caches owned by the graphics context, released while that context
is current; they are neither saved nor consulted by simulation code.

High-resolution sprite layers are shared by gameplay, editors and background
scenes. Changing screens preserves published layers and pending reload work.
Preference changes, a new asset source generation or a different pack directory
start a reload; unchanged requests do not. Explicit synchronous reloads used by
tools still refresh unchanged artwork. Disabling the preference releases the
optional layers through asset polling, rather than through screen destruction.

Terrain uses the shared CPU material compositor and bounded 16 by 16 cell pages
on software and GPU backends; see [terrain materials](../assets/terrain-materials.md).
Fully revealed resources use canonical map rows, with sorted source-tile indices
selecting the contiguous visible vertex range. Translation places a canonical row
at its wrapped-map position, so camera panning does not change its vertices.
Each resource entry compares its exact frame/visibility vector before reuse.
Partial-discovery resources keep the ordinary drawing path. The resource geometry budget
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

Colony-skin materials are declared once in `libgag/shaders/skin-materials.json`
(ids, keys, display names, picker groups, which materials grow fur shells, the
shell count and the fur length and depth bias every renderer uses) and shaded
once in `libgag/shaders/skin-material.glsl`. `scons/skin_materials.py`
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
dispatch line to the GLSL, mirror the entry in `platform/packages/protocol/src/skins.ts`,
then run `test/build_system/test_skin_materials.py`, the `SkinMesh` display
suite with `GLOB2_UPDATE_SKIN_FINGERPRINTS=1` once (it rewrites
`test/fixtures/skins/material-fingerprints.json` and writes contact sheets under
`artifacts/skins/materials/`) and review the sheets. While iterating on the
GLSL, `tools/skins/material_spheres.mjs` (run from `platform/apps/web`) renders
every material on a sphere through headless Chromium in seconds, and the
`skins-materials.spec.ts` e2e captures each material on the worker at studio
resolution. Any shader edit changes the
sprite render revision and re-bakes every published skin.


## Scene renderer

Drawing reads an immutable `PresentationFrame` (`src/render/scene/`). It retains
the shared world snapshot, view request, captured timing and derived presentation
results. Unit, building, team, relationship, entity-index, effect and map data are
borrowed from standard snapshot components. There is no second entity extraction
or map-array capture. `Game::drawMap` requires an explicitly supplied frame;
`Game::drawSceneMap` accepts it directly.

- Interactive engines capture the standard simulation preset at each read
  boundary. When the bounded producer admits presentation preparation, capture
  also includes its session, effects and statistics payloads and any requested
  history, telemetry, entity diagnostics or annotations. Diagnostic requests can
  independently add the payloads they need. AI, map jobs and presentation
  borrow the same captured data through standard views.
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
  records. Selected panels borrow those records and immutable catalogs; presentation
  calculations and formatting belong in frame storage. Catalog definitions are frozen
  once per revision, so a later owner reconfiguration cannot change a retained frame.
  AI telemetry, debug gradients, failure histories, and statistics histories are requested
  separately from ordinary frames. Statistics histories share immutable samples
  between observations until the next history revision; the compact HUD sample
  ring remains independent. Alliance controls read session player identities and
  team masks; objective and hint dialogs retain immutable narrative payloads that
  are reused until mutation. Opening these dialogs does not park simulation.
  Shift-click diagnostic dumps select the displayed generation and serialize it
  only if that same entity still exists at the explicit owner boundary.
- The editor and standalone tools explicitly obtain an owner-boundary snapshot before
  preparing a frame. Offline map images and diagnostic PNGs use the same drawing
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

Simulation speed, save/replay formats and network contracts are independent of
this preference. Headless runs, offline image exports and renderer benchmarks do
not opt into the interactive limiter. Recording output FPS remains independent;
recordings cannot gain new visual detail from frames the application did not draw.

### Simulation thread

Interactive sessions run the simulation on its own thread (`src/engine/sim/SimulationRunner.h`)
on native platforms; there is no setting. Both browser runtimes, and any platform where
creating the simulation thread fails, run the same session serially (`Engine::stepSession`), which
also remains the headless default and the equivalence reference.

- The simulation thread paces itself with the speed presets and runs ticks
  (`Engine::simulationStep`: orders, network, `Game::syncStep`). Before pacing,
  the engine publishes the completed world's standard simulation snapshot,
  adding presentation payloads only when preparation is admitted (or diagnostics
  require them). Presentation preparation
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

The experimental **Smooth unit motion** graphics setting (`Settings::unitInterpolation`,
off by default) draws units between ticks, so threaded play at display rate uses all
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
  on. `Game::drawMap` updates it once per frame from the drawn Scene, and resets it while
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
  everything drawn for them: bars and status pips (through the `opacity` parameter of
  `Game::anchorBars`, `drawStatusPip`, `drawPointBar` and `drawHealthBar`; queued bars take
  it from their anchor), selection circles, the level-up number and magic effect, the
  carried resource and the accessibility label. A fading unit's sprite is translucent and so
  leaves the unit sprite batch; only the few units at the edge of the fog do.
- Still binary: undiscovered black, the minimap, mouse and touch picking, bullets and
  explosions, and remembered enemy buildings. Bullets near a fading unit therefore vanish
  at the fog swap.


See [adaptive zoom detail](zoom-detail.md).

## Clouds, bars and resource batches

Cloud patches in the flat game and editor views use a coarser, world-anchored
lattice when zooming out to half size or smaller. Patches retain at most their configured
1:1 size on screen; normal zoom keeps the original sampling. The field and animation
time remain unchanged, but distant clouds have less fine detail. The torus view
retains its separate sampling budget.

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

`libgag/shaders/renderer-manifest.json` declares skin/rig, world/cloud, smooth-point and 2D primitive
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
Xcode pin in `mobile/toolchain.json`; `--developer-dir` selects a bundle without
changing the system selection. This separate step compiles MSL to AIR, links
`.metallib`, checks the library magic and compiled function name, and records
source and library hashes with the entrypoint. Failed or incomplete builds leave
Metal unavailable. A verified Apple tool lock and actual SDK compilation are
required before producing release libraries; mock tests verify invocation and
receipt handling only.


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
A family listed as unavailable cannot be selected. New devices remain experimental
until their full feature and hardware release gates pass; Auto retains the existing
platform policy.

Startup precedence is an explicit command-line option or browser query, then the
native `GLOB2_RENDERER` environment variable, persisted preference, and Auto. Existing
`gpu`, `webgl2`, `opengl` and `sdl` aliases remain accepted. Startup overrides are
session state. Settings without the new preference migrate their previous renderer
flags; a new profile uses Auto.

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

Switch status also reports the completed transition duration. Context recovery
recreates the device and shader objects before restoring logical resources. A fatal
submission error schedules fallback at a presentation boundary; an unavailable
frame is never committed as a successful backend change.

The shared skin adapter records posed and rigged geometry into bounded atlas pages,
with explicit depth passes and premultiplied atlas composition. Paint and material
revisions remain immutable for submitted draws. Source, geometry and atlas caches
are rebuilt after switching or recovery.

Native texture restoration uses bounded upload slices when a full authored mip
chain cannot coexist with retained depth atlases and its transfer buffer. Upload-only
preparation can complete independently of a deferred draw frame; sampling waits for
all levels to finish. GPU terrain composition also checks aggregate memory headroom
before promotion and uses the CPU compositor under pressure. Individual cache class
budgets do not imply that all classes fit simultaneously. Torus captures use the requested
terrain density when the full tiled layout fits the terrain-target and aggregate
resource budgets. SDL GPU charges texture and depth allocations once over their
owned lifetime, including pending submissions and captures, instead of rebuilding
the complete texture lease set during each admission check. Staging buffers remain
charged separately until their work completes. Explicit trimming releases completed GPU copies that can be
restored from logical CPU snapshots, while preserving in-flight work, unfinished
uploads and externally owned rendered targets. Actual pressure can reduce density,
rather than a fixed low pixel cap.

The command adapter preserves adjacent painter order and flushes at 4096 commands.
Cloud alpha maps retain complete cell fans in painter order, submitting at most
4092 vertices per chunk instead of allocating a packet for each cell. Legacy GL
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

The browser WebGPU adapter (`browser/webgpu-device.js` and `WebGPUDevice.cpp`)
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
Texture/depth allocations are bounded to 256 MiB in total, with General and
SkinAtlas class budgets of 64 MiB, TerrainPage 128 MiB, and TerrainMask 32 MiB;
persistent geometry to 32 MiB. Eight pending presentation frames and three capture
requests bound GPU admission. Recorder flushes within one presentation share a
fence and admission slot; all transient buffers also count toward the 256 MiB
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
with `--shader-directory`. Serve its output with `browser/serve.py`, then run
`NODE_PATH=browser/node_modules node tools/shaders/prove_webgpu_cpp.cjs URL`.
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
regions; completed region cursors survive this yield.
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
RGBA16_UINT coverage weights/margin and R8_UINT palette selectors. Its 144-byte
fragment block holds composition parameters and per-material seam constants.
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
straight-alpha staging texture onto a transparent target. The staging texture is
shared by the renderer, and compatible cached textures are updated in place when
pixels change. SDL's per-target viewport, clip, scale and logical presentation
state is restored before drawing resumes. A bounded 256×256 startup probe checks
all byte colour/alpha pairs against the CPU integer result. Drivers whose blend
units differ retain CPU conversion and log their conformance mismatch count.
Software renderers and unsupported target/blend combinations also keep CPU
conversion. Preparation uses the existing
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

`RendererSelection` defines stable backend-family identifiers, aliases, capabilities and startup precedence. The application coordinator applies this policy through the live-switch transaction described above. Auto defaults remain unchanged, and renderer selection stays outside simulation state.

### CPU presentation batches

The graphics owner can synchronously dispatch bounded CPU terrain composition
and software raster tiles through `PresentationWork` on the existing compute
executor. This is independent of resumable scene preparation and does not add
an asynchronous frame queue or camera cancellation pipeline. Simulation joins
never pump graphics work. Draw ordering, cache installation, SDL/backend access,
frame capture and presentation remain on the graphics owner. See
[bounded CPU presentation work](software-rendering.md#bounded-cpu-presentation-work)
for scheduling, ownership, admission, serial defaults and development controls.
