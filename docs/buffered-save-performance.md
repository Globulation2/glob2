# Buffered save output

FileManager output streams batch field-sized writes in a bounded 16 KiB buffer.
Large writes bypass the buffer after draining earlier data. Positions include
pending bytes; reads, seeks, flushes, and destruction drain in order. Atomic
saving retains checked write/flush/seek/close errors before replacing the
previous file. Gradient fields go out as one run of bytes each
(`OutputStream::writeUint16Sections`), with the same bytes and SHA1 as writing
each value in its own section. The serialization format, fields and SHA1 are
unchanged. An autosave leaves its whole-file SHA1 for the writer thread:
`Game::save` records the hashed range and the header bytes as first written in a
`DeferredGameSHA1`, and `DeferredGameSHA1::apply` stores the same hash an inline
save computes before the file is written. `Engine::haveMap` uses that hash when a
joining client decides whether its local copy matches the host's file.

Autosave runs every `AUTOSAVE_INTERVAL_TICKS` (256) ticks at normal speed, first
on tick 79 of a session, and waits proportionally more ticks at faster speed
presets so saves stay about 10 seconds of real time apart. Players can turn it
off under Settings → Gameplay. The game is serialized between ticks into a
`MemoryStreamBackend`, and `BackgroundFileWriter` writes the file on a worker
thread, which hashes the snapshot first; a newer snapshot replaces one that has
not started writing, and one queued during a write is written right after it. The engine
waits for a pending write before a session ends, before an in-game save, and
when `GameGUI` is destroyed.

On Apple M3, optimized integration revision c71373bb, an eight-team Playground
match (seed 20260907) took 84.19 seconds with Cortex geometry optimization and
71.19 seconds after additionally enabling buffering: 15.4% less elapsed time.
Retired instructions fell from 1.526 trillion to 1.217 trillion. Cortex was
held fixed in this comparison; it is not a dependency of this PR. The replay,
initial save, and final autosave were byte-identical. These are single-run,
workload-specific observations, with some build activity during the first run.
The integration's unpublished Maxima commits are not included here.

The standalone buffering patch on master, with original Cortex code, also
produces the original complete SmallForTwo replay for seed 42 (Cortex/Nicowar):
SHA256 524317223299ed21c1e7c9d028b5f5ac7713c1fb77799eceff646ccc07996741.
This split verification is a correctness check, not an isolated timing claim.

Build `scons release=1 buffered-file-test savegame-safety-test`, then run:

```sh
./build/src/BufferedFileStreamHarness
python3 test/run-savegame-safety-tests.py build/src/SavegameSafetyHarness
```

The stream harness checks byte and SHA1 equivalence across buffer boundaries,
large writes, backpatching, reads, seeks, flushes, and destruction, and that
memory streams overwrite, append, zero-fill seek gaps and hand over their
contents. The save-safety tests check atomic/direct byte equivalence, reloading
checksums, write-failure preservation, truncated input, background writes,
disabled autosave, a deferred SHA1 matching an inline one when the header
backpatch changes hashed bytes, and `Engine::haveMap` trusting a local save only
when its SHA1 matches. Linux CI runs both harnesses;
existing Windows save-safety coverage exercises the atomic path. This PR
contains no Cortex changes.
