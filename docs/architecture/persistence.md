# Save capture and background persistence

Save capture preserves simulation continuation; encoding and durable persistence have separate ownership and failure handling.

## Serialized representation

Unit format 153 embeds the immutable resolved catalog before live units, building
interaction compilation and AI state. It stores explicit assignment purposes and
exact extended cargo packets. Current race tables must match that authoritative
catalog. Formats 58–72 bind saved BaseTeam race tables before reading units;
formats 73–152 recover the later Team race tables through staged migration.
Both paths retain cached unit performance, hunger, learning and pending service state.
Additional production counters are counted by actual catalog size; historical
three-choice recipes retain their saved cost overrides. See [unit definitions](units.md).

Packed statistics records support rows up to 1 MiB. Rows through the historical
65,544-byte limit keep their original 256-record wire batches. Wider unit tables
use smaller batches to keep raw transpose storage below the same 16 MiB ceiling;
immediate and deferred encoders use identical boundaries.

Memory-only representation changes must preserve legacy serialized widths and
sentinels. Maxima's obstacle-free distance fields use 16-bit storage with an
internal 65535 infinity, translated to the existing signed 32-bit `INT_MAX` on
save/load. Route distances retain their wider representation. Food source masks
retain their legacy 64-bit encoding. Wrapped nine-cell geometry tables share
immutable storage by map dimensions; loaded noncanonical tables retain their
original content. Weighted gradient searches share an immutable water snapshot;
water-classification changes invalidate the map's current snapshot while ongoing
searches retain their frozen version. Growth overlap counters use 16-bit storage;
removal precedes addition so both generations cannot temporarily exceed the bound.

Game-file persistence uses move-only snapshots in uninitialized 1 MiB blocks.
Growing the block table does not copy the saved bytes; allocated byte capacity is
at most the snapshot length plus one block (with a separate small pointer table).
Gzip loading reads compressed input in 64 KiB batches and inflates directly into
these blocks. CRC/truncation/trailing-data validation completes before loading
headers or simulation state. Filename-based custom and campaign initialization
reuse one validated input for both headers and the body, then release it before
replay/network setup. Standalone header readers retain their existing interfaces.

Interactive saves capture owned literals and bounded array/history batches at a
consistent game boundary. A lightweight fixed integer representation bounds the
capture memory; final array encoding and history transposition run on the worker.
Final output uses chunked storage, with relocated header offsets and SHA1 ranges;
headless callers can still serialize synchronously without changing saved bytes.
A captured `DeferredStream::Snapshot` is consumed once: finalization releases its
owned inputs as their output is produced. Append deferred fields in stream order;
seeks may only backpatch fixed-size literals. Serialize stream positions with
`OutputStream::writeOffset32`, which explicitly registers relocation on deferred
streams and writes an ordinary uint32 on binary/text streams. Field names do not
control relocation. SHA1 still covers the original header followed by the final
body, preserving the existing pre-backpatch hash contract.

Worker gzip compression uses bounded 256 KiB output buffers; cooperative gzip
uses 64 KiB input/output steps. Neither flushes at input-block boundaries.
Optional level-zero compression retains the legacy whole-buffer path to preserve
zlib's stored-block byte layout; it is outside the normal-save memory bound.
Background finalization does not add a wire-format change beyond compact format
128 (save floor 58, replay floor 127, network protocol 51).

## Save lifecycle

Autosave defers capture while a previous writer is busy, then captures the current
tick when the writer becomes idle. It never queues a second owned snapshot or
waits for compression during a game tick. Manual game and editor saves keep their
dialog pending while waiting for the worker, writing the file, and persisting
browser storage; names and editor dirty state change only after success. Editor
mutation is disabled while saving. A pending save dialog cannot be replaced by
another panel. Normal session exit stops simulation and keeps presenting frames
and polling the dialog through queued capture, file writing and browser storage
completion. A failed save remains actionable for retry/export or cancellation;
exiting does not silently discard that dialog.

Native and threaded-browser jobs finalize arrays, transpose histories, hash,
compress and replace files on the worker. Threadless builds advance bounded
encoding and compression steps with a two-millisecond polling budget (individual
steps can exceed the budget); snapshot capture still occurs synchronously.
Worker-start failures fail the save rather than running encoding synchronously.
Allocation, serialization and worker-finalization failures retain the previous
file and allow subsequent writes. Browser persistence occurs after local atomic
replacement: if it fails, the new local file remains available for export while
the previously persisted browser copy remains intact. A retry creates a new save
operation; each operation's terminal state is sticky and its success callback
runs once. Other background string writers still keep the newest queued snapshot.

## Pending team statistics

Format 155 saves the live event collector separately from published statistics.
The save barrier joins pending statistics jobs without changing their publication
ticks, then serializes their completed raw samples, FIFO deadlines and sampling
phases. Restoring these results needs no historical world snapshot. Future
publication proceeds at the original deadlines, including after resaving.
Format 156 omits the redundant snapshot input digest; loading format 155 reads
and discards that field. Routine checksums cover pending identities; full checksums
join and cover every completed sample, including restored results. World-state
checksums retain their existing coverage. Neither check publishes early.
Older saves remain readable and migrate to shared snapshot timing at delay zero;
this is an intentional simulation-continuation change. Ending a match leaves
pending samples unpublished; reporting uses the already published histories.

