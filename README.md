# PR 1196: remaining ordinary-map overhead

Tested source 656921aa2cb275a294508ef07e9614ead56be0de (application code unchanged since 2c057dfd91816c08dc2234f7cb889f108381a4f9), compared with master 486a2230a4ddb257ba9dee34d3aace05f2260d6f. No production code changed during this investigation.

The ordinary-map fast path restores direct steering, but retains shared-field destination selection, air-aware seed kernels, and nine-class field bookkeeping. Therefore it does not restore master's computational path.

## Measurements

Optimized GCC 13.3 release objects, -O3, dev_fast=0, retain_symbols=1, CXXFLAGS=-g1, no PCH/unity, Linux x86_64. CPU-only, four participants, telemetry off. Same retained saves as previous native evidence, 128 warmup ticks and 256 measured ticks. Callgrind instrumentation was added only to a copy of Headless.cpp at measurement boundaries; compiler/link argv and patch are archived. Every thread's self costs reconcile with its footer; combined thread totals reconcile with Callgrind's collected count. Each instrumented run matches its native counterpart's initial/final checksums and tick counts. No builds/tests overlapped the profiles. These are instruction counts, not native timing measurements; one profile per revision/input.

| Input | Master instructions | PR instructions | Extra instructions | Earlier ten-run CPU median delta |
| --- | ---: | ---: | ---: | ---: |
| islands | 2,846,751,060 | 2,956,528,982 | +3.86% | +2.2% |
| midgame24k | 1,619,739,570 | 1,648,782,223 | +1.79% | +0.9% |

CPU medians come from the earlier quiet native experiment, not Callgrind. Small timing deltas have overlapping run ranges and remain noise-qualified. Instruction counts establish actual extra work independently.

## Where the extra instructions go

Percentages below are shares of the net instruction increase. These selected call-tree roots are distinct; omitted paths and offsets account for the remainder. Instruction shares must not be interpreted as shares of native elapsed CPU time.

| Path | Islands | Midgame |
| --- | ---: | ---: |
| food_selection | 38.327% | 7.546% |
| material_seed | 28.912% | 37.377% |
| clear_seed | 7.542% | 15.417% |
| scheduled_building | 4.959% | 9.515% |
| field_propagation | 5.271% | 4.369% |
| direct_gate | 0.003% | 0.006% |

1. **Food selection still routes.** Team::findNearestFood calls buildingAvailable for flyers on ordinary maps. That locks/prepares the building field and resolves the unit's distance. Master used geometric distance on these maps. Healing and upgrade selection have the same structural difference, but contributed no positive delta in these windows. Native counters over each retained run show 16 vs 8 synchronous building builds on islands, and 46 vs 37 on midgame; scheduled job counts match (206 and 355 respectively). These counters span the run, not just the profiled window.
2. **Ground material preparation pays for air-capable code.** MaterialSeedCache::trySeed contains an airSignatures.empty test inside its per-cell refresh loop. Annotated worker profiles show millions of instructions on that test, and no instructions on the air-data update body during the measured window. The entire function's delta also includes compiler/code-layout changes and surrounding bookkeeping; it is not all attributable to that single branch. Keeping the air template lazy has not preserved master's ground loop. Specialized ground-only preparation would remove the per-cell air test.
3. **Other fields still participate.** Clearing-goal preparation, building-field preparation and field propagation add work. Direct steering can still obtain destinations from fields; the current fast path does not bypass every field consumer. Air movement adds a ninth class to cache/scheduler scans. These facts explain remaining paths, but are not individually isolated causal timing measurements.
4. **The eligibility gate is tiny.** hasAirRoutingConstraints costs 3,216 instructions on islands and 1,656 on midgame, versus net increases of roughly 110 million and 29 million. It is not the remaining bottleneck.

## Implication

To approach master cost and preserve historical choices on ordinary maps, use the no-air-constraints gate for service destination selection as well as steering, and specialize ground seed preparation so unused air support is outside its inner loops. Keep air-only caches/jobs dormant until a real consumer needs them. Such changes have not been made here. Restoring geometric selection changes simulation choices from the current PR and needs the usual simulation-version/golden/compatibility validation.

## Evidence and limits

The archive includes raw profiles, parsed call trees and reconciled totals, worker source annotations, instrumentation patches, compiler/link commands and binary hashes, run commands/results/checksum comparisons, timing summary and input provenance plus the two saved initial states. The previous native measurements and full validation remain linked from the PR. This investigation adds no cross-platform or human playtesting qualification. No additional full suite run was needed because production source was unchanged.

One annotation command initially used a relative raw-profile path while selecting the master working directory and failed; it was corrected to absolute paths and rerun for all six worker profiles per revision. The final annotation files are the successful results.
