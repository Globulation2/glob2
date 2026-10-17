# Simulation state and field computation

Current simulation state, deterministic randomness, terrain rules and scheduled field publication. For change verification, use [simulation verification](../development/simulation-verification.md).

## Domain state

A `Team` is a colony; a `Player` controls a team, and several players can share one.
For timing and scheduling, start with `src/game/Game_sync.cpp` and `src/engine/EngineRun.cpp`.

## Entity population order

Each team owns append-ordered intrusive unit and building lists. The hooks live
outside `UnitState` and `BuildingStateRecord`; immutable worker records never
contain owner-thread links. `myUnits` and `myBuildings` remain allocation and GID
lookup tables. They are not population iteration APIs. Creation appends, removal
unlinks before destruction, and conversion unlinks from the source before
appending to the destination. Surviving entities keep their position, including
in-place building upgrades. Capability and service lists remain independent.

Read-only population queries use `liveUnits.entries()` and
`liveBuildings.entries()`. A walk that can change membership uses the scoped
`mutableWalk()` cursor. Membership removal repairs all active cursors, including
removal of a pending item or the original tail. Arrivals after a cursor starts
wait for its next traversal. Team phases remain sequential: a converted arrival
can participate if its destination has not yet started its walk. Dead marking
retains the existing destruction phase.

Worker observations expose compact immutable `unitsForTeam(team)` and
`buildingsForTeam(team)` ranges plus direct GID lookup indexes. Record order
follows population order; consumers must not assume ascending slot IDs. Operations
whose public contract requires ID ordering implement that ordering locally.

Save format 166 writes counted entity records in population order with their
existing IDs, followed by cross-reference payloads. Older slot formats bootstrap
ascending slot order; current loads restore the exact saved order. Team checksums
include ordered entity hashes and the persisted live round-robin task cursor.
Task swapping visits at most four distinct live candidates per team tick, rather
than probing potentially empty slot IDs. This intentionally changes scheduling
and replay trajectories; the save support floor is unchanged.

Deep list/table consistency sweeps belong to explicit diagnostics and tests,
not normal ticks. Rebuilding building capability lists never reconstructs or
reorders population membership.

## Routine desynchronization checksums

Routine checks cover live units/buildings, both RNG state words, team/player state,
and deterministic scheduling metadata every tick. Entity traversal uses the live
lists; its normal path omits diagnostic field-vector branches. RNG fingerprints
and periodic statistics use MurmurHash3 x86_32 with seed zero over explicit
little-endian words (64-bit scalars feed the low half first), avoiding byte
extraction and serializer names. Its rotations and final avalanche avoid the
paired high-bit cancellation of a word-wise XOR/multiply mixer. This remains a
non-cryptographic error detector; collisions are possible.

Secondary coverage runs when the absolute simulation tick is divisible by 32:
AI-relevant aggregate statistics, all 32 smoothing entries and coverage-building
records; remote games also sweep map cells and material-field contents on that
boundary. Queue timing remains covered every tick. Persistent secondary-only
divergence is detected at the next boundary (up to 31 additional ticks); transient
differences that disappear between samples may escape unless they affect core
state. Diagnostic statistics ledgers stay outside routine coverage. Full
verification (`heavy=true`) retains
exhaustive map/statistics and pending-result coverage on every invocation.
Immutable behavior fingerprints remain cached by their catalogs and contribute
without reserializing definitions. No cadence counter is stored in saves.

Format 165 changes checksum values and replay acceptance without changing save
encodings or the minimum supported save version. Its simulation-version key
separates peers using the earlier checksum scheme. The explicit pre-refactor
checksum adapter retains its historical entity/RNG representation for fixtures.

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
    conversion, executed orders, script presentation), a latest waiting-player mask,
    and a per-tick pulse (`Team::wasRecentEvent` for every team). Network-wait notices
    consume the client mask so they can change during a stall without recapturing
    the retained world. `Game::executeOrderAndNotify` publishes
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
    `Game::advanceLiveSimulation`; only the observed building records
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

## Completed world observations

- `Game::observeCompletedWorld` is the common owner coordinator for graphical,
  headless, replay and direct stepping. At a new completed boundary it publishes
  due map, hiring, statistics and targeting results; captures one immutable world;
  then submits world consumers using named views of that same handle. Engine AI
  admission receives that handle explicitly. Due AI orders are delivered separately
  into the ordered command schedule, validated against live state, then executed.
  `Game::advanceLiveSimulation` only mutates live state and advances the tick; it
  neither captures nor submits preparation. `Game::step` is the direct caller's
  observe-then-advance entry point: it also updates unpolled JavaScript visibility
  history, but leaves AI polling and order execution to its caller. There is no
  deferred-step variant or runtime optional-snapshot fallback.
