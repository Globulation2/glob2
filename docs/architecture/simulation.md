# Simulation state and field computation

Current simulation state, deterministic randomness, terrain rules and scheduled field publication. For change verification, use [simulation verification](../development/simulation-verification.md).

## Domain state

A `Team` is a colony; a `Player` controls a team, and several players can share one.
For timing and scheduling, start with `src/game/Game_sync.cpp` and `src/engine/EngineRun.cpp`.

## Deterministic randomness

- Units and buildings own saved private `EntityRandom` PCG32 streams (format 151).
  Entity decisions use `entityRandom.nextU32()`; randomized map pathfinding takes
  the moving unit's stream explicitly. Initialization salts the game seed with
  kind, full GID and slot generation without consuming another stream. Upgrades,
  repairs and ownership conversion preserve progress; reused slots get fresh streams.
  Both state words participate in entity checksums and snapshot records. Older
  saves initialize missing streams once and then run only the new behavior.
  Starting maps are reseeded with the final match header; saved resumes retain state.
  Team has no independent draws or stream. `python3 test/check_entity_random.py`
  checks that production code cannot implicitly draw `syncRand()` or process-global `rand()`.
- Format 152 adds separate saved PCG32 streams owned by the map for growth-job
  seeds, immediate reference growth, resource stocks, placement and smoothing.
  Each SGSL story has its own stream, salted by its source-order index. All streams
  are seeded directly with the match/request seed and fixed domains; no parent
  stream is consumed. Both words participate in ordinary simulation checksums.
  Older saves initialize missing streams once. Fresh match headers reset template
  progress; resumed saves retain it. Resource-growth jobs retain a private MT19937
  scan stream, with separate PCG32 decisions per source tile. Additional decisions
  at one source cannot change another source's draws or the scan schedule.
- AI controllers already own saved MT19937 streams derived from game seed and
  player number. Helpers now take that stream explicitly, including placement,
  shuffling and strategy selection; no thread-local binding chooses their owner.
  Each controller still permits at most one decision in flight.
- Generation mutates only its target map's streams, and scored trial maps initialize
  independently. Test-game map selection and each AI seat use distinct domains.
  The historical `Game::syncRandom` record is retained for save/test diagnostics
  but has no production consumers or per-tick advancement. The source contract
  rejects implicit RNG calls throughout production code; legacy utility bindings
  remain only to support older test diagnostics.
- Random consumption is isolated, but map mutations, interactions and execution
  order remain sequential. Keep iteration and tie breaking deterministic; never
  depend on pointer ordering, hash-table iteration or thread scheduling. These changes alter resource ecology, map placement and
  legacy summons, in addition to the entity trajectory changes in format 151.
  Gameplay review remains necessary.
- Keep rendering, particles, animation and other presentation-only randomness off
  `syncRand()`. Use a presentation-owned generator such as `GameGUI::effectsRandom`,
  so visual effects can change, run at any frame rate or move to another thread
  without consuming simulation draws.
## Client boundary

- Simulation/client boundary (`src/engine/sim/`). Simulation code must not call `GameGUI`;
  it talks to the client through value channels. `GameGUI` owns the event/request
  queues; `Game` owns the stable script endpoint (inactive without a GUI):
  - `ClientEvents`: lossless queue of notices the simulation publishes (team
    `GameEvent`s, chat, voice, marks, pause, ghost removal, building removal, unit
    conversion, executed orders, script presentation) plus a per-tick latest-value pulse
    (`Team::wasRecentEvent` for every team). `Game::executeOrderAndNotify` publishes
    the order effects; `GameGUI::consumeClientEvents` applies them after each order,
    after each engine tick, and at the start of `step` and `drawAll`.
  - `ClientCommandSink`: the presentation commands map scripts issue (building and
    flag choices, GUI elements, highlights, Space swallowing, script text). SGSL,
    USL and JavaScript map scripts call it instead of `GameGUI`. Its two read
    methods are legacy USL queries; do not add more. `Game::scriptClient` forwards
    directly during serial execution. At threaded startup it receives the current
    ordered choices and aliases, then updates its own enablement mirror and enqueues
    commands. Queries never wait for GUI consumption. USL retains this same stable
    endpoint across mode changes.
  - `ClientRequests`: a latest-value `ClientView` (viewport, observed building,
    overlay, debug layers) and a lossless command queue (the SGSL Space
    acknowledgement). `Game::applyClientRequests` applies them at the start of
    `Game::syncStep`; only the observed building records
    `Building::unitsFailingByReason`.

  Client code holds entities as `BuildingRef`/`UnitRef` (gid plus `scriptIdentity`)
  and resolves them through `Game::resolveBuilding`/`resolveUnit` at each use; do not
  keep `Building*`/`Unit*` across ticks in client code. Queue and pulse access are
  synchronized. Draining swaps out one batch under a short lock, then delivers it
  without holding the lock; notices published during delivery wait for the next
  drain. Channel resets require a lifecycle boundary with producers stopped.
