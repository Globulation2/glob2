# Scheduled resource growth

Resource ecology runs from captured inputs and publishes at fixed logical deadlines. These rules affect gameplay and save continuation.

## Delayed resource growth

Natural growth reads the completed-tick engine observation through `MapState::View`.
The snapshot store captures resource, occupancy, terrain, ecology, catalog and rule
requirements together with AI and periodic gradients. One batch per advancing tick
uses one owner-drawn seed and a private MT19937 stream; calculation never reads or
writes live map state. Every attempt sees the original snapshot, so proposals do
not seed further growth within the same pass.

The shared compute executor runs whole batches concurrently. Publication occurs
at `snapshot.tick + delay`, after team stepping, with eight ticks as the default.
A late worker is joined at its deadline. With zero workers, the executor runs
the same calculation on the simulation owner at the join. The mutation pass visits the compact proposal list rather than scanning
the map. Each 12-byte proposal stores its tile, resource type, source variety and
operation. Replenishment carries one signed material delta. A seed carries
worker-calculated replenishment choices for a matching destination at publication.
If the destination remains empty, it receives the configured initial stock of every
material and the source variety, just as the immediate growth pass did. Seed stocks
do not depend on replenishment rates. If a matching deposit is already present,
only the precomputed replenishment increments apply, capped independently; initial
stocks never replace existing stocks. Publication makes no new random choices.

There is no incarnation tracking. A positive replenishment whose deposit disappears
can create a new deposit with configured stocks. Publication retains growth
permissions, habitat and empty-destination occupancy checks. Disabling growth rejects
a complete due batch; incompatible resource catalogs or worlds invalidate it.
Mutations use authoritative resource setters and preserve cache invalidation.

## Statistics

Stock statistics record actual added/removed units. As in immediate growth, a seeded
tile is credited to every material with positive starting stock; those per-material
tile counters must not be summed to count physical deposits. Diagnostic `tilesAdded`
counts physical deposits. Global growth measurements are duplicated into each team;
they must not be summed across teams. With no teams, measure world stocks directly.

Diagnostic `growth_proposals` counts newly computed work, including future batches.
`growth_publishedProposals` includes restored work and equals accepted plus rejected
proposals. One accepted seed can add multiple stock units. `growth_clamped` counts
wholly rejected positive proposals with attempted replenishment; partially accepted
seed collisions may contain saturated materials without increasing this counter.
Diagnostic counters restart on load; team statistics retain their saved history.

## Buffering

Proposal buffers are pooled for the full delay horizon. Initial reservation is
`max(128, cellCount / 16)` entries per buffer, approximately four proposals per
expected sampled cell. Completed batches raise the reservation to their observed
size plus 25% headroom. This is a heuristic, not a guaranteed 99th percentile;
rare overflows grow the vector normally. Headless results expose
`growth_capacityGrowthBatches` and `growth_maxProposals` to measure its coverage.
Capacity and pooling decisions do not affect simulation results.

## Save continuation and configuration

Resource-growth integration introduced save format 149 and network protocol 67.
The current version gates live in [Version.h](../../src/app/Version.h) and [ReplayReader.h](../../src/replay/ReplayReader.h). The save compatibility floor remains 58. Released layouts
146–148 and historical growth-draft layouts with the same numbers are resolved
before loading game state: the map catalog or a bounded terrain-block probe
identifies vertex versus legacy corner storage. Historical growth layouts retain
their raw proposal version while older terrain, routing and AI-memory readers
use the pre-vertex layout. Format 149 stores scheduled building-gradient state
followed by pending growth outputs; loading never publishes either queue early.

Historical growth gates in `FileFormatVersions.h` intentionally overlap released
artwork/terrain versions. Resolve the layout before interpreting those gates;
`FILE_FORMAT_VERSION_INTEGRATED_RESOURCE_GROWTH` identifies the unambiguous format 149.

The historical growth draft format 148 added typed proposals and restored configured natural seed stocks.
Pending unit proposals from formats 144–147 retain their old mutation semantics until
their existing deadlines, including through resave; new batches use the restored rules.
The historical growth draft format 147 combined embedded map/building artwork and pending growth. Older formats
remain readable.
Released format 145 adds building artwork; its bounded header is distinguished
from the earlier compact-growth format 145, including empty artwork. Two
independent formats used version 144: released artwork saves have an artwork
length before map arrays and no growth queue; the earlier growth prototype has a
packed map-array header there, incarnation counters and pending material masks.
The loader distinguishes their bounded headers (or named text fields), discards
legacy incarnations, and converts masks to positive deltas in material order.
The durable save floor remains 58. Saves without pending growth start with an
empty queue. Saving
drains computation without publishing early. Registry/world replacement cancels
pending batches. Routine checksums include seeds and deadlines without waiting;
heavy verification joins and includes proposal contents. Worker placement is local
execution configuration; delay and pending output are simulation state.

`--resource-growth-delay 1..16` controls the headless publication delay.
Growth always uses the shared executor. `--compute-threads 1` leaves zero workers
and exercises its fallback; larger values include the owner plus worker threads. A loaded queue cannot change delay while work is pending.
No growth passes are scheduled while regrowth is disabled. Disabling regrowth also
rejects pending proposals when they reach publication.


See [growth verification](../development/growth-verification.md) for ecology fixtures, kernel attribution and worker comparisons.