Format 157 marks pending aggregates as worker-finalized. Formats 155/156 have raw
aggregates: loading retains that mode, and their publication performs the original
exact reduction from the consecutive owner ring. Resaving keeps the mode until
those rows publish; new submissions use the frozen published-window calculation.
No historical window is needed to restore completed jobs. The minimum readable
save version remains 58. Loading does not append an extra sample.

Format 158 marks pending measurement frames as complete (cumulative labour and
carried spatial diagnostics included). Loading formats 155–157 completes their
raw measurement deltas in source order from the published state without advancing
publication. Their aggregate-finalization mode remains unchanged, preserving
older pending exact smoothing. Worker continuation is seeded from the last
completed pending frame, or published measurements when the queue is empty.
Resaving stores complete frames; no historical snapshots are required.

Format 159 persists cached turret targeting lists and delayed outputs. Older saves
load with an empty turret cache, which the normal delayed update path populates. Format 163
stores only turret identities and entries; formats 159–162 load by discarding their
source signatures, digests and building targeting epochs.
Maps and saves retain the version-58 compatibility floor; replay acceptance remains
a separate boundary.
Text statistics saves isolate pending teams, sampling phases and coverage anchors
in indexed sections so distinct records survive loading. Binary section markers
carry no bytes. See [turret targeting](../development/compute-benchmarks.md#cached-turret-targeting)
for its snapshot, checksum and continuation boundaries.

## Entity storage and serialized values

Entity field compaction does not change explicit stream encodings, field order,
checksum order or the inherited save compatibility floor. Unit loaders read
narrowed fields into full-width temporaries, apply existing validation and legacy
migrations, and only then convert to their in-memory storage. In particular,
invalid values such as 256 must not wrap into valid byte enums or training levels.
Learning flags retain the historical nonzero-to-boolean loading behavior and are
written individually in the existing order. Their unsigned mask is reconstructed,
with unused bits cleared on initialization and loading, rather than serialized.
`isDead` remains signed 32-bit because loading historically accepts values beyond
zero and one. Building loaders validate lifecycle values, recruitment levels and
inside capacity in wide temporaries before narrowing; capacity remains unsigned
and accepts 0 through 65535. Bounded identifiers and preferences likewise validate
wide values before conversion. Priority, staffing, failure counters, demand and
exchange masks retain their existing full-width accepted values.

Presentation animation clocks still initialize to zero on load rather than
acquiring new saved fields. The compact terrain-health remainder preserves its
signed continuation debt; accumulation and division occur before converting the
remainder. Canonical-save comparisons must use baseline-generated resaves, since
existing historical migrations can already normalize an input file.

Cached unit performance deliberately changes continuation in format 160. Each
ability remains a signed 32-bit serialized value in the original field order,
while the live cache clamps to 0–65535. Format 153 and newer retain their existing
0–1000000 validation and applicable nonzero-HP validation before clamping; invalid
inputs are still rejected. Accepted negative caches in older formats normalize
to zero. Catalog and per-team race performance remain wide. Newly computed HP
performance applies its rule division before the final cap; loading retains the
saved cache rather than recomputing it from the catalog. Actual HP, hunger, speed,
movement surplus, experience and timers retain their wide continuation values.

Private building stock deliberately changes continuation in format 160: every
loaded slot is clamped to 0–255 before byte storage. Historical negative stock
normalizes to zero. Pre-catalog repair credits retain their wide migration before
actual stock is selected. Serialized stock remains fifteen signed 32-bit values
in its original order. Maps and saved games retain the version-58 compatibility
floor, including these normalization rules for older saves.

Only buildings whose private stock was normalized reconcile commitments made
unaffordable by the cap. Affordable service visits survive; unfunded visits exit
without receiving food, healing or training. Unfunded committed production is
canceled. A canceled entrant completes its entry phase before exiting, preserving
the saved entrance occupancy until the normal entry action clears it. Canceled
visits clear their service timer so terrain and area effects resume after exit.
Format 161 also defers explorer service rebound until a free exit is found. Its
replay floor is 161; saved games from formats 159 and 160 remain loadable.
Construction retains its wide budget and progress, while funding is
limited to remaining backed stock and premature completion is cleared. Existing
service/production/construction restoration priority and shared-stock validation
remain intact. Cached demand is preserved rather than recomputed on load.

Runtime additions saturate private stock without wrapping. Deliveries accept only
available headroom and account for actual acceptance or residual cargo; consumption
settles existing reservations. Team stock keeps signed 32-bit storage. The stock
and cached-performance policies change simulation revision and replay/network
acceptance; format 159 saves
remain readable, while earlier replay trajectories are not promised.

GPU execution services and numerical test oracles are not saved. Simplifying
their lifecycle does not change the version-163 encoding or version-58 save
compatibility floor; completed pending fields retain their publication deadlines.