- Team capacity is `Team::MAX_COUNT` (16), shared by colonies and controller slots.
  `MAX_COUNT_ON_DISK` (32) is the fixed GameHeader player/alliance layout, not a
  selectable match size. Team masks are 32-bit; packed growth coverage requires
  three masks to fit a 64-bit word (at most 21 teams in that representation).
  Unit/building identifiers must also fit below the 16-bit empty-entity sentinel.
  Team iterators must use the live match count; a full array has no null end slot.
  Format 127 counts Maxima opponent records and script-generation team slots;
  older formats retain their historical 12-slot layouts. Text header alliances use
  indexed slots from format 127; binary header bytes stay unchanged. Custom-game
  preferences version 4 counts colony records and still reads the twelve records
  written by versions 1–3. The building-generation
  plane must be remapped when loading old saves. Never substitute a live capacity
  for a historical serialized length. Save floor 58 remains unchanged.
  Warrush probes one capacity slot every two ticks (32 ticks for sixteen slots).
  Empty slots fall through to normal decisions, preserving smaller-match timing.
  Replay floor 127 gates the new capacity; network protocol 51 additionally
  requires the format-128 compact save reader. Older saves load into the current
  simulation.
- `.map`/`.game` files are gzip level 6 by default (`FileManager::writeGzipAtomic`/
  `writeGzipAtomically` in `libgag/src/FileManagerGzip.cpp`; the gzip header carries
  a zero timestamp and OS=unknown, producing deterministic bytes with the same
  zlib encoder). This is a container change, not a save-format schema change: the bytes
  inside the gzip stream are exactly what today's serializer already writes, so it
  does not need a `VERSION_MINOR` bump. Reading is transparent and extension-driven:
  `FileManager::openInflatingInputStreamBackend`/`glob2OpenMapOrSaveInputStreamBackend`
  inflate a `.gz`-suffixed path and reject corrupt/truncated gzip data; a raw legacy
  file with no `.gz` sibling still loads unchanged. `glob2PreferGzipReadPath`/
  `glob2GzipWritePath`/`glob2ListMapOrSaveFiles` (`src/map/io/MapHeader.cpp`) are
  the read/write path-resolution helpers most call sites should use rather than
  hand-rolling the `.gz` suffix logic. Replays and network protocol gates are
  unaffected.
- Terrain simulation properties retain the fixed layout in `src/map/TerrainProperties.h`,
  indexed by stable 16-bit `TerrainType` IDs in a map-owned immutable `TerrainRegistry`.
  Use `map.terrainProperties(type)` for a terrain or `map.terrainPropertiesAt(...)`
  for a cell; the global constexpr table defines only the built-ins. Walking, swimming, flying, building eligibility,
  resource habitats, irrigation, movement rates, health and projectile obstruction
  are independent capabilities. Use a property predicate when asking what a cell
  permits; compare IDs only when its identity is the actual question (for example,
  an editor brush or a generator's material selection).
- `Map::terrainSeed()` is presentation state saved with the map (format 138): it salts
  the terrain material hashes so maps look distinct; generators derive it from their
  request seed and the editor can reroll it. It is never read by simulation code and
  is not in `checkSum()`; see [terrain materials](../assets/terrain-materials.md).
- Terrain is stored once per map vertex (`Map::vertexTerrain`, save format 146).
  Vertex (x,y) is the top-left corner of cell (x,y); `cellCorners(x, y)` returns the
  top-left, top-right, bottom-left and bottom-right corners. A cell's rules come
  from its corners through `combineCornerRules` (`TerrainPropertiesLayout.h`):
  equal corners keep their terrain's exact profile; mixed corners are walkable when
  any corner is, never swimmable or buildable, block projectiles and count as a
  shoreline when any corner does, and are otherwise as permissive as their weakest
  corner (speed and ground damage from the walkable corners). Mixed grass/sand and sand/water cells reproduce the retired shore
  profiles exactly, which is why no shore terrain types exist.
- A per-map `CellRuleTable` (`src/map/CellRules.h`) compiles each corner combination
  once: properties, movement costs per swimming class, air costs and resource
  habitat. Cells store only a rule index; rule t is the uniform cell of type t, and
  mixed combinations follow in the order the map first needs them, up to 65536.
  Snapshots, gradient preparation and AI observations share the table immutably.
- `Map::terrainTypeAt` returns a cell's terrain when its corners agree and
  `MIXED_TERRAIN` otherwise. That sentinel is never stored; never index a table
  with it. The script-facing `getTerrainType` maps mixed cells to the unknown
  category. Write vertices with `setVertexTerrain`, `paintVertices`,
  `paintVertexSquare`, `assignVertexTerrain` or `fillTerrain`, and batch edits with
  `editTerrain()` so snapshots, topology and ecology caches stay consistent.
- Grass directly against water is legal: the cells between are walkable and
  unbuildable. Beaches are a painting and generation convention. `paintVertices`
  (by default) and `paintVertexSquare` turn an opposite vertex next to painted
  grass or water into sand. `Map::layBeaches()` handles a whole map independently
  of scan order and turns both sides of every grass/water contact into sand, a
  two-vertex beach. Editor terrain brushes stamp vertices; the smallest
  figure is one vertex.
- Files older than format 146 convert at load. Their classic corner grid gives the
  vertices; then a vertex touching a cell that held a non-classic terrain takes it,
  preferring the cell it is the top-left corner of, then the cells to its top-left,
  top and left. A classic ID that disagrees with its cell's corners (an older direct
  cell edit) is kept the same way. Where two different whole-cell terrains touch,
  the cell converted first loses corners and becomes a transition. Saved sprite
  frames are skipped.
