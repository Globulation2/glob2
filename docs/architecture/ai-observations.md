# AI observations and scheduled work

How immutable observations, compute ownership and delayed AI orders interact. Read the [architecture overview](overview.md) first.

## AI observations and delayed orders

All shipped AI controllers decide from an immutable engine snapshot through
`AIEngine::AIWorldView`. The simulation owner captures a union of required
components through `Game::captureReadBoundary()`. The engine declares AI, gradient,
admitted presentation and diagnostic requirements before publishing their union.
`Game::snapshots()` owns the shared Store; the AI pipeline receives a projection
of that published handle and does not initiate capture. Explicit
`Store::invalidateBoundary()` revisions allow an owner to publish same-tick edits
without advancing game time; existing leases stay immutable. Engine snapshot records live under
`src/engine/sim/snapshot/`; the AI adapter and shared queries live under
`src/ai/observation/`. Records contain values and stable entity identities;
rendering pointers and live `Game`, `Map`, `Team`, `Unit` or `Building` objects
are not decision inputs.

Controllers borrow the complete observation for one invocation and release it
before returning. Their private planning state, RNG, pending intents and bounded
caches may persist. Incremental caches retain explicitly named immutable component
inputs when necessary, rather than retaining the whole world. New snapshot fields
belong to the engine component that owns their source data; add capture and query
coverage when extending them.

Map storage and snapshots use the same trivially copyable records from
`src/map/MapState.h`. Resource, occupancy and area arrays are authoritative;
there is no maintained `Tile` mirror. Vertex terrain, cell rule indices and
visibility remain contiguous scalar arrays. The live `Map` and every snapshot expose the same
borrowed `MapState::View` (`src/map/MapStateView.h`), and each cell query has one
inline implementation there; `Map` members forward to it. Every cell write stamps
its 16x16 chunk (`src/map/MapChangeTracking.h`); capture compares whole-array
generations to share unchanged components. Reused pooled buffers copy changed
chunks when at most half are dirty; denser changes use contiguous whole-array
copies to avoid many strided row copies. Both paths copy the same authoritative
records without per-cell translation. Copy-byte metrics include the multi-material
stock sidecar, which is copied whole when resources are refreshed. Writes that
bypass the marked setters are caught by `GLOB2_SNAPSHOT_VERIFY=1`, which
byte-compares every capture with the live game. Published material gradient planes
sit in a dense registry keyed like the snapshot's plane table
(`src/map/ResourcePlaneKey.h`); teams keep sorted live entity lists
(`src/team/LiveSlotList.h`) so extraction visits occupied slots only. The old
`Tile` value is assembled only for compatibility consumers such as editor undo.
Growth flags retain their original byte values; growth predicates test nonzero.

Unit and building scalar state uses the authoritative `UnitState` and
`BuildingStateRecord` definitions beside those domains. Capture copies one record
per live entity; heap entities are not a contiguous slot pool. Runtime pointers,
query scratch and GUI state remain outside the records. Building observations
select private stock or the captured team's stock through an immutable resource
pool selector; team stock is not duplicated into every building. Supplier records
also capture a material-availability mask after reservations, used by periodic
market gradients without consulting live stock. Ordered
relationship IDs remain explicit capture work. Upgrade and repair feasibility
queries read the frozen map arrays when requested; capture does not scan every
building footprint to precompute unused decisions.
Controllers read those canonical records directly. The engine captures one shared
slot-to-record index so legacy slot scans preserve holes and ordering without
rebuilding per-controller entity or relationship tables. Immutable capability
lists are shared with the authoritative catalog index; selection uses their
existing order without cloning catalogs, rescanning definitions or sorting again.
Private overlays contain only changed planning fields and pending actions.
Script adapters preserve existing numeric observation types while records retain
live simulation types. Save formats continue to serialize fields explicitly,
not object representations or padding.

Mutation generations advance when exposed values change, including active
visibility, rather than on repeated identical writes. Use narrow component
queries for scalar map reads. Combined tile queries preserve all fields,
including farm eligibility, which remains an explicit derived query when only
that value is needed. Keep capture and derived-query costs separate in profiles.

`AIWorldView` validates dimensions and leased map-array sizes when it binds an
observation. Its scalar readers borrow the retained arrays directly: callers must
request the corresponding component and supply an index from that view's geometry.
Dimensions are immutable and wrapping masks/shifts are cached. Keep checked handle
queries at diagnostic boundaries; do not add component or bounds checks to each
inner-loop read. Use full tiles only when the consumer needs the combined fields,
including derived farm eligibility.

