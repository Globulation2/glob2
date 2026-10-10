# Offline gradient alternative qualification

Tested tooling source: `bec5f1b0e4901488657df15d3c906c6bd33a0be3`. Runtime source remains unchanged from `9f3c92c834c17f4f15f09a3f0c65edb2997cefd7`. Current master `4ca7b086359891010281dd994dbbcd8cceadadad` was fetched and the committed change merges cleanly; no unrelated merge/rebase was performed.

**No new production algorithm admitted.** The implementation adds an offline, reproducible qualification pipeline and three experimental candidates. They are not registered with the runtime controller. Accounting remains opt-in, automatic unknown workloads remain CPU, and eight compute slots still include the owner.

## Candidates and predeclared classes

- Global Jacobi relaxation: large complete fields (at least 256² cells), cap <=40. One path edge per global dispatch with tile activity tracking.
- Atomic frontier propagation: large complete fields with at most max(1, cells/1000) seeds. Atomic monotone updates schedule outgoing work for the next dispatch. Seed-count classification is not assumed free in production.
- Bounded dependency cone: large complete fields with cap <=16*minimum entry step. Selects 2/4/8/16 local Jacobi rounds sufficient for floor(cap/minimum step), completing in one dispatch without convergence polling. The offline runner includes minimum-cost scanning overhead; a production profile would need measured cheap availability.

Each was compared against all six existing GPU plans. The bounded plan reuses the production Jacobi kernel with an independently checked finite-path bound; the other two are separate experimental kernels. None changes the shipped kernel source.

## Completed verification

| Coverage | Layouts / cases | Eligible GPU executions | Outcome |
|---|---:|---:|---|
| Development corpus | 40 independent layouts, 120 chronological fields | 6,000 | all exact |
| Stress corpus | 18 fixtures, 54 chronological fields | 2,700 | all exact |
| Adversarial edges | 120 cases | 1,039 | all exact |

Development has eight layouts at each of 64², 128², 256², 512² and 1024². Stress includes 1×1, 1×257, 257×1, 7×13, 63×129, 127×513, 513×257, 2048×64 and 2048². Adversarial cases include empty/blocked fields, deferred seeds, costs up to 65535, cap boundaries and directed source-cell costs. The independent oracle is heap Dijkstra without production traversal or GPU implementation reuse.

The development log has 6,480 record slots, including 480 semantic skips for the bounded plan; stress has 2,916 slots including 216 skips. Skips are not executions or wins. Repetitions and chronological fields do not increase independent-map counts. Earlier development revisions remain local and are not pooled with this cohort.

Seven tool tests passed: split independence, topology reservation, determinism, classification, noise/cold penalties, map-level aggregation and rejection of incomplete/duplicate/inexact/mismatched evidence. Documentation validation: 306 documents, zero errors; ten documentation tests passed. Native runner and oracle compile with GCC 15.2 `-O3 -DNDEBUG -Wall -Wextra -Werror`.

## Screening result and reserved evaluation

All three candidates had **0/24 development class maps satisfying every combined win condition**: beat every existing GPU plan in cold and warm measurements by more than 5%, 10 µs and three median absolute deviations. This is an admission-screen failure, not a claim that an implementation never has an isolated favorable timing. Raw samples and every comparison are included.

The final evaluation is reserved: **1,000 independent layouts, 200 per square size**, disjoint seed domains and entire held-out ridge/canal topology families. No final layout inputs or timings were generated. The runner requires complete matching-source development, stress and adversarial reports, a development survivor, frozen source/protocol/corpus hashes and a one-use holdout receipt. Final qualification requires at least 100 class maps and >=90% map wins. Missing results, failures, incorrect exclusions and inconclusive evidence cannot qualify.

No candidate advanced to final evaluation, runtime integration or real-game removal ablation. Those are unperformed gates, not passed tests. Offline analysis always leaves `admitted` empty: cheap selectability, competitive coverage, stratified early/middle/late simulations, eight-slot scheduling, rendered contention, whole-game runtime/publication waits and compatibility evidence remain required before adding a runtime plan. Existing plans have not been requalified under the new 1,000-map standard by this work.

## Environment and limits

Linux x86-64, NVIDIA RTX 2070 SUPER, driver 580.178.04; GCC 15.2; Python 3.12; NumPy 2.3.3 and PyOpenCL 2025.2.6 from the existing laboratory environment. These observed versions are recorded rather than claiming a fresh install of the repository's broader dependency set. The native execution loop uses an unprofiled queue. Cold means fresh buffers in an already compiled process; driver/process cold startup is not measured. Compilation is separate; total timings include Python preparation/orchestration, cost upload when cold, transfers and readback.

The host is restricted to CPUs 0–7, with a cooperative GPU-1 lock. Other builds and desktop activity may run; this is not exclusive CPU/GPU ownership. Timing noise and preparation costs are included in screening. No production speedup, cross-vendor portability, cross-platform determinism or whole-game benefit is claimed. Only Linux/NVIDIA execution was tested. Runtime files did not change, so full-game save/replay suites were not rerun for this tooling-only commit; prior milestone evidence remains linked on PR #1045.

## Reproduction

`validation-commands.json` contains exact commands. Each corpus directory includes the protocol, complete roster and source hashes frozen before generating inputs, compilation flags/logs, observed environment, input hashes, raw per-execution timings and analysis. `source/` contains the tested scripts/kernels and production kernel comparator. `reserved-final-manifest.json` records the unused final roster. Build binaries are omitted; rebuild from the included sources.