- Detailed terrain rendering resolves shipped appearances through a
  presentation-only material catalog, corner coverage resolver and CPU compositor.
  `data/terrain/tileset.json` defines those materials independently of gameplay
  IDs; `TerrainPresentation.h` retains semantic editor and image-interchange
  metadata. See [terrain material authoring](../assets/terrain-materials.md) for
  variants, boundary profiles, asset validation and cache behavior. Visual catalog
  changes must not change saved state or simulation RNG use.
- Built-in terrain is table-driven. `TerrainGroup.h` defines one property profile per
  gameplay group; `TerrainTypeTable.h` lists every `TerrainType` with its group, external
  name, string-table label and semantic colours, and the `TerrainProperties.h`,
  `TerrainPresentation.h` and `TerrainExperiments.h` tables derive from it. Members of a group are byte-identical
  profiles, so the registry deduplicates them into one property index; use
  `terrainGroup(type)` for palette and reporting buckets, never for simulation rules.
  Adding a type is one enumerator, one row, one label and one material binding;
  adding a group is one profile and, when gated, one `ExperimentId`.
- Format 141 raised the built-in count from 7 to 31; format 146 retired the two
  shore types (IDs 5 and 6), moving the catalogue down by two to `TERRAIN_COUNT` 29.
  `TerrainRegistry::savedBuiltinCount` gives a file's built-in count
  (`TERRAIN_COUNT_BEFORE_CATALOGUE` or `TERRAIN_COUNT_BEFORE_VERTEX` for older files),
  `TerrainRegistry::currentTerrainId` remaps its saved IDs, and
  `TerrainRegistry::deserialize` takes that count. Custom registries re-serialize
  with shifted IDs, so their digest changes and replays that embed one no longer
  verify.
- Runtime types inherit a shipped appearance and occupy vertices like built-ins.
  Any paintable built-in is a valid `base` or `appearance`. Import definitions through
  `Map::importTerrainDefinitions` before a match or in the editor. It validates and
  compiles the complete replacement before publishing it, preserves existing IDs,
  and appends new keys in sorted order. Scenes and gradient jobs retain the same
  registry snapshot; inner loops borrow indexed data. Scenes resolve each
  vertex's shipped appearance through the registry; the compositor resolves equivalent aliases
  to the same material without scanning custom definitions. Render caches bind the
  registry snapshot and actual asset revisions. Saved custom colors remain
  authoritative for previews and minimaps; built-ins use catalog palettes.
  Experimental authoring gates live in `TerrainExperiments.h`; maps carry required
  experiments into matches, while saves retain them independently of user settings.
  A runtime definition whose properties equal a gated built-in group's profile
  requires that group's experiment too (`Map::requiredTerrainExperiments` compares
  property indices); a definition with its own profile stays ungated.
- Trail retains stable terrain ID `4` (`TRAIL`) and experiment position `3`
  (`TrailTerrain`). Its external name, translation keys and serialized experiment
  key remain `road` / `road-terrain` for scripting, reports, editor actions and
  existing files. The material catalog chooses the detailed appearance for that ID.
- Ecology caches terrain-only land and aquatic fields for the map's lifetime.
  Normal growth, harvesting, unit movement and building placement do not rebuild
  them. Map replacement invalidates them; terrain edits invalidate them only when
  effective fertility contributions, inhibition, shore support or local growth
  factors change. Habitat-only edits update the resource masks of the cells around the vertex, and other
  capability changes retain the fields. A query inside an edit batch observes all
  preceding changes; closing the batch does not discard an already-current field.
  The weighted kernels preserve the classic paired water/inhibition and rotated
  shoreline probes; growth reads their cached results. Fields use Q16 integers,
  while opportunity rates use `Fertility::kRateScale` (three times Q16) so wheat
  retains positive growth even at the smallest nonzero fertility. Keep these
  units distinct. Weighted contributions below one Q16 quantum round down;
  classic terrain probabilities retain their exact integer numerators.
  `Tile::canResourcesGrow` is the saved scenario override;
  `Map::canResourcesGrow` also checks the terrain capability.
