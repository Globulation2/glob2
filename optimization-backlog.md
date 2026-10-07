# Runtime resource optimization evidence and remaining backlog

The first optimization candidate was **rejected and reverted** after controlled paired CPU measurements failed to show an aggregate benefit and showed a Maxima slowdown. Frozen binaries and the patch remain as evidence only. The former 2%/5% limits are historical diagnostics, not completion criteria. No speedup is claimed from sampling or instruction counts.

## Recovered references and workload

The merged-resource production reference is `9084ea907`. The locally built reference at tooling revision `7bafc3f15298a1649e807e42909c3fa2fe3bc609` has unchanged production engine code and release executable SHA256 `77fb4b9d29432cdfea08734b4eb0b0d3eb7c354862fdc9820ba01c89a76333b6`. Its symbolized twin was relinked from identical objects without stripping: every allocated ELF section matches except build-id. Full object, source, command and runtime-library provenance is retained.

Historical pre-refactor f0 pure executable: `842ca2aecc71c2cb48f9a26b146cd9c39dec3cab214deb260191719dc1592c00`; approved-fixes control: `eb48402f6332a0f519b7f6b6e6f908adc359f0c69ed5acc301efabffe3e46dc9`. Surviving symbolized twins have distinct binary hashes (`8296540a82234e060250b1b6d8d445743e239f7049e1bd6c5219c4f6c36accab` and `91d3543383785811fc1fbd79ee497d6cdf7c67201464383ba09f471fbf741741`) and recorded allocated-section equivalence except build-id; do not call them byte-identical restorations. Both restored f0 data roots match all 100 historically recorded runtime-data hashes. Older controls attribute intended behavior changes; optimizations compare primarily with the merged reference.

The original `7e54a3fc581d22d32e7b97a6aee76dbfeae58e9f` fixture generator was rebuilt after local archival removed the original executable/checkpoints. Both priority saves and the complete 98-window legacy corpus were regenerated **byte-exactly**, with every historical fixture hash verified. This restores the original workload rather than introducing replacement saves. Recovery reports retain source archives, original/executed commands and dependency identities.

Priority windows both load tick 24576 and end at 32768: land-2-1002-maxima save SHA256 `bad81fa2df4be1cbd9446bd6251c55468cc759bd9a25a147dc6e3aa0e2efd91a`; land-2-1001-cortex save SHA256 `2a7be1f93c10462e5f73bb8c7c4fc2f9697a58a8ce7de9510dec8ee02d9cde0d`. Diagnostic engine arguments preserve compute threads 4, experiment ai, gradient workers 2 and delay 8.

## Fresh merged profiles

Each process-scoped profile ran the exact 8192-tick window with affinity 0–7, 499 Hz user-cycle sampling, and no added checksum/telemetry flags. Both lost zero samples. Binary, fixture, catalog and runtime-library hashes remained stable. Startup is included, flat attribution is affected by inlining, and concurrent owned fixture/build work was allowed for diagnostics. These results are not controlled CPU measurements.

| Hotspot | Cortex sampled cycles | Maxima sampled cycles |
| --- | ---: | ---: |
| Periodic plus synchronous propagation | 20.81% | 35.91% |
| Placement body | 18.60% | — |
| Ground BFS | 12.04% | — |
| Shared tile predicate | 10.43% | — |
| Byte-snapshot nearest-food rings | 9.80% | — |
| Worker circulation | — | 3.82% |
| Resource growth | 1.30% | 2.77% |
| Guard seeding | 1.46% | 2.75% |
| Maxima route preparation | — | 2.26% |
| Maxima candidate scoring | — | 2.22% |
| Maxima farming | — | 2.02% |
| Surviving-food cluster count | 0.62% | — |

Separate exact-window process-counter runs measured effective frequency as cycles divided by task-clock: Cortex 2.5957 GHz and Maxima 3.5079 GHz, with all events running 100%. These cover all engine threads including startup, not a particular simulation phase, and are separate instrumented runs.

