# Local memory churn investigation

Commit: `80aba7aca`. macOS arm64, optimized client at
`build/darwin/client/release/src/glob2`. No tracked source changes.

The local `churn.c` dylib interposes malloc, calloc, realloc and free, captures
five caller frames for each successful allocation, and writes cumulative per-site
counts and requested bytes at process exit. `report.py` subtracts call-stack
counts between two deterministic runs; `groups.py` attributes each stack to a
source subsystem. This subtraction isolates added simulation ticks from startup
and teardown that are common to both runs. All traces and replays here are
development artifacts, not committed documentation.

Build:

```sh
scons -j6 release=1 server=0
clang -O2 -dynamiclib -o artifacts/churn/churn.dylib artifacts/churn/churn.c
```

For each map and tick cap, run the command below with the listed values. Use an
absolute output path for each trace and replay.

```sh
GLOB2_TEST_SEED=2287 GLOB2_TEST_MAX_TICKS=20000 \
GLOB2_CHURN_OUT=/Users/bradley/glob2/artifacts/churn/play-20000.tsv \
GLOB2_REPLAY_PATH=/Users/bradley/glob2/artifacts/churn/play-20000.replay \
DYLD_INSERT_LIBRARIES=/Users/bradley/glob2/artifacts/churn/churn.dylib \
build/darwin/client/release/src/glob2 -test-games-nox 1 --map Playground \
--matchup nicowar,warrush,econo,castor,numbi,cortex,cabino,nicowar \
> artifacts/churn/play-20000.log 2>&1
```

Run `Playground` at caps 1000, 5000, and 20000 with seed 2287. Run
`SmallForTwo` at the same caps with seed 1441 and matchup
`nicowar,warrush`, using trace names `1000.tsv`, `5000.tsv`, and `20000.tsv`.

Analyze:

```sh
python3 artifacts/churn/groups.py artifacts/churn/play-5000.tsv artifacts/churn/play-20000.tsv
python3 artifacts/churn/report.py artifacts/churn/play-5000.tsv artifacts/churn/play-20000.tsv
python3 artifacts/churn/groups.py artifacts/churn/5000.tsv artifacts/churn/20000.tsv
```

## Results, ticks 5000–20000

| Scenario | Allocations | Requested bytes | Frees | Realloc calls |
| --- | ---: | ---: | ---: | ---: |
| Playground (8 teams, mixed AI) | 1,052,818 | 1,267,534,448 | 1,052,818 | 0 |
| SmallForTwo (Nicowar/Warrush) | 264,117 | 56,862,583 | 264,117 | 0 |

Playground allocation volume by stack attribution: building gradients 536 MB
(42.3%); Cortex water/reachability 346 MB (27.3%); Cortex wheat scan 175 MB
(13.8%); Cortex placement 57.7 MB (4.6%); Nicowar defense scan 27.4 MB
(2.2%). The remaining stacks include order dispatch, team tasks, statistics,
and other work. SmallForTwo: building gradients 36.9 MB (65.0%).

Specific hot sites in Playground: `Map::roundTripGradient` allocates a
32 KiB field 4,403 times (144 MB); Cortex wheat component scans grow deque
blocks 26,513 times (109 MB); `Map::pathfindBuilding` allocates fields 2,159
times (70.7 MB); the Cortex full-map BFS allocates repeated 32 KiB distance
fields and expanding frontier buffers. `BuildingGradientSearch::resolve`
grows its bucket vectors by many 1 KiB chunks, accounting for much of the
remaining gradient call count.

At 5,000 ticks, the instrumented and uninstrumented Playground game both
reported checksum `deb7f93f` and 886 orders. The cap is a measurement window;
neither game reached a winner. The shim counts requested allocation bytes, not
peak resident memory or allocator overhead. It observes the standard C heap
path, including C++ new/delete backed by it, but does not instrument VM-only
allocations. Stack-based categories are approximate and include nested callers.
The direct C `realloc` count was 11 for every whole process; vector/deque
growth appears as separate allocation and free calls, so the zero delta in
`realloc` calls does not mean there was no container growth.

## Bounded reuse prototype

The source patch pools idle and invalidated full-map `Uint16` buffers in `Map`
(64 slots, up to 2 MiB per map) and detached `BuildingGradientSearch` objects
with their retained bucket capacities (16 slots, up to 2 MiB globally, cleared
when a map clears). Active fields retain their existing in-place refresh rules.
Construction, reload and teardown still directly destroy building-owned fields.
The first prototype pooled only idle fields and cut just 5.2 MB in the Playground
midgame window; free stacks identified live invalidation resets as the dominant
release path. `free-report.txt` holds the ranked release stacks.