- Observation T represents the completed world after publication and before the
  orders for its next advancement. Orders and simulation mutations first appear
  in observation T+1. A published observation is never reconstructed to reflect
  an order acknowledgement. Synchronous order validation and demand queries still
  use live state. Every newly admitted background job has a later publication or
  order-delivery boundary; configured zero-delay AI/statistics now mean one tick.

  | Consumer | Observation input | Effective delay | Publication / first consumer |
  | --- | --- | --- | --- |
  | AI | Shared AI view of T | max(1, configured 0–8) | T+delay into the ordered command schedule |
  | Hiring | Shared entity and route views of T | 1, staggered refresh 4 | T+1 before capture; live worker assignment |
  | Statistics | Shared statistics inputs of T | max(1, configured 0–8) | T+delay before capture; statistics and presentation |
  | Targeting | Shared targeting view of T | configured positive delay, default 1 | T+delay before capture; live turret firing |
  | Periodic gradients | Shared gradient view of T | configured 1–16, default 8 | T+delay before capture; live routing |
  | Building gradients | Shared building-gradient view of T | configured 1–8 in matches; standalone maps use synchronous live queries | T+delay before capture; live routing |
  | Resource growth | Shared growth view of T | configured 1–16, default 8 | T+delay before capture; live resources |

- Initial setup and restore establish an observation before admission. Initial AI
  command slots are null until a decision's delivery deadline. Pause, pacing and
  network stalls retain the current observation and admit no duplicate world
  work. Authoritative jobs are admitted once per logical tick independently of
  local readiness, including an initially paused boundary; AI polling still pauses.
  An unpause order leaves the simulation tick unchanged until the next order
  delivery phase, so restored outputs due at that tick cannot be skipped.
  Actual paused edits explicitly invalidate its observation revision, so
  the same tick can have a new immutable identity. Order acknowledgements and
  waiting-mask changes only update client/command state.
- Format 167 saves map publication/submission phase markers. Restoring an already
  observed boundary neither advances its queues again nor resubmits its jobs.
  Earlier supported saves retain their completed pending outputs and historical
  deadlines, including zero-delay outputs; they are never regenerated from the
  loaded world. New admissions use the new timing rule. Replay floor 167 and the
  simulation revision separate the changed trajectory from earlier executions.
- The simulation preset always includes catalogs, terrain, resources, occupancy,
  areas, visibility, entities, teams, rules, published resource fields, growth and
  cumulative statistics inputs.
  `CaptureOptions` adds the ordinary presentation preset (session, effects and
  compact statistics), with explicit history, telemetry, entity-diagnostic and
  annotation options. Consumers do not contribute arbitrary capture masks.
  Complete snapshots still share unchanged components and refresh dirty map
  chunks in reusable buffers. Capture constructs only components that need filling;
  reused and unrequested components require no placeholder records. Paused edits,
  configuration changes and world replacement retain explicit invalidation
  boundaries. A cold option may widen an explicit inspection capture at an
  unchanged boundary, reusing all
  existing components without another entity extraction. Normal runtime options
  are fixed by the first observation request, even when presentation preparation
  is busy. Repeated requests reuse that handle; requesting an omitted payload is
  an error rather than permission to enrich it from live state. Camera
  extraction uses the retained full observation. Graphical sessions freeze the
  optional view inputs before capture so paused panel/debug toggles can project
  from that observation; headless consumers retain only their requested inputs.
  Store caching tracks tick, observation and configuration revisions, and map
  generations. Runtime callers establish a new observation only at the next tick
  or `Game::invalidateObservation()` for an explicit owner edit. Invalidating the
  standalone inspection cache does not revoke an established owner observation;
  live entity helpers and orders cannot refresh that retained handle.
  Named views drop unwanted extras.