Snapshot component buffers are plain shared pointers in a bounded pool
(`src/engine/sim/snapshot/BufferPool.h`). A consumer holds a buffer through its
pointer; the pool reuses a buffer once that pointer is the only one left, after an
acquire fence that orders the consumer's final reads before the owner's next write,
including inputs retired by delayed gradient jobs. Reads need no locking, and a
buffer outlives the capture Store while any consumer holds it. Memory metrics are
computed on telemetry query, never on the capture path. Component pools permit
22 buffers, retaining the existing safety ceiling until the combined consumer
horizon is measured. Buffers allocate only on demand. Retired frame slots release
their world leases immediately while preserving reusable derived arrays; the
current frame, admitted preparation, retained view-refresh input and diagnostic
publication each have explicit ownership. The resource-plane pool retains its original 17-epoch bound because
presentation does not lease resource-gradient fields.

All parallel simulation work shares the map's `ComputeExecutor`
(`src/common/ComputeExecutor.h`): blocking `run()` batches for map computation and
deferred batches for AI decisions, periodic gradients, scheduled building
gradients and resource growth. Each deferred batch carries the tick it is due. Workers run deferred
jobs earliest due first, in submission order within a lane. The owner never runs
deferred work while a worker exists: at a join it only waits, even when the only
worker also runs presentation, which that worker interleaves with simulation jobs
(so a join may wait out one presentation chunk). Only an executor with no workers
runs deferred jobs on the owner, at the join, because nothing else can. This keeps
owner time split cleanly into owner work and owner wait (`compute_owner_jobs` is
zero whenever workers exist). A cheap AI batch at delay 0 computes
inline when it submits, outside the executor, and still publishes at the deadline.
AI controller lanes preserve decision order;
gradient and growth jobs need no lane.
`AIEngine::Pipeline` submits one batch per tick with one job per controller on
that controller's lane, and joins it at the deadline. The
match-wide `GameHeader::aiOrderDelay` is an integer from 0 through 8, defaulting
to 8 for new games. An order observed at logical tick `t` is delivered at `t + delay`. Thread
count and completion time never choose that deadline or which decision a
controller makes; the owner publishes in stable request order. Human orders and
scenario map scripts retain their existing scheduling.

The executor also provides a separate, bounded presentation queue. It runs one
chunk at a time on the last compute worker, leaving another worker for simulation
when at least two are available. Ready simulation work takes precedence between
chunks. Submission replaces the single pending request; the active request keeps
its immutable inputs until its current chunk finishes. Tickets expose completion,
cancellation and failures without joining. Simulation `run()`, `join()` and
`joinAll()` never execute or await presentation. With no workers, the application
thread calls `pumpPresentation()` explicitly. Reconfiguration and teardown cancel
pending work and wait for the active chunk without executing it on the caller.
Captured inputs are released on completion, even if a caller retains its ticket.
Presentation preparation uses these jobs and only the published world handle.
Data-dependent operations can yield and resume the same chunk: large defence
footprints visit at most 1024 positions per claim, and overlay clearing is sliced
into 1024-cell ranges. This also bounds cooperative work on serial browser hosts.

Commands own encoded order bytes, target incarnation, diagnostics and telemetry.
The owner validates current identities and normal order rules at delivery and
reports accepted, rejected or canceled execution through immutable receipts on
the next admitted decision. Controllers must distinguish an issued intent from
an observed effect, tolerate stale observations, and reconcile rejection without
resending or releasing a reservation prematurely. Receipt acceptance establishes
order execution; it does not imply completion of a building's later work.
Accepted construction projects count as outstanding work before a building exists.
Duplicate placement suppression and Numbi's desired-building counts include those
projects as well as submitted private intent. This corrects earlier duplicate
requests, including at zero delay; strategy thresholds and selection cadence stay
unchanged.

JavaScript memory updates run in the same ordered stream as decisions, once per
logical observation tick. Unpolled replica and replay controllers retain the old
post-step visibility-history boundary through an explicit owner barrier and a
fresh frozen observation. Never mutate their remembered terrain concurrently with
a decision. Worker diagnostics are buffered values; file output, field publication
and shared telemetry updates belong to the owner at the delivery boundary.
Diagnostic field reservations count outstanding captures and publication-copy
headroom against one session budget across the delay horizon. Saved pending field captures, diagnostic text and named telemetry share a 128 MiB
retention budget when restored. Excess presentation output is consumed and discarded;
orders and resource-field enrollment planes remain intact. Repeated enrollment planes
share one restored allocation after their initialization identity and contents match.

Save barriers finish outstanding computation without delivering future orders
early. Format 143 retains the match delay, completed pending command bytes and
deadlines, request sequences, execution feedback, controller RNG and private
continuation state. Older supported saves load with delay 0 and an empty engine
order queue; the save compatibility floor remains 58. Lifecycle changes cancel
requests by controller generation. Verify delay 0 and 8, rejection and identity
reuse, serial/threaded execution, and save/load with outstanding work. A snapshot
migration alone does not establish identical AI trajectories or performance;
compare per-tick checksums and measure capture, retention and worker costs. Delay 0
retains each controller's strategy and cadence, apart from the explicit pending
intent, rejection and identity fixes listed in the
[replay guide](../development/headless-replays.md).