The final prototype was built headlessly at `build-software/src/glob2` with
`scons -j4 release=1 server=0 opengl=0 wss=0 --build=build-software`.
Run the same pinned seeds, maps, matchups and 5,000/20,000 tick caps above,
with the trace names `search-play-5000.tsv`, `search-play-20000.tsv`,
`search-small-5000.tsv`, and `search-small-20000.tsv`. The original optimized
client binary was copied to `glob2-baseline` before building the prototype.

| Scenario, ticks 5,000–20,000 | Baseline allocations | Prototype allocations | Baseline requested bytes | Prototype requested bytes |
| --- | ---: | ---: | ---: | ---: |
| Playground, 8 teams | 1,052,818 | 902,926 (−14.2%) | 1,267,534,448 | 782,713,088 (−38.2%) |
| SmallForTwo, 2 teams | 264,117 | 240,829 (−8.8%) | 56,862,583 | 22,517,943 (−60.4%) |

Playground building-gradient attribution fell from 536 MB to 122 MB; SmallForTwo
fell from 36.9 MB to 4.7 MB. The final checksums and order counts matched for
both headless scenarios. These are allocation volumes, not peak memory saved.

The structured Playground comparison used `--run-game`, game seed 2287, eight
AI players in the same order, 20,000 ticks, one compute thread, saves at every
5,000 ticks and final, and `--telemetry checksums`. The checksum artifact and
all four checkpoint saves plus the final save were byte identical between the
pre-prototype and prototype binaries. Loading the baseline 10,000-tick save
into the prototype produced the same resumed final save as loading it into the
baseline binary. Both resumed runs differ from the uninterrupted final save;
that continuity discrepancy exists on the baseline too and is not caused by
this prototype.
The legacy `-test-games-nox` replay files differed in their header bytes, so
raw replay equality was not used as the behavior check.

`BuildingGradientInvalidationHarness`, `PathGradientHarness` (1,526 cases),
and `SavegameSafetyHarness` passed after the final code change. macOS arm64
was the only platform measured. A 20,000-tick eight-team run had peak RSS near
72.7–73.0 MB on the baseline and 74.7–75.5 MB on the prototype. Alternating
wall times varied substantially, and the binaries used different headless
rendering/network build flags, so these runs do not establish a speed change.

## Cortex scratch reuse prototype

The subsequent patch keeps water reachability fields and their BFS frontier,
wheat depth/queue/component buffers, and the placement proximity mask in
thread-local scratch vectors. Each scan resets logical contents before use;
worker threads retain independent capacities. The wheat FIFO now drains a
vector by index instead of allocating `std::deque` blocks. The unmodified
gradient-prototype binary is `glob2-gradient-prototype`.

Both profiled runs used the same Playground seed, eight-player matchup, and
5,000/20,000 tick caps above, now with `build-software/src/glob2` and trace
names `cortex-play-5000.tsv` and `cortex-play-20000.tsv`. Run
`python3 artifacts/churn/groups.py artifacts/churn/cortex-play-5000.tsv
artifacts/churn/cortex-play-20000.tsv build-software/src/glob2` to reproduce the
attribution.

| Ticks 5,000–20,000 | Gradient-only prototype | With Cortex reuse |
| --- | ---: | ---: |
| Allocation calls | 902,926 | 816,876 (−9.5%) |
| Requested bytes | 782,713,088 | 215,251,752 (−72.5%) |
| Cortex water/reachability bytes | 345,897,984 | 311,936 |
| Cortex wheat scan bytes | 174,843,452 | 563,684 |
| Cortex placement bytes | 57,719,976 | 10,124,456 |

Together the three Cortex categories fell from 578,461,412 to 11,000,076
requested bytes (−98.1%). The remaining placement category includes geometry
snapshot and candidate data outside the reused mask. Building gradients still
account for 121,507,056 bytes, so the combined patch is 83.0% below the
original 1,267,534,448-byte baseline. These are allocation volumes, not peak
memory or speed measurements. Thread-local vectors retain capacity until their
worker thread exits; large maps or many workers can raise resident memory.

