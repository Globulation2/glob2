# Save capture and background persistence

Save capture preserves simulation continuation; encoding and durable persistence have separate ownership and failure handling.

## Serialized representation

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
