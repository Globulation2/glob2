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

`ResourceGrowthBenchmark/player-free*` reconciles stock scans with statistics,
using two passive teams and no players, units, buildings or harvesting. Zero-worker/shared
states and statistics must match. `GLOB2_GROWTH_PLAYER_FREE_OUTPUT` selects a JSON
output path and expands testing from two seeds/64 ticks/32² to 20 seeds/512 ticks/64².
`GLOB2_GROWTH_PLAYER_FREE_DELAY` selects 1–16 ticks (default 8) for this test only.
Reports include 128-tick checkpoints and a separate diagnostic terminal flush that
applies pending proposals without calculating more batches. The immediate reference
calls the retained old growth pass, not the latest master executable. Snapshot timing,
within-pass feedback and random streams remain different; configured seed stocks,
material rates and variety follow the original business rules. Heavy checksums join
pending work, so these checks establish correctness and ecology, not throughput.

`ResourceGrowthBenchmark/generated landscapes*` exercises the actual River, Swamp,
Crater Lakes and Islands generators with their default controls and two colonies,
on 128²/256² maps. It removes colony entities for a no-harvesting comparison, keeping
the generated terrain and resource placement. Each execution variant loads the same
serialized starting world. Every tick reconciles material stocks and deposit counts
with growth statistics and compares zero-worker/shared heavy checksums. Set
`GLOB2_GROWTH_GENERATED_OUTPUT` to a JSON path to expand from one seed/32 ticks to
20 seeds/512 ticks per generator (`GLOB2_GROWTH_GENERATED_TICKS` can select a longer
1–16384 tick horizon) and save representative generated worlds with their original colonies,
empty-colony inputs and endpoints beside the report. Results include per-material
stocks, deposit counts, checkpoints and a separate terminal-queue flush. These are
no-player ecology measurements, not throughput or sustainable-harvesting tests.

`GLOB2_GROWTH_GENERATED_DELAYS` selects a comma-separated list of delays from 1–16
(default `8`). `GLOB2_GROWTH_GENERATED_WIDE=1` adds Rain Shadow, Old Growth, Braided
River, Fjord Continent, Stone Highlands, Tidal Flats, Canals and Continents, including
512² and rectangular maps. `GLOB2_GROWTH_GENERATED_CASE` selects one generator ID
for independent process execution. `GLOB2_GROWTH_GENERATED_SEED_BEGIN` and
`GLOB2_GROWTH_GENERATED_SEED_END` select an inclusive seed range for sharding
large-map campaigns; default coverage is unchanged. Refused generation seeds are recorded rather
than replaced; a selected case with no successful samples fails.

For comparisons across incompatible save formats, `GLOB2_GROWTH_GENERATED_INPUT`
selects a directory of exported master fixtures, one subdirectory per generator
and `fixture-<seed>.json` per world. This mode requires case selection and an output
path. It imports classic terrain vertices, the canonical resource catalog and
exact deposit types, varieties and material stocks, then verifies habitat,
growth permission and ecology rates at every cell for all exported renewable
types. Mismatches fail before stepping. Zero-worker/shared arms load identical native
saves of that imported world; the retained immediate-growth pass is omitted in
this mode. Master reference results must come from a separately pinned master
executable using full simulation ticks, and every missing fixture must be
reconciled against its generation-failure report. Report both delay-versus-delay
and delay-versus-master comparisons: only the former isolates scheduling.


Proposal buffers are pooled for the full delay horizon. Initial reservation is
`max(128, cellCount / 16)` entries per buffer, approximately four proposals per
expected sampled cell. Completed batches raise the reservation to their observed
size plus 25% headroom. This is a heuristic, not a guaranteed 99th percentile;
rare overflows grow the vector normally. Headless results expose
`growth_capacityGrowthBatches` and `growth_maxProposals` to measure its coverage.
Capacity and pooling decisions do not affect simulation results.

Resource-growth integration introduced save format 149 and network protocol 67.
The current writer and replay floor are 152, with private entity, map and legacy-story RNG state after area-effect funding and fractional services in format 150. The save compatibility floor remains 58. Released master layouts
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

For attribution, set `GLOB2_GROWTH_EXISTING_CAPTURE=1` when running the paired
`ResourceGrowthBenchmark` case. Every variant then captures the full shared
component union once per observation, including the legacy control; new growth
uses that existing handle. This separates the standalone cost of introducing
snapshots from growth on an already captured world. It does not simulate AI worker
contention or retained AI leases, and old/new growth trajectories still differ.
Compare full-engine capture counts, capture time and copied bytes as well; total
shared capture time must not be attributed entirely to growth. Worker compute,
queue residence and owner wait timings overlap and must not be added as if they
were sequential costs.

The opt-in `identical live and snapshot kernel inputs` case in
`ResourceGrowthBenchmark` isolates read access: it runs the same pure kernel over
live and captured views of the same unchanged map, compares ordered proposals and
RNG continuation, then times paired runs. Capture, input setup and RNG construction
are outside timing. Set `GLOB2_GROWTH_KERNEL_OUTPUT` to an output JSON path to retain
samples. This measures kernel access costs; it does not measure capture, delayed
publication, retention or contention with other engine jobs.

