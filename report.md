# Deeper movement-planning investigation

Production source remains 3a3917757752fe64a2af9c1dc964c074794ebf3e, base 0709322f3. No tracked files changed. All added code is isolated under ignored artifacts and linked into diagnostic executables. Linux x86-64, GCC 15.2.0, Valgrind 3.26.0. Same preliminary 1,024-worker colony, seed 713, K=1, four compute participants, 128 ticks.

## What was measured

- Optimized source-line Callgrind profiles for reference and shadow. Instrumentation begins at tick 64, avoiding initialization and the first cold planning wave. The window covers ticks 64–127 plus final computation drain, with possible tail work submitted before tick 64. Objects retain -O3 and add -g; no de-optimization or new runtime dependency.
- A separate native causal probe records cache misses, exact resource blocking/material-membership changes, building geometry/supply changes, and exact ordered (seed cell, destination cell) sets.
- Native counterfactuals share resource fields when a home contributes no eligible stock-supplier goals, and traverse already ordered unit/actor records once instead of repeated map lookups plus live-ID sort/binary searches. These are diagnostic experiments, not promoted production fixes.
- Full suggestion records (identity, sequence, epochs, action generation, observation/publication/completion ticks, position, mode, disposition, destination/supplier, preferred prefix and all eight directions) are compared against control for 7,676 requests. All outputs match for goal sharing, owner traversal and both combined. All final checksums equal 2507656052.

## Field rebuild causes

The unchanged control builds 1,086 fields: 258 resource fields and 828 building fields. There are 128 first-goal builds and 958 later misses whose changed key member is the resource revision. Only one rebuild has unchanged conservatively tracked semantics.

Across captured planning inputs, 126 wheat cells are removed on harvesting. Wheat blocks ground movement; removing it changes walkability and, for resource planning, available goals. Building geometry and supply availability do not change. One observed interval changes amounts without changing membership/blockers. Thus the broad resource-revision key is not primarily responding to quantity-only noise on this input. Claims that all or most of these invalidations can simply be removed would be wrong. Each field still performs a nearly whole-map reverse search after small genuine changes.

Exact seed comparisons identify 242 resource field builds with the same effective goals and capabilities at the same world/resource revision, differing by home. There are no eligible stock suppliers contributing home-specific goals here. The isolated key experiment preserves home identity when eligible supplier goals exist; otherwise the field is shared. It builds 844 fields and expands 12,817,069 cells, versus 1,086 fields and 16,480,647 cells. All 7,676 suggestions match exactly. This proves an avoidable duplication on the tested input, not general market/supplier correctness.

## Owner admission

In ticks 64–127 the owner scans 65,536 unit records to submit 3,154 requests: 20.8 scans per request. It extracts 65,536 Task records, calls modeOf twice per record, predicts completions 55,967 times, compares task signatures 65,536 times, and validates published task records another 52,813 times.

The original boundary's own instructions include 5.70 million in ordered-map internals, 5.70 million in algorithm/base helpers (including binary searches), 1.06 million in algorithm helpers and 2.56 million in iterator helpers. Those source-file buckets are not separate inclusive phases and should not be summed with child function totals. Repeated Task equality adds substantial child work.

Captured units are already in ascending GID order, as specified by the snapshot extraction pass. The isolated owner candidate walks that ordered stream and the actor map together, removes missing actors during the walk, inserts using a correct-position hint, and evaluates mode once. It retains deterministic request sorting and all admission/epoch logic. Boundary inclusive instructions fall from 51,316,579 to 28,853,776: 43.77% fewer. Exclusive instructions fall from 31,170,390 to 17,637,309. All 7,676 suggestion outputs match. No death, conversion or slot-reuse coverage is implied by this fixture; those cases need regression validation before promotion.

## Snapshot extraction

Source-line profiles show the movement boundary captures full UnitView records, 384 bytes each, although Task is 88 bytes plus the action-clock inputs needed for scheduling. Unit copying alone is 384 KiB per tick for 1,024 units, or 24 MiB over the late 64-tick window; this is payload work, not a peak-memory or bandwidth measurement. Extraction also copies building records (408 bytes each), relationships, slot tables and team statistics.

Store capture inclusive instructions over the late window are 9,526,744 for reference and 27,875,882 for shadow. In this headless fixed-order fixture AINull requests no observation data, so movement adds full entity extraction. Do not extrapolate that marginal cost to a controller/presentation workload that already requests Entities. A narrower movement input representation and event-driven admission are plausible next investigations, not measured fixes in this report.

## Timing limitation

Unrelated compiler jobs were active during native phase timing collection. Those runs show substantial preemption/submit wall-time variation, so their absolute times and phase percentages are discarded for performance inference. The current report makes only instruction/work-count and exact-output claims. It does not revise the prior ten-pair timing estimate or claim another measured runtime speedup. Reliable native performance must be repeated on an idle host after production candidates are validated.

## Recommendation

There is demonstrable optimization headroom. Promote the small ordered-traversal and effective-goal-sharing candidates into Stage A only after lifecycle/slot-reuse, supplier/market/depletion, all-delay/participant and canonical-output regression checks. Then repeat native timings and the unchanged gate.

The dominant remaining worker work is 828 building fields recomputed after real walkability changes. Investigate reuse of existing prepared fields, compatible bounded searches or incremental updates; do not blindly suppress genuine invalidation. The owner still scans every unit and builds full entity snapshots, so further admission/data-layout work remains even after ordered traversal.

Stage A remains failed for the production implementation. No live adoption, gate pass, memory-ceiling acceptance, qualified sustained colony, gameplay seed population, footage or cross-platform equivalence is established. This is evidence against claiming the current prototype is optimal, not evidence that every parallel design must beat synchronous execution.

Commands, raw source-line profiles and annotations, causal traces, suggestion comparisons, modified probe sources, linker commands and summary JSON accompany this report. An early diagnostic used an undersized resource-kind field and is not used; the retained v3 probe uses Uint16. Its literal newline separators in block-change logs are normalized explicitly by the parser; numeric events are unchanged.