A later separate diagnostic repeated both frozen engines under the performance governor: merged Maxima3.9526GHz/Cortex3.9636GHz and rejected-ring Maxima3.9510GHz/Cortex3.9634GHz. All counters ran100%; inputs stayed stable and all eight governors were restored. Two unrelated asset encoders were active, so these are instrumented diagnostics and **not the effective frequencies of the earlier paired CPU observations**. [Raw counters, provenance and restoration audit](stabilized-frequency-diagnostics.zip) preserve that distinction.

## Rejected indexed-ring candidate

Annotated Cortex assembly shows each vertical ring byte lookup wrapping y, multiplying by width, and adding x. The dependent vertical load branches account for 24.44% and 22.65% of nearest's local samples. This supports testing compact indexed traversal; it does not predict savings.

The candidate changes only `FoodAvailabilityView::nearestDistance`: vertical edges advance or retreat the flat index by width and mask by total cells. Original horizontal dx loops, ordered ring edges, torus aliases, caps, immutable snapshot lifetime, allocation footprint and material predicates remain unchanged. No eager distance field, memo table, new schema or gameplay feature was added.

- Base: `7bafc3f15298a1649e807e42909c3fa2fe3bc609` plus retained patch.
- Patch SHA256: `13777ea2021815ed05aba03648601e9b8d957e787581f6cb6737d0bcfab3053d`.
- Frozen candidate executable SHA256: `353dcb91758300d9ceb965273cb32bf9821d0cf03462d6cf97ac9dba48c08386`.
- CortexGeometry: 4 cases pass. RuntimeResources: 25 cases pass, including composition/metamorphic fixtures, secondary/depleted stock, custom obstruction and legacy 138/139 continuation.
- Forty engine children returned 0. Eight complete 8192-record priority traces agree byte-for-byte across before/after and compute/gradient workers 1/4. Sixteen continuation tails match every detailed simulation record across all eight pending-gradient deadline phases per scenario, saving with worker 4 and resuming with worker 1.
- The original correctness command session reported shell status **143 after its final PASS log and passed audit**. Cause remains unresolved; the script has no process-group signalling or post-PASS error path. This is not a clean-wrapper-exit claim. A separate independent output audit exited 0 and reread all 40 child statuses, eight full traces, sixteen tails and frozen hashes. Published evidence keeps this distinction explicit.

Candidate Cortex instructions were 143.277 billion versus reference 150.273 billion: **4.656% fewer instructions, not a CPU speedup claim**. Diagnostic cycles increased by a factor of 2.614 and effective frequency was 3.9558 GHz. Execution context differs, and the indexed loop adds a dependency chain; either environmental effects or a real regression is possible. The subsequent controlled run completed one warmup pair and 16 measured alternating pairs per priority window with exclusive owned-work scheduling and stabilized governors. Binary, fixture and runtime inputs verified unchanged. Original governor settings were restored successfully with no restoration errors. Quiet preflight passed; measured background activity was 0.26–1.53 average cores per run (mean 0.549), so the host was not completely idle.

| End-to-end metric | Candidate change | 95% confidence interval |
| --- | ---: | ---: |
| Aggregate CPU | +0.0895% | −0.5503% to +0.7575% |
| Cortex CPU | −0.8306% | −1.6100% to −0.0299% |
| Maxima CPU | +1.0232% | +0.0675% to +2.0197% |
| Cortex peak RSS | +2.5308% | +2.0654% to +3.0576% |
| Maxima peak RSS | +2.3948% | +1.1339% to +3.7229% |

The small Cortex improvement does not establish an aggregate benefit and comes with a Maxima regression and increased measured peak memory. **Rejected:** the coordinator restored the original production header. Passing historical percentage diagnostics (`performance_gate: within_limits`) does not justify retention. No full-corpus campaign is warranted for this rejected patch. Raw paired observations, metadata, input verification and summary are retained under `priority-timing/paired-01`, with governor audit `priority-timing/paired-01-governors.json`.