- Save format 136 embeds custom IDs, keys and fully resolved properties and presentation
  before the tile data. Legacy numeric presentation fields are retained verbatim
  for round trips and checksums; the material catalog controls detailed drawing.
  Bounded JSON byte chunks support binary and text streams.
  Serialization emits definitions in canonical ID order, with object fields in key
  order, and import/load
  releases the parsed JSON tree before compilation to bound temporary memory.
  Loading rebuilds compiled tables before restoring dependent caches;
  it never consults authoring JSON files. Earlier files use the built-in registry;
  pre-134 files also derive canonical IDs from legacy sprite ranges. Save floor 58
  remains unchanged. Building format 137 adds the per-game building catalog; replay
  floor 137 and network protocol 57 introduced those simulation/catalog gates.
  The completed-tick observation phase introduced replay floor 139. Runtime resource
  catalogs introduced replay floor 140 and network protocol 59.
  Damage-weighted routing and idle safety introduced replay floor 142 and network protocol 60.
  Engine snapshots and scheduled AI decisions introduced replay floor 143 and network
  protocol 61; building artwork raised the protocol to 62. Vertex terrain set replay
  floor 146; greedy-only fetching (format 147) and scheduled building gradients
  (format 148) set the current replay floor before growth integration. Delayed resource growth sets
  replay floor 149 and network protocol 67; building area effects set replay floor 150 and network protocol 68; private entity RNGs raise the replay floor to 151 and private world/story RNGs raise it to 152.
  Loading earlier saves rebuilds cached routes on maps with terrain health effects;
  current saves retain their completed and pending fields for exact continuation.
  Custom registry checksums hash canonical serialized fields, not struct padding.
  Built-in-only maps keep their previous terrain checksum contribution. Existing
  map-content hashes cover the embedded section for LAN, online and verification.
- Routing values expected terrain damage at **20 ticks per HP**. For damage rate
  `d` HP/tick, the effective travel cost is `travel * (1 + 20*d)`. Compile the
  speed-adjusted cardinal cost once, round its weighted value to nearest integer,
  then derive the diagonal with the existing `cardinal*14/10` integer rule.
  Ice therefore costs 33 straight and 46 diagonal; grass remains 10/14. Healing
  gives no discount. Ground profiles remain shared by swim class, independent of
  unit type, current HP or hospital availability. Strategic travel/influence
  fields retain travel-only costs; route-derived distance estimates include the
  preference penalty and can consequently make long hazardous jobs less attractive.
  Authored extremes saturate at cardinal 181 (diagonal 253), preserving compact
  fields and readable maps. Finite field range still limits very long costly routes.
  Idle units on safe ground never wander onto damaging terrain. Idle units already
  exposed follow a lazily built shared reverse escape field, allowing hazardous
  intermediate steps. Ground fields are keyed by team, swim class and whether the
  unit is escaping forbidden paint; flyers share a separate air field across teams.
  Buildings, terrain and forbidden paint invalidate ground fields. Resource edits
  invalidate only the movement classes whose blocking properties changed; stock
  changes alone do not. Air fields react to terrain and air-blocking resources. Occupancy is checked at the next step and
  does not invalidate either field. Unreachable results are cached too. The cache
  retains at most 64 MiB of 32-bit field cells (or one field on larger maps), evicting
  least-recently-used profiles; temporary propagation queues are additional memory.
  Complete synchronous rebuilds consume no RNG, so
  cache eviction and save/load discard cannot change directions or timing rules.
  A cold query can still require a full-map build; subsequent queries inspect eight
  neighbors. A unit waits if traffic blocks every descending step. Flyers use their
  separate air damage rate. These rules are preferences for travel, not
  guarantees against lethal crossings or overrides of explicit local combat moves.
- Registry compilation calculates movement and air costs once, deduplicates cost
  profiles and caches distinct edge steps. Runtime gradient setup scales with
  distinct profiles, not registered IDs. Uniform, binary swimming and general-cost
  kernels dispatch outside cell loops. The general kernel has scalar, SSE2 and NEON
  implementations and compiled 64/128/256 bucket rings. Map counts select the smallest
  safe ring from terrain present; unused slow definitions cannot enlarge it. Search
  setup validates reachable edge costs against the selected ring before changing a
  field, because a too-small ring can alias a future cost layer. Keep validation out
  of cell/neighbor expansion; compact production snapshots bound it by distinct costs.
  Capability counters keep health, air and projectile shortcuts independent of
  registry size. A* retains the historical built-in lower bound and lowers it only
  for faster custom terrain actually present, preserving old route choices.
  Map property queries use a derived two-byte index plane into deduplicated
  fixed-layout property structs, keeping equivalent custom IDs out of the hot
  property working set. Canonical tile IDs and persistence remain unchanged.
  Maps lazily cache a one-byte cost-profile plane and only the distinct costs
  present in that plane per queried swimming class,
  removing the ID-to-profile lookup from general-cost cell loops. These planes
  share ownership with searches/jobs and invalidate together with terrain snapshots.
  Eager fields, resumed building searches, worker snapshots and strategic travel
  share compiled integer costs and reusable scratch storage.
