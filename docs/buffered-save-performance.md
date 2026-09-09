# Buffered save output

FileManager output streams batch field-sized writes in a bounded 16 KiB buffer.
Large writes bypass it after draining earlier data. Positions include pending
bytes; reads, seeks, flushes, and destruction drain in order. Atomic saving
retains checked write/flush/seek/close errors before replacing the previous file.
The serialization format, fields, SHA1, and autosave cadence remain unchanged.

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
large writes, backpatching, reads, seeks, flushes, and destruction. Existing
save-safety tests check atomic/direct byte equivalence, reloading checksums,
write-failure preservation, and truncated input. Linux CI runs both harnesses;
existing Windows save-safety coverage exercises the atomic path. This PR
contains no Cortex changes.