## Ranked remaining investigations and evidence rejections

1. **Indexed-ring candidate rejected with controlled CPU evidence.** Original production code is restored. Preserve the diagnostic reduction in instructions alongside the null aggregate CPU result and Maxima slowdown; do not resurrect this patch from instruction counts alone.
2. **Exact-input diagnostics now demonstrate repeated periodic propagation; a bounded reuse candidate is under investigation.** Across all 8192 ticks, Maxima repeats 2187 of 8160 comparable prepared inputs (26.80%); Cortex repeats 2221 of 8151 (27.25%). Material fields account for 1662/6120 and 1600/5966 comparable duplicates respectively; the remainder are guard/clear fields. Complete seed/water/terrain contents and scalar parameters match, with zero conservative immutable-registry/profile pointer misses in either window. Both instrumented full 8192-record traces match frozen merged execution byte-for-byte; inputs remained unchanged. This is approximately 27% of comparable **job counts, not CPU cost or saved CPU**. Periodic propagation accounts for 30.02% of Maxima samples; another 5.89% is synchronous propagation and is outside this opportunity. Low discarded-job counts (89 Maxima, 5 Cortex) still limit cancellation, but cannot bound duplicate-input reuse. A new candidate must preserve fixed publication deadlines, synchronous invalidation, worker/error behavior and pending-save continuation, and earn retention through controlled CPU measurements. No reuse speedup is established. [Complete diagnostic evidence](gradient-input-identity.zip) includes the exact patch, frozen binary hash, build provenance, commands, counts, input audits and full before/after traces; the binary itself is omitted.
3. **A second full-map seed enumeration is demonstrable but bounded.** Producers seed every cell; `propagateField` then scans the entire buffer to enqueue seeds. Sampled initial seed-scan instructions account for roughly 5.4% of the 30.02% Maxima periodic kernel, about 1.6% of total samples attributable to that part. This is an approximate cost bound, not a speedup projection. Carrying ordered seed indices from preparation might avoid the scan, but requires extra per-job storage, correct supplier/deferred-cost seeds, parallel-initialization ordering and completed-snapshot save semantics. No implementation is justified before frequency/cost measurement establishes that these costs outweigh new work.
4. **Do not repeat existing propagation optimizations.** Current kernels already hoist coordinates/terrain costs/destination buckets, specialize uniform swim classes, vectorize relaxation, reserve append capacity by chunks and discard stale queue entries. Historical commits `9e8b378eb` and `05207be36` cover earlier repeated-work reductions; current seed templates already deduplicate dirty cells and patch goals/fog/forbidden state. Growth/seed caches are not unexplored blank slates.
5. **Complete toroidal Food distance field: rejected by existing diagnostic evidence.** The prior implementation preserved 8192 GCS records but increased instructions from 172.600 billion for the byte snapshot to 181.968 billion (+5.43%). Construction overwhelmed the avoided scans. Retained source/profile evidence prevents treating that approach as untried.
6. **Per-coordinate nearest memo: reject before implementation.** Each placement pass visits a coordinate at most once for its cap-5 food gate; only four retained candidates repeat at cap 12. Non-food buildings make only those four queries. A map-sized memo would add writes for predominantly unique requests to save at most four small scans.
7. **Reusing the existing food snapshot in cluster counts is low priority.** The current direct count accounts for 0.62% of Cortex samples. It may remove duplicate material queries but does not presently support a substantial-win claim. Maxima discarded expansion-query/hoisting ideas likewise had less than 1% old self attribution.
8. **No evidence for ecology mutex removal or mixed-stock layout redesign.** Existing profiles do not attribute meaningful contention/storage cost to those mechanisms. Keep them as unproven investigations, not committed changes.

Cross-platform per-tick verification and final corpus/stress performance remain separate requirements. Expanded gameplay trials, physical ARM/iOS testing and maintainer gameplay review remain deferred with the original documented limits.
