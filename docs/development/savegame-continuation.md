# Saved-game continuation

Save-format version 91 preserves the live Mersenne Twister state and routing state in saved games. Keeping only the original seed restarts the random sequence on load. Rebuilding cached gradients also changes unit decisions: these fields intentionally lag map edits until their scheduled refresh.

New saved games retain:

- The canonical 624-word Mersenne Twister state, written as fixed-width integers.
- Immobile-unit occupancy, clearing reservations, both fog buffers and the active buffer.
- Allocated resource, forbidden, guard and clearing gradients, including refresh scheduling flags.
- Building gradients, dirty flags, refresh ticks and cached access results.

Map files do not contain this runtime section. Older saves remain readable and use the previous seed-based initialization; their missing historical state cannot be recovered. Version 91 saves require a version 91-capable reader. The live RNG is installed only after successful loading, and replacing the saved player header with the same seed preserves it.

`savegame-safety-test` compares 300 subsequent simulation steps against uninterrupted play, for human and AI players, after advancing the random sequence and loading a mid-game checkpoint. It also checks 2,000 future RNG outputs, text round trips of the routing section, and rejection/reuse after truncated or invalid runtime sections. The normal save loader and production simulation execute these checks.

The earlier Windows OpenGL “background bleed-through” observation was the existing cloud overlay. With the same build and saved game, setting only `cloudMaxAlpha=0` removed the moving shapes. No production renderer change was required. `map-render-resize-test` additionally checks opaque rectangle and alpha-map drawing after a menu background and cached-frame presentation.

## Compact saves

Format 128 changes storage without changing simulation state. The save floor remains
58 and the replay floor remains 127. Network protocol 51 requires peers that can
read compact map snapshots; old clients cannot load newly written files. Loading
an older supported save and saving normally upgrades it. Existing files are not
rewritten in place as a migration.

Binary maps store tile attributes in separate arrays. Runtime arrays preserve
allocated-versus-absent routing fields, stale distances, dirty flags, topology
stamps, both fog buffers, reservations and pending gradient publication state.
No routing field is discarded or recomputed to reduce file size. All script
identity counters survive, including nonzero counters in unused entity/team slots;
the binary identity table stores sorted nonzero index/value pairs.

`PackedArray` stores at most 4,096 unsigned values per block, with a one-byte tag
and a four-byte payload length. Tag 0 is raw big-endian values, tag 1 is a single
constant value, and tag 2 is a first value followed by modular differences. The
latter uses width-specific zigzag unsigned varints, with token zero followed by a
positive varint run length for repeated values. The writer selects the shortest
payload, preferring raw on ties. Counts come from the containing checked schema;
readers reject oversized/truncated payloads, overlong varints, overflowing runs,
unknown tags and unused payload bytes.

Statistics and telemetry retain every sample and its original bits, timestamps,
validity and descriptors. Histories transpose batches of at most 256 canonical
records into 32-bit columns before packing. Existing record serializers and
validators remain the schema definition. Maxima uses compact numeric arrays and
stores a canonical neighborhood table by dimensions only after an exact content
comparison; noncanonical tables are retained explicitly. Empty/unallocated table
state is preserved in the compact representation.

Text streams retain the readable scalar layout. Gzip level, extensions, atomic
replacement are unchanged. Interactive saves defer final encoding, hashing and
compression to owned background jobs; a busy autosave writer defers the next
capture without blocking gameplay. Manual/editor dialogs stay pending through
write and browser persistence completion, including during normal session exit.
Failures retain the dialog for retry or cancellation; a completed local file can
also be exported after a browser-storage failure. Names and editor dirty state
change only after the complete operation succeeds. Threadless builds yield
between bounded finalization steps. These scheduling changes do not alter the
format-128 bytes; see the
[persistence implementation](reference.md#native-simulation-memory-and-cpu-comparisons)
for snapshot ownership and offset relocation rules.

Size comparisons must use final gzip files, not only the size of the intermediate
serialization. Use `save-size-harness` and `test/measure_save_sizes.py` as described
in the test guide.