The profiled 5,000 and 20,000 tick runs matched the prior checksums and order
counts. `structured-cortex-final/` has a 20,000-tick, single-compute-thread
run with the same structured options as `structured-proto/`: their uncompressed
per-tick checksum files and all four checkpoints plus final save compare byte
identically. `structured-cortex-resume/final.game.gz` matches the earlier
`structured-resume/final.game.gz` when loading the pre-Cortex 10,000-tick save.
The 5,000-tick four-compute-thread final saves in `structured-gradient-4t/`
and `structured-cortex-4t/` also compare byte identically.
`CortexGeometryHarness` passed 184,704 candidate comparisons. Only macOS arm64
was exercised; cross-platform checksum coverage remains outstanding.
In one concurrent pair of uninstrumented 20,000-tick runs with identical build
flags, peak RSS was 76,513,280 bytes for the gradient-only binary and
77,348,864 bytes with Cortex reuse (+835,584 bytes); wall times were 11.59
and 11.60 seconds, respectively. One pair is insufficient to claim a speed
change.

## Nicowar and gradient-search pool follow-up

`NewNicowar::compute_defense_flag_positioning` now resets and reuses its three
full-map `Uint16` arrays per worker thread. A temporary
`GLOB2_GRADIENT_POOL_STATS=1` diagnostic measured search-pool hits, misses,
rejections, and retained-capacity peaks; it was removed from the source after
choosing the pool size. With the previous
16-slot, 2 MiB search pool, a 20,000-tick Playground run reported 1,431 slot
rejections and zero byte-cap rejections (the first 5,000 ticks had none).
The search pool was tested at 32 slots/4 MiB and 64 slots/8 MiB, and the
64-slot setting was retained in source.

Playground used the same seed, map, matchup and tick caps above, with the
`nicowar-play-*.tsv`, `pool32-play-*.tsv` and `pool64-play-*.tsv` traces.
`glob2-cortex-prototype` is the binary before these changes;
`glob2-nicowar-prototype` includes Nicowar reuse and pool counters with the
16-slot pool; `glob2-pool64-prototype` is the 64-slot build. Run `groups.py`
with each trace pair and the matching binary to reproduce attribution.

| Playground, ticks 5,000–20,000 | Cortex prototype | Nicowar + 16 slots | Nicowar + 32 slots | Nicowar + 64 slots |
| --- | ---: | ---: | ---: | ---: |
| Allocation calls | 816,876 | 816,038 | 768,841 | 747,962 |
| Requested bytes | 215,251,752 | 187,733,992 | 124,194,080 | 95,021,336 |
| Building-gradient bytes | 121,507,056 | 121,507,056 | 57,957,296 | 28,489,696 |
| Nicowar defense bytes | 27,444,872 | 214,664 | 214,664 | 214,664 |
| Pool misses at 20,000 ticks | not measured | 1,572 | 544 | 199 |
| Pool slot rejections at 20,000 ticks | not measured | 1,431 | 403 | 33 |
| Peak pool retained bytes | not measured | 1,555,712 | 3,408,384 | 6,962,176 |

The 64-slot result is 55.9% fewer requested bytes than the Cortex prototype,
and 92.5% below the original 1,267,534,448-byte baseline. In one concurrent
pair of uninstrumented Playground runs, peak RSS was 76,496,896 bytes with
Nicowar reuse and 16 slots, 77,791,232 bytes with 32 slots, and 80,035,840
bytes with 64 slots. Timings from these individual runs are too noisy for a
speed claim. The 64-slot pool holds at most 8 MiB; its measured peak was
about 7 MiB.

On SmallForTwo (seed 1441, Nicowar/Warrush), the 64-slot build produced
238,111 allocation calls and 16,552,839 requested bytes in the same tick
window, versus 240,829 calls and 22,517,943 bytes for the earlier
gradient-only prototype. Checksums and order counts matched.

`structured-pool64/` holds the same 20,000-tick Playground run and
single-compute-thread options as `structured-cortex-final/`: its uncompressed
per-tick checksums and all four checkpoints plus final save compare byte
identically. `structured-pool64-resume/final.game.gz` matches the earlier
resumed save, and `structured-pool64-4t/final.game.gz` matches the earlier
four-compute-thread final save. `BuildingGradientInvalidationHarness`,
`PathGradientHarness` (1,526 cases, unchanged digest), and
`run-savegame-safety-tests.py` passed. Only macOS arm64 was exercised;
cross-platform checksum coverage remains outstanding.