- Runtime-terrain performance qualification compares equivalent maps with the
  current built-in count (`TERRAIN_COUNT`), 259 and 1,024 definitions, plus
  distinct-cost and 16,384-type stress cases. The maximum catalog fixture assigns
  aliases in 16×16 blocks so intermediate paint operations stay within the
  separate 65,536-corner-rule limit; it retains all catalog definitions but does
  not place every identity on the map. Use release
  builds on a quiet machine, warm up, randomize paired execution order and collect
  at least ten repetitions. Report CPU and wall time separately, with rendering
  and memory costs. Repeatable regressions over 2% full-match CPU or 5% terrain
  kernel time block acceptance; noisy measurements do not establish a pass.
- Keep the terrain index domains explicit when changing this code:
  canonical `TerrainType` IDs identify saved definitions; property indices select
  deduplicated simulation structs; per-swimming-class profile bytes select movement
  costs; scene appearance IDs select shipped visual materials. None is a valid
  substitute for a canonical ID in serialization or scripts. These derived planes
  are rebuilt from the registry and cells, never serialized. Registry factories
  publish `shared_ptr<const TerrainRegistry>`; copying a registry is private because
  authoring presentation strings borrow its owned key/name storage.
## Scheduled gradient work

- The engine has a completed-tick observation phase. After fog, projects and
  scripts, `Game::syncStep` selects/reserves one periodic gradient job. The next
  observation publishes the complete simulation preset in a game-owned snapshot
  store. `Game::prepareReadBoundary()` publishes due statistics before capture and
  admits one new sample on advancing ticks. Its capture does not depend on polled players or
  pending job kinds. All polled controllers share one immutable AI view of that
  boundary. Map jobs retain named gradient, building-gradient or growth views,
  which release unrelated components independently. AI and gradient batches share
  the compute executor; gradient seeding and propagation may outlive the
  observation boundary. Direct stepping captures and submits before returning;
  explicit deferred stepping leaves that capture to its caller. Direct map
  stepping invalidates the cached boundary because its caller need not advance
  the game's tick counter.
- The simulation preset always includes catalogs, terrain, resources, occupancy,
  areas, visibility, entities, teams, rules, published resource fields, growth and
  cumulative statistics inputs.
  `CaptureOptions` adds the ordinary presentation preset (session, effects and
  compact statistics), with explicit history, telemetry, entity-diagnostic and
  annotation options. Consumers do not contribute arbitrary capture masks.
  Complete snapshots still share unchanged components and refresh dirty map
  chunks in reusable buffers. Paused edits, configuration changes and world
  replacement retain their explicit invalidation boundaries. A cold option may
  widen the cached capture within a boundary, reusing its immutable entity records
  and sidecars without another extraction. A changed tick, observation revision,
  configuration revision or map generation refreshes entities; named views drop
  unwanted extras.
- Gradient selection, round-robin flags, invalidation and publication stay on the
  simulation owner. Jobs own their output and use worker-private scratch; they never read live map
  arrays or mutable seed caches. Publication remains after the configured delay
  (eight ticks by default), before team stepping. Completion time never changes
  publication time. Saving joins private work without publishing it, then writes
  the existing field/deadline representation. Reconfiguration drains work before
  resizing scratch; teardown drains callbacks and discards reservations. Job-owned
  errors survive executor batch retirement and surface at save/publication.
