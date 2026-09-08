# Global gradient performance evidence

Measurements on 2026-09-07 compare `master` at `b47c7d23b`, the global sweep
simplification, and that simplification plus the no-source fast path. No local
traversal rewrite, seed cache, refresh scheduling change, or runtime profiling
code is part of the production patches.

## Clean-patch measurements

| Game | Baseline core CPU | Kernel only | Kernel + fast path | Combined reduction |
|---|---:|---:|---:|---:|
| G2 | 4.134 s | 3.614 s | 3.510 s | 15.1% |
| playground-8numbi | 3.981 s | 3.430 s | 3.108 s | 21.9% |

Each number is the median of three serial 15,000-tick runs on an Apple M3
(8 cores, 24 GiB RAM), compiled with Apple Clang and `-O3`. The Playground case
uses eight Numbi-controlled teams, seed 424242, plus the passive local controller
on team 0 required by headless setup. It approximates the reported eight-player
filled-AI workload; it is not the original profiling session's saved game.

The core metric is thread CPU time around advancing `Game::syncStep` calls.
Temporary instrumentation was identical in all three timed binaries and is
absent from the production commits. This scope includes teams, units, buildings,
map updates and scheduled world logic; it excludes AI order generation, order
execution, startup and replay/checksum output. Whole-process elapsed samples
are retained separately and are not interchangeable with core CPU time.

Runs alternated variant order. The runner waited for other compiler/game jobs
to settle and rejected runs with those jobs present. This does not eliminate OS
noise or CPU-frequency variation. No hardware cache-miss counters were measured.

[Raw samples and validation digests](global-gradient-results.json) include all
accepted repetitions, exact baseline/patch identifiers, and compiler details.

## Why the changes help

The global sweep uses row pointers and a single maximum over its four directional
neighbors. Subtraction by one preserves neighbor ordering, so only that maximum
needs the propagation threshold and improvement tests. Zero-valued obstacles and
255-valued goals are immutable. The forward/backward in-place order, toroidal
wrapping, value floor and convergence cap remain intact.

The fast path checks whether any input byte is at least 3. If none is, propagation
cannot raise any cell: zero is immutable and neither 1 nor 2 can contribute a
value above a free cell's seed of 1. The existing seed buffer is already final.
This also handles a contributing seed of 3 or 254 correctly; it does not assume
all sources are 255-valued goals.

## Behavior preservation

Both exact production patches and the unmodified baseline ran G2,
`gd-small-2ai`, `gd-large-4ai`, `gd-archipelago`, `gd-bigarena-long`, and the
fixed-seed Playground case for 15,000 ticks each. Each variant produced identical
replay bytes and identical complete per-tick unit/building checksum sidecars:
90,000 ticks per executable. Every timed run also matched its baseline replay.

`GlobalGradientHarness` calls the real production function and compares every
byte against an independent priority-frontier solver. It tests 3,000 fixed-seed
random fields, mixed seed strengths, toroidal seams, diagonals, thin dimensions,
winding obstacles, the distance cutoff and idempotence. The fast-path tests add
inert 0/1/2 fields and contributing seeds at the final input position. Both kernel
variants passed with the harness and gradient translation unit instrumented by
AddressSanitizer and UndefinedBehaviorSanitizer; the remaining linked engine
objects were uninstrumented. Linux CI also builds and runs the harness.

Build the harness from the repository root:

```sh
scons -j8 release=1 server=0 global-gradient-test
./build/src/GlobalGradientHarness
```

To reproduce a replay comparison, build baseline and candidate executables,
then run each from the same checkout/assets with distinct absolute output paths:

```sh
GLOB2_CHECKSUM_SIDECAR=1 GLOB2_REPLAY_PATH=/tmp/baseline.replay \
  /path/to/baseline --nox games/G2.game 15000 1
GLOB2_CHECKSUM_SIDECAR=1 GLOB2_REPLAY_PATH=/tmp/candidate.replay \
  /path/to/candidate --nox games/G2.game 15000 1
cmp /tmp/baseline.replay /tmp/candidate.replay
cmp /tmp/baseline.replay.checksums /tmp/candidate.replay.checksums
```

Use relative input paths with this baseline's `--nox` loader. Generate Playground
once from the baseline with `GLOB2_TEST_SEED=424242`, `-test-games-nox 1`,
`--map Playground`, `--matchup numbi,numbi,numbi,numbi,numbi,numbi,numbi,numbi`, and
`--save-game-as` pointing to an absolute path under the checkout's `games/`
directory. Reuse that saved state for every comparison.

## Earlier exploratory evidence

The preceding experiments used a different base, `c71373bb2`, and runtime variant
switches. Their combined variant also included the local traversal rewrite and
measured 22.0% less core CPU on Playground and 24.1% on G2. Those percentages are
not attributed to either isolated PR or substituted for the clean-patch results
above. That exploration compared 112,517 live global fields and 5,302 local fields
against the original routines, plus 23,000 randomized kernel cases, with no
mismatches. Seed caching had one hit in 16,040 Playground updates and added no
meaningful benefit; local propagation saved only a few milliseconds overall.

These measurements and differential checks establish behavior on the tested
workloads, not a proof for every game or a speedup guarantee on other hardware.
