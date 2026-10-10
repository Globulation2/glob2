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

- The engine admits presentation only when its bounded producer has capacity,
  declares requirements with AI and gradients, and publishes one shared capture.
  `SceneExtractor::prepare` accepts an immutable handle and request, never live
  simulation objects. Preparation runs on the shared compute executor. A complete
  frame is published through `SceneBuffer`; drawing keeps the previous complete
  frame while work is in flight. Simulation barriers do not join presentation.
  Interactive sessions open the next completed world's shared read boundary
  before waiting for tick pacing. AI polling and order delivery stay at their
  existing deadlines and reuse that publication; presentation does not wait a
  whole tick interval before starting. The native mailbox admits at most one
  preparation per client request and can replace an unconsumed completed frame.
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
  published union rather than recapturing from the live game. A bounded diagnostic
  batch owns its fields and snapshot independently; PNG preparation and export do
  not park the simulation. Occupied publication slots stop further diagnostic
  admission, with skipped or superseded output reported explicitly.
- The animated menu colony includes admitted presentation requirements in its AI
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
  the engine publishes the completed world's shared read boundary with the union
  of AI, gradient and admitted presentation requirements. Presentation preparation
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