- Stale building walking fields are refreshed by `BuildingGradientPipeline`
  (`src/map/gradient/`, `MapGradientScheduling.cpp`). Team stepping requests a
  refresh and keeps serving the old field; after the tick up to four requests are
  staged, captured at the observation boundary with the periodic job, built on
  workers and published `buildingGradientDelay` ticks later (the match rule, 1–8,
  default 8), before team stepping. Publication swaps the field and its search on
  the owner; it is discarded if a synchronous rebuild or a reset superseded it.
  Access metadata (`locked`, and a clearing flag's `anyResourceToClear`) follows
  the newest capture, so it, and the AI's view of it, may lag up to the delay. Team-wide resets and forbidden-area paints keep the old
  walking fields serving until the refresh publishes, so units may follow a
  pre-edit field for up to the delay. Fields a building has never had (or lost to
  idle eviction or its own move, type or range change), queue overflow (more than
  64 waiting requests) and maps without a game build synchronously.
  `Map::predictBuildingDepth` reads the generated
  [depth model](../ai/architecture/building-gradient-depth-model.md) from the field's own past
  reader demand (its serving search's required cost and `settledCostHint`). It only moves
  search work between worker and owner; `GLOB2_BUILDING_DEPTH=full|table|lazy`
  or an operating point name overrides it for timing.
- The executor ring holds 65 batches: each deferred producer holds at most its
  horizon plus one (AI decisions 8, periodic gradients 16, building gradients 8, resource growth 16),
  plus headroom. Gradient jobs use no AI controller lane and submit no nested
  deferred work.
  Thread counts change execution only. Review scratch ownership, input lifetimes,
  RNG and shared caches before adding another producer.
- Periodic snapshot seeding uses `SnapshotGradient` and shared `SeedCells`
  predicates. Each executor slot owns derived seed templates, maintained using
  exact immutable chunk versions, with direct-kernel fallbacks. Material caches
  retain compact base fields and goal bitsets. No cached template retains snapshot
  buffers or reads the live mutable material cache. Immediate building seeding
  remains in its domain source files.
  Material and clearing precedence comes from the restricted unsigned-integer
  source `src/field/shaders/preparation-rules.json`; regenerate its C++, OpenCL
  and GLSL predicates with `tools/shaders/gradient_preparation_rules.py --generate`.
  Builds reject stale generated predicates. Independent scalar reference tests
  remain handwritten. Ordinary resource/corner-rule compilation produces compact
  resource traits and accessibility/movement tables; periodic seeding does not
  rebuild those tables. Clearing reads no visibility or stock sidecar values.
  Execution selection is captured before CPU seeding. Resident GPU preparation
  is not connected to production execution yet. `GradientResidency.h` models
  exact-owner identities, upload ranges and retirement transitions; it supplies
  neither adapter bindings nor budget leases. Its tests cannot demonstrate
  adapter residency or GPU preparation. Uploaded contents become readable only
  after all transfers succeed and retire; invalidation alone cannot release
  retained owners. Changed ranges include alignment widening and metadata in
  the 256 KiB / 16-region speculative quota.
  `MapGradientPropagation.cpp` starts eager fields through the private
  `src/field/GradientPropagation.h` core; `BuildingGradientSearch.cpp` resumes
  building fields. Both use `src/field/GradientRelaxation.h`. Keep their cell-cost
  and queue ordering contracts shared when tuning architecture-specific kernels.
  `src/field/GradientConstants.h` owns the field encoding; `Map` keeps its pipeline and
  per-executor scratch in an opaque `GradientRuntime`. Save/load reaches pending
  work through snapshot views, not the pipeline's mutable jobs.
- `src/field/` is the Map-independent field library. Weighted paths retain
  bucket queues and scalar/SSE2/NEON relaxation; uniform four/eight-neighbour
  fields use an ordered FIFO with caller-owned payloads and admission rules.
  Seed and neighbour order matter for first-discovery payloads and early stopping,
  including Cortex wheat depth and Maxima food claims. Keep those searches ordered.
  Callers own seeding, field encodings, transient scratch, cache ages and publication.
  Sharing a solver does not make fields with different predicates interchangeable.
## Optional GPU gradient computation

Settings → Gameplay → **Gradient backend** selects CPU automatic, scalar, SSE2,
NEON, OpenCL, Vulkan or WebGPU according to build and runtime availability.
Renderer selection is independent. CPU automatic selects an available SIMD kernel,
otherwise scalar; explicit CPU kernels also apply to resumed building searches.
The **Gradient algorithm** control offers bucket propagation on CPU, Frozen8 on
OpenCL, and Frozen8/Jacobi on Vulkan and WebGPU. Every implementation must produce
the same integer fields before it can be qualified. Order-sensitive AI traversals
retain their existing implementations.

A preference change requests an owner-side tick-boundary barrier. Existing jobs
finish privately and keep their publication deadlines, serving fields and pending
queue positions. Subsequent jobs capture the new configuration/generation. Neither
GPU completion speed nor backend switching advances or resets the simulation
schedule. Resumable building searches stay on CPU without restarting their queues.

An absent preference uses CPU automatic with graphics and OpenCL headless.
Legacy `gpuCompute=0|1` resolves to CPU automatic/OpenCL. Explicit
`--gradient-backend` overrides `--gpu`, then `GLOB2_GRADIENT_BACKEND`, then saved
preferences/defaults. Algorithm selection uses `--gradient-algorithm`,
`GLOB2_GRADIENT_ALGORITHM`, then the saved choice; incompatible saved choices use
the backend default. Explicit incompatible CLI algorithms are rejected. Settings
are local and never saved in simulation state. An explicit menu change supersedes
the launch choice for the current run and also saves the preference.

The process coordinator is additional to the shared CPU pool; compute thread
counts still include the owner. One-slot pools stay CPU-only. Initialization,
submission and completion run outside the game loop. Already-ready compatible
requests group up to eight without waiting for a batch. New backends use simple
per-field transfers inside those groups. Frozen8 uses four-color integer relaxation
on 16×16 tiles with frozen halos and eight sweeps; Jacobi uses separate input/output
buffers for each round. A convergence signal starts complete-field validation;
it does not by itself establish correct execution.
The GLSL sources generate pinned SPIR-V and WGSL with integer-only wrapping.
Vulkan/WebGPU readback also passes an exact positive-cost fixed-point check against
original seeds before commit. A convergence flag alone cannot publish fabricated
sources or stale data. A proven fixed-point mismatch stops the run through the
original completion ticket and writes original seeds, costs and the full 32-bit
readback to `artifacts/gradient-proof-*.bin` (or `GLOB2_TEST_ARTIFACTS`). The binary
uses little-endian records, with a text sidecar identifying the device, driver,
shader and job generation. It does not
count as a committed GPU field or a successful CPU fallback.

Vulkan integration remains unqualified: native NVIDIA stress runs have produced
intermittent fixed-point proof failures. Retaining this guard prevents those
fields from reaching simulation state; passing individual kernel tests does not
establish live-game correctness. Threaded-browser WebGPU also requires complete
game, continuation and platform qualification before production enablement of
resident preparation.

Accelerator payloads share 64 MiB of host storage and 128 MiB of tracked device
storage. Demand-sized storage and immutable cost-buffer leases bound retained
inputs. Unsupported devices, memory pressure, cancellation and driver errors
recover the exact CPU result from original seeds; partial GPU results are never
published. Required transfer failures retain unsafe storage until completion can
be proved. Saves, reconfiguration and shutdown drain required work.

After games and their executors drain, the application owner joins the process
coordinator before SDL teardown or process exit handlers run. A driver can
register exit handlers during asynchronous initialization, so the broker's
static destructor alone cannot establish that ordering. Test entry points use
the same barrier; subsequent registrations can restart the coordinator.

OpenCL dynamically loads an installed desktop driver. Vulkan dynamically loads
native drivers on Linux, Windows and Android; Apple bundles a hash-pinned MoltenVK
framework. Compute devices have no rendering surface or swapchain. Threaded
browsers use a separate WebGPU device and an asynchronous main-browser bridge;
only coordinator workers wait for its completion. Serial browsers stay on CPU.
Original inputs and callback storage survive cancellation until completion proves
safe retirement; late results never publish into an expired job.

Diagnostics distinguish requested/selected execution, committed GPU fields and CPU
fallbacks. Adapter phase totals include canceled browser work when its retained
callback retires, exactly once. Per-field execution observations include only
completion proof seen before the worker resolves its ticket; a later callback
cannot mutate a released field. Optional **Verify gradient results** /
`--gradient-verify on` computes a
scalar reference from original seeds before publication. A mismatch stops the run
and writes `gradient-mismatch-*.bin` in the artifact directory with geometry, cap,
family, seeds, expected/actual fields and packed costs. Verification has separate
CPU timing and should be disabled for performance comparisons. There is no
automatic policy, calibration or tuning.
`GLOB2_GRADIENT_VERIFY` accepts `1/on/true` and `0/off/false`; explicit CLI choices
take precedence. Unknown values disable verification. Standalone CPU benchmarks
force verification off and bucket propagation so inherited game settings cannot
contaminate their timing.

## Shared field primitives

- Choose the smallest field operation that preserves the caller's contract:

  | Operation | Entry point | Caller responsibility |
  | --- | --- | --- |
  | Weighted path field | `gradient_kernel::propagateField` | Encode seeds/obstacles, supply stable terrain and a `GradientWorkspace`. |
  | Resumable weighted paths | `gradient_kernel::expandBucket` | Preserve pending buckets and settle whole cost layers before pausing. |
  | Uniform distance field | `field::expandDistances` | Seed equal distances, choose the unvisited sentinel and ordered stencil. |
  | Ordered FIFO with payloads | `field::traverse` | Admit and enqueue neighbours; retain first-discovery payloads and stopping rules. |
  | Domain heap search | `field::traversePriority` | Own costs, comparator, stale-entry checks and parent ties, including zero-cost edges. |
  | Component stack/queue | `field::depthFirst` / `field::breadthFirst` | Own discovery and push order. |

  `Grid::neighbors` supplies raw coordinates for bounds checks before wrapping;
  `Grid::neighborIndices` supplies wrapped indices. Both retain stencil order and
  aliases on thin grids. The vector FIFO keeps discovery history; `Frontier`
  consumes entries and retains storage for the largest pending frontier. Clear
  and seed either workspace at the owning caller. An early stop preserves writes
  already made; grid traversal finishes the current neighbour stencil before
  visiting the next entry. Use `breadthFirst` for stops during expansion.
- Influence has two distinct contracts in `src/field/Influence.h`: convergent
  maximum-contribution propagation and four directional sweeps. Castor requires
  the latter's staggered scan order and byte arithmetic; replacing it with
  convergence changes AI decisions. Map retains the cooperative checkpoints
  around convergent rows. No solver depends on Map, AI, threading or serialization.
- In `src/map/gradient/MapGradientChamfer.cpp` the chamfer distance transform's
  convergence-pass cap is bounded by the Uint8 value range (256), not by the
  Borgefors 1-pass result. Borgefors holds only on an obstacle-free grid; with
  obstacles each bend in the propagation path costs about K/2 passes, and real
  128×128 maps needed well over 8. The cap is a tripwire for monotonicity
  violations, not a throttle. Do not derive a tighter bound from grid geometry.
- A candidate comparison used only to pick the best of several options (which unit
  to hire, which move to take) must not allocate, rebuild or refresh anything it
  touches, including cache-use timestamps. If scoring can trigger the same side
  effects as actually doing the work, "read-only" claims about it are false and any
  performance comparison built on it is unreliable.
- Never bound simulation work by wall-clock time or a timeout: this is a lockstep
  engine, and two machines running the same tick at different real speeds must still
  do identical work. Use a fixed count of ticks, steps or comparisons instead.
- A new regression test only protects the codebase once
  `.github/workflows/build.yml` actually builds and runs it; one that only runs by
  hand, once, is not a regression test. Add its translation unit to `test/tests.py`:
  the unit or engine binary is already in the job's single "Build glob2 and the
  regression harnesses" command and `test/run_tests.py` picks the new cases up on
  the next run, so no per-test step is needed. A harness that must stay a
  separate program (two processes, a golden-table tool) gets a `PROGRAMS` entry
  and one step that only runs it: a separate `scons` call per step re-reads the
  whole build and compiles one file at a time. Builds that need other options
  (`role=relay`, `opengl=0`) belong in
  the `linux variants` job, and long CPU-bound checks in a job of their own, as the
  golden-map sweep does; its four sweep shards are split between two jobs per
  toolchain, alongside a job for telemetry and generator defaults. These jobs reuse
  the main Linux build artifacts when native checks are selected; map-only diffs
  build the required programs themselves. Tests run in parallel after the shared
  build. The Linux variants matrix owns the relay build; the main Linux jobs do
  not repeat it.
  Browser checks follow the same rule: build once per job, pass outputs to the
  test jobs as artifacts, and shard long suites rather than lengthening one job.
- A map generator's `revision` is enforced by `MapGeneratorGoldenTest`: a seed's map changing
  while the revision stays fails the check, so bump the revision and run `--update` together
  (see the framework reference). `--sweep` there is the first thing to run after touching
  colony placement; a cell it fails is a "Generation failed" a player would see.
- A cache or other retained state with no eviction policy needs an explicit bound —
  a count or a byte budget. "It would take an enormous game to reach" is not a bound.

### Hiring

`HiringPipeline` belongs to each `Game`. It admits FIFO team jobs from the shared
completed-tick world snapshot at `t % 4 == teamNumber % 4`, then publishes their
outputs before team stepping at `t + 4`. Workers derive demand, material
apportionment, supplier modes, qualifications and integer rankings. The owner
establishes immutable observations and safe building-route generations, enqueues
jobs, publishes due results, and validates/commits hires in the existing priority
buckets and rounds. Task swaps remain serial. Attraction clocks retain their normal progression;
when a round cannot obtain fresh suggestions, its outstanding recruitment request
may retry for eight ticks without extending the deadline. Delivery cannot borrow
that round's attraction seats while the request is outstanding. This bounded
retry changes recruitment timing and prevents a cache miss from adding another
33-tick clock cycle. Requests survive save/load and configuration changes; live
role demand determines whether they are still owed.

Each supported job receives a bounded list of incarnation-aware unit references.
Live admission rechecks ownership, activity, medical eligibility, skills, cargo,
current quotas, reachability, food budgets and destination lookup. Temporary
unavailability only skips the suggestion for that attempt. Exhaustion or missing
lists waits for a scheduled refresh; there is no population-wide fallback scan.
Clearing, exploration, defense, ground and flying transport share this contract.

Building-route expansion and owner reads share the existing per-generation locks.
Retirement retains the original buffer/search until leases end, without copying
it. Workers never call live building APIs. Immutable snapshot components and route
handles are shared by reference, not copied for each team. Private planner caches
are bounded separately from deterministic pending/published output reservations.

Published lists, fixed rules, last admission state, input identities, output
reservations and completed pending outputs/deadlines are saved in format 159.
Saving drains computation without early publication. Older supported saves start
with empty hiring state and the fixed refresh schedule; the compatibility floor
remains unchanged. Configuration epochs reject pending results even when a
building configuration changes away and back. Unit changes are checked live.
Checksums include ordered published suggestions and logical pending identities,
independently of worker readiness. This policy change advances the simulation
revision; older match records and mixed simulation-version network games are
subject to the normal admission gates.
