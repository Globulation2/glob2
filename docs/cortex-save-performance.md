# Cortex placement and save-write performance

Cortex placement searches reuse a snapshot of live building positions, upgrade
reservations, and inn occupancy for one search. Candidate order, scoring, random
number use, and observation cadence are unchanged. The snapshot expires with
the search, so map changes between searches require no invalidation scheme.
Inn crowding uses the union of the existing occupancy mask and four wrapped
clearance strips, retaining the corner rule that occupies two adjacent sides.

FileManager output streams batch small writes in a 16 KiB buffer before calling
stdio. Large writes bypass the buffer after draining earlier bytes. Positions
include buffered bytes, and seeks, reads, flushes, and destruction drain in
order. The serializer, byte order, SHA1 calculation, saved fields, and autosave
frequency are unchanged. Input streams use the existing backend. Atomic saves use the same batching
through their checked writer; write, flush, seek, and close failures still prevent
replacement of the previous save.

## PR-base match checks

The final patch was also built directly on `master` at `b47c7d23`. Original and
modified binaries used the same source revision, flags, map data, and seed.
These full matches ran sequentially without concurrent builds:

| Match | Seed | Ticks | Before | After | Elapsed reduction |
|---|---:|---:|---:|---:|---:|
| SmallForTwo: Cortex, Nicowar | 42 | 18,562 | 10.83 s | 7.95 s | 26.6% |
| A big pond: Cortex, Nicowar, Castor | 43 | 35,906 | 118.91 s | 30.54 s | 74.3% |

Both pairs have byte-identical complete replays, including their initial state:

- SmallForTwo: `524317223299ed21c1e7c9d028b5f5ac7713c1fb77799eceff646ccc07996741`
- A big pond: `49cead91b2e7accb6afa762d7d8b0c36fe1b0c733e59fa777be5d2f224d8adad`

These are single-run measurements and workload-specific, not universal speedups.
Use the integration command below with map/matchup replaced by these entries
(`A_big_pond` is the command-line map name) to reproduce on the PR base.

## Integration benchmark

Measured on Apple M3 / macOS 26.6.2, optimized ARM64 builds, on September 7, 2026.
The integration revision was `c71373bb`, with the changes applied in stages.
That revision supplies Maxima; these performance changes do not require or
include the integration branch's unpublished Maxima commits.

Playground is 128 × 128, with eight teams: Numbi, Castor, Warrush,
ReachToInfinity, Nicowar, Maxima, Cortex, Maxima. Seed 20260907. All runs end at
55,330 ticks with Nicowar winning and 14,426 replay orders. Replay recording,
initial saving, and automatic saving are enabled.

| Variant | Wall seconds | User + system CPU seconds | Retired instructions |
|---|---:|---:|---:|
| Same-source baseline | 119.77 | 117.63 | 2.098 trillion |
| Cortex snapshot | 84.19 | 82.54 | 1.526 trillion |
| Cortex snapshot + buffered writes | 71.19 | 69.56 | 1.217 trillion |

Cortex reduces elapsed time by 29.7%; buffering reduces the Cortex-only time
by another 15.4%. Together they reduce elapsed time by 40.6%, CPU time by
40.9%, and retired instructions by 42.0%. These are single-run observations,
not statistical confidence intervals. Some baseline/Cortex timing overlapped
build activity; instruction counts and exact replay equivalence provide
additional evidence independent of the elapsed-time comparison.

The older executable used for the initial exploratory profile has a different
save version, so its timing is not used as the same-source baseline here.

All three variants produce identical hashes:

| Artifact | SHA256 |
|---|---|
| Full replay | `0cfc845ea611f6d7af0f1f3e769a53788f762397f1aa2ff85449a52aa30c2c18` |
| Initial saved game | `91e09e0f3721119b5fbbe8ce631e09ba023cd3f5fb2730e71f6a6509c889b57c` |
| Final automatic saved game | `c0c6e744c6783759300f7c770748a568d8b1b0c8f9143664eb6aea203cd4217a` |

Run each integration binary from a checkout containing its matching data:

```sh
GLOB2_TEST_SEED=20260907 GLOB2_REPLAY_PATH=/tmp/variant.replay \
  /usr/bin/time -l /path/to/variant/glob2 -test-games-nox 1 \
  --map Playground \
  --matchup numbi,castor,warrush,reachtoinfinity,nicowar,maxima,cortex,maxima \
  --save-game-as /tmp/variant-initial.game
```

Use unique output paths and run variants sequentially. Compare replays and
saved games with `cmp`. On Linux, use an appropriate `time`/performance-counter
invocation rather than macOS's `time -l` options. This measures headless engine
and AI work, not rendering.

## Regression tests

`cortex-geometry-test` compares 57,600 candidate queries with the original
helpers, including seams, grown and empty footprints, construction sites,
existing corner occupancy, dead buildings, and empty colonies.

`buffered-file-test` compares raw bytes and SHA1 against FileStreamBackend,
including small writes, exact buffer boundaries, larger blocks, backpatched
headers, relative/end seeks, reads, explicit flushes, and destructor flushing.
The existing `savegame-safety-test` exercises the file-manager save path.

See `test/README.md` for build/run commands.