## Scheduled gradient work

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
  refresh and keeps serving the old field; at the next observation up to four requests are
  staged from the shared handle with the periodic job, built on
  workers and published `buildingGradientDelay` ticks later (the match rule, 1–8,
  default 8), before team stepping. Publication swaps the field and its search on
  the owner; it is discarded if a synchronous rebuild or a reset superseded it.
  Access metadata (`locked`, and a clearing flag's `anyResourceToClear`) follows
  the newest capture, so it, and the AI's view of it, may lag up to the delay. Team-wide resets and forbidden-area paints keep the old
  walking fields serving until the refresh publishes, so units may follow a
  pre-edit field for up to the delay. First use after creation, idle eviction,
  movement, or a type/range change queues initialization on the same pipeline.
  Until publication, route queries return Pending; assigned units wait without
  abandoning their assignment or consuming movement RNG. Initialization seeds and
  settles only the minimum depth (32 cost units), then transfers the unfinished
  search buckets with the field. Hiring proposals and live readers continue that
  search only as far as their requested cells. Warm refreshes retain the existing
  depth model. The FIFO has no synchronous overflow fallback; stale entries are
  pruned and each live building/route/swim slot has at most one request.
  Saving retains the existing complete-field representation and remaining
  deadlines; format 170 adds cold destinations and a wider queue count. Historical
  save loading remains supported. Maps without an active pipeline retain the
  synchronous fallback.
  `Map::predictBuildingDepth` reads the production
  [depth model](../ai/architecture/building-gradient-depth-model.md) from the field's own past
  reader demand (its serving search's required cost and `settledCostHint`). It only moves
  search work between worker and owner. Experimental depth overrides are retired.
- The executor ring holds 79 batches: each deferred producer holds at most its
  horizon plus one (AI decisions 8, periodic gradients 16, building gradients 8, resource growth 16,
  team statistics 8, turret shortlist jobs 4),
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
submission and completion run outside the game loop. OpenCL groups up to eight
already-ready compatible requests without waiting to fill a batch. Vulkan and
WebGPU use individual service transactions because their adapters execute fields
sequentially: a later decline must not roll back an earlier completed field.
Each transaction retains its original due tick, ticket, controls and immutable
inputs. Frozen8 uses four-color integer relaxation
on 16×16 tiles with frozen halos and eight sweeps; Jacobi uses separate input/output
buffers for each round. The adapter commits output only after convergence and
confirmed driver completion. Numerical correctness is established by device tests.

Vulkan retains one adapter-owned work set because its execution is serial. The
set holds buffers, immutable descriptor bindings, command resources, a fence and
mapped staging/readback storage. It is reused only after all submissions retire,
including canceled work whose fence later proves completion. Unknown work stays
quarantined and charged. Host metadata and mapped allocations share the host
limit; every device allocation retains its device charge. An idle set is evicted
before declining seed-backup admission or replacing it with larger storage, and
explicit shutdown releases it after retirement. Inputs are overwritten for each
captured request; serving fields remain separate from this storage.

The GLSL sources generate pinned SPIR-V and WGSL with integer-only wrapping.
GPU numerical verification lives in tests. The runtime checks input bounds,
convergence, driver completion and safe storage retirement, but does not run
scalar comparisons, fixed-point validators or duplicate reference uploads.
Independent heap and fixed-point test oracles compare actual committed device
outputs, including retained NVIDIA failures, weighted costs and wrapped geometry.
This deliberately removes per-field numerical fault detection in ordinary games.

Direct Vulkan adapter batches remain atomic: batch commit flags publish only after
every member succeeds. A completed private field followed by cancellation retains
its dispatch observation,
but reports no GPU commit and leaves all caller seed arrays unchanged.
Vulkan uses device-local compute storage and maps only coherent transfer/control
buffers, preferring host-cached memory. Mapped allocations also charge the shared
host budget; their leases remain charged during quarantine. Preserve whole-game
stress coverage alongside isolated transfer and fixed-point regression fixtures
when changing storage or synchronization.

Backend/platform qualification remains separate from correctness checks and is
based on total CPU consumed, including recovery, final drain and startup reported
separately. Failed or inconclusive experiments do not establish CPU offloading.
Keep their measurements in review evidence. Resident preparation requires its own
memory, game, continuation and platform qualification; it is not enabled by these
solver and validator changes.

Accelerator payloads share 64 MiB of host storage and 128 MiB of tracked device
storage. Demand-sized storage and immutable cost-buffer leases bound retained
inputs. Unsupported devices, memory pressure, cancellation and driver errors
recover the exact CPU result from original seeds; partial GPU results are never
published. Required transfer failures retain unsafe storage until completion can
be proved. Saves, reconfiguration and shutdown drain required work.

Before consuming a polled/asynchronous OpenCL transfer, the backend synchronizes
its event after it reports completion. A completed status query alone does not
establish host synchronization. A failed wait follows the existing permanent
failure and CPU fallback path. Abandoned transfers retain their covering-proof
lifetime checks without publishing canceled output.

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

Backend status reports actual committed fields, dispatches and CPU fallbacks.
The coordinator owns initialization and bounded ready-work batching. One request
owns its immutable inputs, private output, cancellation and completion through
queued, running, recovering and complete states. Declines recover on an executor
worker; each ticket resolves once. Pending publication deadlines survive saves
and backend changes.

CPU configurations have no GPU service or session, register no GPU worker-only
predicate, and select CPU job/join callbacks at configuration. The application
installs its GPU shutdown callback only when GPU state is created, retaining it
across later backend switches; a CPU-only application never calls that barrier. An existing idle
coordinator parks without polling when its registrations are gone. GPU fences and
quarantined storage remain adapter-owned until safe retirement.

The verification preference, CLI/environment switches, detailed GPU profiling and
runtime proof artifacts have been retired. Existing settings files may contain
an ignored `gradientVerify` key. Use external profilers and separate correctness
runs; backend defaults and algorithm choices are unchanged.

## Shared field primitives## Shared field primitives

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

`HiringPipeline` belongs to each `Game`. At every read boundary, before matched
orders and including on paused or waiting replicas, it publishes due results and
then admits one job per team with `t % 4 == teamNumber % 4` from the shared
completed-tick snapshot. Results publish before team stepping at `t + 1`; early
completion cannot publish early, so participant counts never change outcomes.

Workers derive demand, material apportionment, supplier modes, qualifications and
integer rankings from immutable snapshot records. Each job keeps a bounded heap of
its best candidates, at most the observed vacancies plus two; optimistic score
bounds skip route queries only for candidates that cannot enter the heap. When a
team's total vacancies fit one list, a greedy pass in priority order gives each
building distinct candidates first; imminent attraction rounds likewise spread
offers between peer flags. Lists are suggestions, not reservations.

The owner keeps priority buckets, rounds, material quotas and task swaps. Admission
rechecks ownership, activity, medical state, skills, cargo, quotas, reachability,
food budget and destination for every suggestion, so flag edits need no list
invalidation. Missing or exhausted lists wait for a refresh; there is no
population-wide fallback scan. An attraction round with owed roles stays open for
up to eight ticks by running its clock past 32, independently of the refresh and
publication intervals.

Resource distances read the snapshot's published resource planes. AI query fields
are local planning caches, whose presence depends on local polling, so they never
enroll map fields. Missing planes, flying transport and consumer-specific supplier
modes use an optimistic zero estimate. Building routes borrow existing valid
generations through `SharedBuildingGradient` leases; workers settle only queried
cells under the generation's lock, and retirement hands the buffer and search to
the lease instead of copying them. Observation only captures route inputs. Missing or stale fields keep the
obstacle-free ranking estimate. After ranking and team coordination, the hiring
worker prepares routes only for the primary candidates selected to fill demand;
the fallback suffix stays on the list without explicit prewarming. Cold fields
are seeded lazily on that worker and published with the shortlist one tick later,
provided the building incarnation, slot epoch and topology still match. Existing
serving fields are reused, including stale generations, without changing their
refresh schedule. A usable field can still extend during normal ranking, so a
fallback position may already be covered. Live readers retain ordinary Pending
and lazy-extension behavior when preparation was invalidated or insufficient.

Creation and construction type transitions leave gradients uninitialized until
requested by a route query or primary-candidate hiring preparation. Saves written
with format 172 can contain proactive initialization requests and off-cohort jobs;
loading retains those requests and their publication deadlines, but new building
changes do not enqueue them. The format-172 continuation records remain readable
and writable so saving again does not drop inherited work.

Published lists and pending outputs with their deadlines are saved; saving drains
jobs without publishing early. Prepared cold fields awaiting the same deadline
are persisted from format 171 onward, using completed fields from their frozen
inputs; earlier saves contain no such prepared results. The save compatibility
floor is unchanged. Saves predating cached hiring start with empty
state. Lists are a pure function of the snapshot, so game checksums omit them.
Saves containing the previous four-tick hiring policy are no longer accepted.

## Turret targeting

Turrets consume ordered three-entry lists refreshed per team every two ticks and
published one tick after observation. The first currently valid entry fires;
owner-side ranking and fallback scans are absent. One pure background kernel
preserves the original preferred target at the head and ranks alternatives.
Capture joins the shared completed-tick snapshot union and existing compute pool.
Firing revalidates every entry against live state, so lists need no invalidation.
See [targeting lifecycle](../development/compute-benchmarks.md#cached-turret-targeting)
for scheduling and saved continuation.

Shared snapshot capture uses local dirty stamps for incremental copies. It does
not maintain terrain or occupancy protocol hashes after cached turret outputs
left the game checksum.

CPU-only gradient workspaces have no backend-session allocation. Periodic jobs use
a CPU callback selected at configuration, preserving the same immutable snapshot,
slot-local scratch and fixed publication time. GPU ownership and cancellation
state is allocated only for GPU-configured submissions.
