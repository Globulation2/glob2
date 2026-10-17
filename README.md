# PR 1196: forbidden zones apply only to ground units

Source `2c057dfd91816c08dc2234f7cb889f108381a4f9`, based on fetched master `486a2230a4ddb257ba9dee34d3aace05f2260d6f`; previous PR `eddff43fbb57f772f570a9a23741ece5a6b4ecc5`.

Flyers ignore forbidden paint in direct angular steering, local traffic repair, shared material/supplier/service/building/guard/clearing fields, point routing and hazard escape. Painted zones no longer trigger air escape or disable direct flight. Ground checks retain their existing masks. Air hazard-safety fields now share across teams and paint does not invalidate them. Terrain hazards, unflyable cells, air-blocking resources and air occupancy still apply; constrained routing and guard balancing retain their shared fields.

Simulation revision is **72**, and the match record is regenerated. Save layout remains 171, save floor remains 58, replay floor remains 171, and network protocol remains 71; SIM version separates changed simulations. No serialized field or runtime dependency was added. Released save migration/current continuation and replay/network version acceptance are covered by the focused tests listed below. Previously generated unmerged PR saves/replays do not promise identical results across this simulation-rule change.

## Validation

- Clean Linux x86_64 / GCC 13.3.0 development build of the game and both test binaries: `python3 tools/dev_build.py --build=/dev/shm/glob2-direct-air -j12 engine-tests unit-tests /dev/shm/glob2-direct-air/src/glob2`, `dev_fast=1`, `-O0 -g1`, no PCH/unity.
- **288 engine cases and 50 unit cases pass**, with no failures, errors or skips. Exact filters, commands, case inventories, JUnit and logs are archived. Coverage includes direct travel through paint, idle flying, whole-map painted service/resource/guard/clearing queries with and without air terrain constraints, unchanged ground blocking, cold/warm owner and worker templates, air hazard cache sharing/invalidation, existing ablation/RNG golden, hiring/service/supplier policies, retained save migration, current continuation, replay boundaries, and the SIM-72 match record.
- Ordinary 32-courier setup (now painted across its wrapped flight path) and constrained 64-flyer setup match every per-tick aggregate and team/entity checksum byte for 128 ticks across 1/2/4/8 compute participants. Both resume identically at all eight publication offsets. Commands and exact saves/sidecars/hashes are retained.
- Documentation (309 Markdown documents), generated preparation rules, simulation-version and whitespace checks pass. Current master was fetched before final validation and remains the recorded baseline. Source checkout is clean.

## Optimized native timing

| Fixed input | Master CPU ms | Prior PR CPU ms | Updated PR CPU ms | vs prior PR | vs master |
|---|---:|---:|---:|---:|---:|
| midgame24k | 157.91 | 166.31 | 159.35 | -4.2% | +0.9% |
| islands | 296.72 | 313.84 | 303.11 | -3.4% | +2.2% |
| swamp | 294.83 | 300.76 | 298.41 | -0.8% | +1.2% |
| lava-shield | 192.63 | 194.56 | 196.20 | +0.8% | +1.9% |
| constrained64 | 1168.33 | 68.99 | 69.11 | +0.2% | -94.1% |
| ordinary32 | 5.48 | 5.53 | 5.55 | +0.3% | +1.3% |


Values are median process CPU milliseconds for 256 measured ticks after 128 warmup ticks. Ten measured runs per input/version (180 total), one priming run each, rotating case/version order, sequential execution on a quiet shared workstation after compilation, testing and video capture finished. Matching Linux release builds: GCC 13.3.0, `dev_fast=0 release=1 retain_symbols=1 CXXFLAGS=-g1`, `-O3`, no PCH/unity/profiler hooks, free-client identity, four compute participants pinned to the same four physical cores, GPU/compute telemetry off. Every run reaches its tick cap and repeats the initial/final checksum within its own version/input combination. Cross-version equality is not expected because behavior changes. Source/input/executable hashes, release identity, CPU/OS/compiler and all expanded command arrays are retained. The prior PR executable was copied and hashed before rebuilding its output directory; master is the same clean baseline used in the previous evidence.

Process CPU includes final worker drains. The raw summary includes min/max/quartiles, headless tick p50/p95/p99 and peak RSS; whole-run elapsed includes warmup and is not the measured CPU window. This is a native sample, not a cross-platform performance guarantee. Small differences remain sensitive to shared-host noise. Shared destination queries still have costs even when direct steering is enabled; ignoring paint does not restore every simulation choice or every CPU cost of master.

## Observed visual behavior

A new optimized, software-rendered 960×720/25 fps clip uses the same authored forbidden-wall initial save as the prior recording. Frames at 1.5, 3 and 6 seconds were inspected: the carrier flies through the painted wall rather than diverting to its distant gap, reaches the inn and resumes flying. The archived video metadata reports completion and 0 dropped frames. This is a virtual-display qualitative observation, not frame-pacing or maintainer hands-on qualification. The custom courier's authored FLY value is 28 and the visual fixture overrides it to 32; the native timing fixture keeps its original authored performance. The older forbidden-avoidance and forbidden-escape clips in previous evidence describe the superseded policy.

## Evidence and limits

The archive contains exact build/test/benchmark/recording commands and logs, final and initial JUnit, fixture inputs and complete checksum sidecars, source/binary/dependency provenance, native results/samples/ranges, the reviewed clip and frames. No executable or compiler object is bundled. Source behavior is committed in the PR; these temporary development records live only on the evidence branch.

The first build exposed a misspelled local movement-class variable; it was fixed. The initial test run found two assertions expecting the superseded paint policy and an air seed probe physically affected by a nearby void tile's blended edges. The assertions were updated and the paint-only probe moved onto clear terrain. Final clean builds and the full focused suite pass. No broad golden regeneration was used to conceal changed behavior: the original ablation golden remains unchanged, and only the required simulation-version match record changed.

Windows, macOS, Android, iOS, browsers, actual GPU/NEON execution, long matches/combat, physical-display frame pacing and human maintainer feel remain unverified. The PR stays draft; no merge or hosted engine matrix is requested.
