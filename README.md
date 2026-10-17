# PR 1196 performance and observed play scenarios

Source `eddff43fbb57f772f570a9a23741ece5a6b4ecc5`; master `486a2230a4ddb257ba9dee34d3aace05f2260d6f`; fully field-routed production `b2264eb567c7b6a8ff33282f9ffaf59fd202ef4e`.

## Native optimized comparison

| Fixed input | Master CPU ms | Fields CPU ms | Hybrid CPU ms | vs master | vs fields |
|---|---:|---:|---:|---:|---:|
| midgame24k | 157.66 | 164.92 | 164.21 | +4.2% | -0.4% |
| islands | 298.35 | 314.90 | 315.26 | +5.7% | +0.1% |
| swamp | 293.38 | 301.44 | 301.42 | +2.7% | -0.0% |
| lava-shield | 192.36 | 196.71 | 193.48 | +0.6% | -1.6% |
| constrained64 | 1171.94 | 68.19 | 67.62 | -94.2% | -0.8% |
| ordinary32 | 5.51 | 8.51 | 5.63 | +2.2% | -33.9% |

Values are median process CPU milliseconds for 256 measured simulation ticks, after 128 warmup ticks. Each input/version has one priming run and ten measured runs; variants and case order rotate. Four compute participants, fixed four-physical-core affinity, GPU and compute telemetry off. GCC 13.3.0, Linux x86_64, free client profile, `dev_fast=0 release=1 retain_symbols=1 CXXFLAGS=-g1`, normal compilation without PCH/unity. Process CPU includes final worker drains; headless tick p50/p95/p99 and RSS are retained separately in summary.json and every sample. Whole-run elapsed includes warmup and is not the measured CPU window. Every trial reaches the cap with identical initial/final checksums within its own source/input combination; cross-version checksums are expected to differ. These compare complete simulation execution from the same saves and empty order streams; changed routing can change subsequent work, so they are not isolated steering microbenchmarks.

Candidate and master are freshly built clean production executables. The previous fully field-routed release executable is reused only after its SHA-256 matches the earlier recorded qualification, with identical release identity. No profiler hooks or instruction counts are used. Exact command arrays, input/binary hashes, build logs and dependency identity are archived. Authoring hooks in visual-fixtures.cpp only construct saves and emit trajectory traces; that local debug harness is not used for timing.

The build and video capture finished before measurement. The first 180-trial pass overlapped a separate contributor simulation workload and showed substantial timing drift (for example midgame CPU ranges roughly 159–262 ms). That entire pass is retained under native-shared-host/. The reported second 180-trial pass started after that workload ended, with the same script, fixtures, executable hashes, priming and trial order. This is still a shared workstation; per-run snapshots retain actual conditions. Each run retains process and load snapshots. Ten interleaved trials and CPU-time accounting reduce scheduling noise, but small differences should not be interpreted as stable wins. No release readiness or other-platform performance claim follows from this native sample.

The ordinary 32-courier input is 33.9% cheaper than field-only routing and within 2.2% of master. The constrained 64-flyer input is 94.2% cheaper than master; median headless p95 tick time falls from 11.862 ms to 0.147 ms. The four broader saves range from 0.6% to 5.7% more CPU than master; they show no general speedup. The ordinary-map preference preserves flight behavior while the shared destination/query infrastructure still has a cost. Per-run snapshots in the reported pass contain no compiler or external simulation process, and source/input checksums match across both passes.

## Recorded and interactive observations

Eight native software-rendered 960x720, 25 fps scripted scenes are retained as MP4 plus metadata. They use small authored maps, a custom flying courier catalog, and accelerated movement (FLY=32 versus ordinary 8) to keep the clips short. These are routing/job observations, not default-speed balance measurements. Contact sheets and time sequences were visually inspected against 512-tick traces; feeding was traced for 4096 ticks.

- Open bearing: delivery tick 142; nearby traffic: 150. Both complete with health 200.
- No-fly wall: delivery 217, through its gap; forbidden wall: 200, with zero forbidden occupancy. Both retain health 200.
- Lava detour: delivery 182, health 200 throughout. This scene's capture drops one frame; it was recorded during compilation and does not measure frame pacing.
- Escape: starts inside forbidden paint, exits at tick 24 and does not return during the trace.
- Clearing: the marked wood disappears at tick 126; health drops by the clearing action and recovers.
- Feeding: enters the inn, consumes food at tick 652, exits and resumes normal activity. The full trace extends to tick 4096.
- Normal interactive application: load the clearing save through the game menu, drag its flag to another tile, pause/resume, quit to results and back to the menu. Screenshots and input receipts are retained. This is a UI smoke test and does not establish that a worker follows the relocated flag.

A separate fully field-routed bearing clip is included for comparison. Scripted clips show no observed failed delivery or stuck service in these cases. Maintainer hands-on playtesting, tower/combat feel, long matches, dense swarm responsiveness, actual GPU/NEON and other platforms remain unverified. The virtual display and software capture do not qualify desktop frame pacing. No production source changed during these measurements; PR remains draft.

## Evidence files

- `summary.json`: per-case median, range and quartiles for CPU time, headless tick latency and RSS.
- `samples.json`: 180 measured trials, CPU time and checksums.
- `provenance.json`: exact source, executable/input hashes, compiler and build identity. `native/input-inventory.json` in the archive records unit/flyer counts: midgame 224/16, islands 322/38, swamp 203/23, lava 49/7, plus the authored 32/32 and 64/64 setups.
- `play-summary.json` and `capture-summary.json`: script outcomes and capture completion/drop metadata.
- `contact-sheet.png`, ordinary/constrained sequences: visual overview.
- Archive: scripts, build logs, fixtures, CSV traces, native result JSON/logs/commands, nine reviewed clips and interactive screenshots/receipts. No binaries are bundled.

## Setup and limits ledger

The first camera setup centered away from the scene; team start coordinates were corrected in fixture generation and clips regenerated. Recording refused a reused output basename because its sidecars still existed; those setup attempts are omitted from the reviewed clips. The feeding trace was extended to include completion. An initial fixture invocation overlapped linking and could not execute the unfinished test binary; the final two fixture-export cases pass. The baseline build was resumed with more compiler jobs after candidate compilation finished. No production code corrections were needed in this measurement task.
