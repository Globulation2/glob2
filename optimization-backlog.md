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

## Exploratory periodic reuse v1 (not retained)

Frozen candidate `7dba5174851dc70775e4c2b865d0b6cd14ef01077860498d567242140e2498a2` used a 32-entry/16 MiB exact-input result cache whose comparisons and copies ran in the exclusive preparation/publication phases. One discarded warmup pair and 16 alternating measured pairs ran per priority window. Governors restored successfully and campaign inputs were unchanged. An external rig-fit Blender workload remained active: this campaign is explicitly contaminated/exploratory and cannot establish retention or a final CPU improvement. Approximate other-host activity averaged 1.35 cores and peaked at 2.94 cores; this estimate includes runner/kernel activity and boundary/jiffy error.

| Observation | Change | 95% paired interval |
| --- | ---: | ---: |
| Aggregate simulation CPU | -2.09% | -2.83% to -1.39% |
| Maxima simulation CPU | -3.25% | -4.52% to -2.01% |
| Maxima whole-child wall | 3.09% | 2.04% to 4.16% |
| Maxima peak RSS | 8.30% | 7.20% to 9.41% |
| Cortex simulation CPU | -0.92% | -1.52% to -0.30% |
| Cortex whole-child wall | 2.71% | 1.90% to 3.44% |
| Cortex peak RSS | 13.90% | 13.29% to 14.46% |

**Not retained:** both windows showed increased wall time and memory, and external contamination requires a quiet repeat before interpreting the CPU observation as a repeatable benefit. The subsequent worker-owned-lease v2 moves comparisons/copies away from the preparation barrier; its isolated priority result is recorded below. Existing v1 correctness evidence includes eight focused pipeline cases, 57 engine cases, eight full 8192-tick comparisons and 16 pending-gradient continuation tails. These checks establish the recorded correctness scope, not performance or broad gameplay equivalence.

[Raw v1 timing and correctness evidence](gradient-reuse-v1-exploratory.zip) includes commands, measurements, governor/input audits, exact patch/provenance and full correctness traces. The frozen binary and duplicated data tree are omitted, with hashes retained.

## Worker-owned reuse v2: incomplete timing, correctness retained

Frozen candidate `4a068846b4adf1aeeebdecbac94fca423f588398319aa0a25f7e5382cee5610b` moves exact comparisons and seed/result copies into a worker-owned lease held until the original fixed publication deadline. Busy entries bypass reuse; the 32-entry/16 MiB storage limits remain. Independent review and recorded correctness checks cover ten focused pipeline cases, 57 engine cases, eight full 8192-tick comparisons, 16 pending-gradient tails, native traces and a clean 10,800-job ThreadSanitizer harness.

The first v2 paired campaign began after a quiet preflight but was interrupted when 27 unrelated compiler processes appeared in `/home/bradley/glob2-pr857`. All eight governors were restored and the input audit found no changes. **The campaign is incomplete and contaminated, with no summary and no retention decision.** Partial observations are not an accepted performance result; a fresh, complete quiet campaign is required. [v2 correctness and aborted timing evidence](gradient-reuse-v2-incomplete.zip) preserves raw observations, interruption/process evidence, restoration/input audits, exact patch/provenance and full correctness traces. Binary and duplicate data trees are omitted, with hashes retained.

Frozen SIM24 v2 also passes all nine retained stress windows: six custom-resource saves (256²/512² sparse materials, equivalent definitions and mixed stocks), and three512² eight-team snapshots. All36 fresh reference/candidate runs at compute and gradient workers1/4 exit zero; each2048-record trace matches exactly across all four variants. Independent record parsing and the before/after binary/source/catalog/full-data/runtime/fixture audit pass. These runs were correctness-only under concurrent unrelated workloads; no performance inference is made. [Full stress correctness evidence](gradient-reuse-v2-stress-correctness.zip) retains all36 traces, commands, independent record audit, frozen input identities and orchestration scripts; binaries and duplicate fixtures/data are omitted.

## Current-master v2 integration: correctness verified, performance undecided

The identical v2 patch applies cleanly to `644611925c66ee9a1259bbd90e72744d387bc59c` (SIM26). Its integration source tree is `718d5a340280bf826e93a69fa4b5e55c4f3796353dc45ca452d0a2f81ba863b6`; CLI hash is `3b64f38dc280067a1ac3f0e0ecfe414050c14f11ab2c6a8503ea88af3abb6f74`, and patch hash remains `a5cb13ab13e4659c0c2453a36f63ef5fbf29c5b4fba4f4ef63558000a5bc14a2`. The build, ten focused pipeline cases and 57 focused engine cases pass. All 40 priority/continuation engine children and the independent reread exit zero: eight full 8192-record traces match the clean644 reference across one/four workers, and sixteen pending-gradient save tails match. Stock1500, official702 and Studio1024 traces also match clean644. The 150-record resource-composition trace matches the committed SIM26 golden (`6b05b1285fc0abaa07d66a1c008b912901d87925267bb576f8bdaacf293a6aaf`); it is distinct from the older SIM24 golden. No simulation version, fixture or production input was changed for these checks.

[Current-master integration evidence](current-644-gradient-reuse-v2.zip) retains exact commands, source/patch/binary/library identities, build/test logs, full traces, continuation saves and independent audit. It omits binaries and duplicate data trees. The initial unsupported extended collector argument failed before starting an engine; the pinned collector's supported interface passed, and the composition case passed through the focused engine runner. All source/runtime/static-library identities were verified unchanged at completion. Subsequent master `2d8d4e681` changes only map-report schema and TerrainMaterialsTest; this evidence remains explicitly pinned644. The worktree is retained without a commit or PR. Cross-platform candidate checks and complete controlled CPU/corpus results remain required before retention; no performance claim follows from this integration.

## Isolated v2 priority campaign: provisional broader-test candidate

The fresh complete exclusive-cpuset campaign compares frozen merged `77fb4b9d…` against v2 `4a068846…`: one warmup pair plus 16 alternating measured pairs per 8192-tick Maxima/Cortex window (68 engine runs). Independent reread recomputes every reported geometric ratio and 4000-draw bootstrap 95% interval exactly, confirms complete windows and paired final checksums, unchanged input snapshots, 637 valid partition checks, and full governor/cpuset/affinity restoration. This supports broader validation, **not final retention**.

| Scope | Maxima candidate/reference | Cortex candidate/reference |
| --- | --- | --- |
| Simulation CPU | 0.947991 [0.940733,0.956788] | 0.975086 [0.964634,0.987494] |
| Whole-child CPU | 0.950276 [0.943295,0.958710] | 0.975959 [0.965833,0.988131] |
| Simulation wall | 1.006153 [1.000204,1.012043] | 1.006018 [0.993043,1.021552] |
| Whole-child wall | 1.005747 [1.000249,1.011259] | 1.006301 [0.993744,1.021371] |
| Peak RSS | 1.083190 [1.068378,1.096657] | 1.140750 [1.135418,1.145818] |

Equal-scenario aggregate simulation CPU ratio is 0.961443 [0.954809,0.968775], a 3.86% reduction [3.12%,4.52%]. Simulation CPU includes all threads through finishSession/final gradient drain, excludes setup/requested final save; whole-child wait4 CPU also includes setup and exit. RSS median increases are 10.07 MiB/9.66 MiB. Small wall-time increases and memory costs remain explicit tradeoffs requiring the representative corpus. Intervals describe paired sample uncertainty, not immunity to systematic environmental bias.

Engines used CPUs 0–7, with SMT siblings 16–23 reserved and excluded from engine affinity. During 64 measured runs, reserved /proc/stat busy minus whole-child CPU averaged 0.14761 cores (maximum per-run 0.39804); reserved siblings accumulated only 0.10 CPU-seconds. Complementary CPUs averaged 4.53651 busy cores, maximum 15.99155. Outside work is expected under the verified exclusive partition; residual accounting includes wrapper/kernel activity and coarse-jiffy/boundary skew and is not process attribution. Shared power, thermal, memory-bandwidth and interrupt effects remain possible. Boundary frequency snapshots are not interval-effective frequency. Earlier non-isolated absolute timings use a different environment and cannot be directly pooled.

[Full isolated priority evidence](gradient-reuse-v2-isolated-priority.zip) retains all raw results/logs, commands, timing inputs and audits, independent summary/activity analysis, exact runner sources and binary hashes. The candidate remains provisional pending full 98-window, separate eight-team/custom-resource performance, candidate platform checks and final integration review. Three-reference attribution is scheduled separately; no old percentage gate determines completion.

## Three-reference priority attribution under exclusive cpuset

Both reference campaigns completed one warmup pair plus 16 measured alternating pairs per 8192-tick Maxima/Cortex window: 136 total children, 128 measured. Independent reread verifies complete windows, alternating order, exact recomputation of all geometric ratios/95% bootstrap intervals and medians, unchanged source/binary/data/library/fixture snapshots, repeat-stable outputs, and full governor/cpuset/affinity restoration with no forced kill. These are reference-cost comparisons, **not the gradient optimization result or a gameplay-equivalence claim**. Final checksums differ between all three engines; intended behavior fixes and resource representation changes prevent interpreting raw cross-reference checksum differences as an equivalent-execution test.

| Comparison | Window | Simulation CPU ratio [95% CI] | Simulation wall ratio [95% CI] | Whole-child CPU ratio [95% CI] | Peak RSS ratio [95% CI] |
| --- | --- | --- | --- | --- | --- |
| Pure pre-refactor → approved fixes | Maxima | 1.051974 [1.043717, 1.062165] | 1.083044 [1.072399, 1.096615] | 1.049464 [1.041128, 1.059598] | 1.001179 [0.989596, 1.010980] |
| Pure pre-refactor → approved fixes | Cortex | 1.011841 [1.008038, 1.016235] | 1.012435 [1.008364, 1.016771] | 1.012032 [1.008430, 1.016179] | 1.000000 [1.000000, 1.000000] |

Pure pre-refactor → approved fixes equal-scenario aggregate simulation CPU ratio: **1.031712 [1.027188, 1.036959]**. Reserved busy minus whole-child CPU averages 0.176988 cores, maximum per run 0.362797; complementary CPUs average 2.344543 busy cores, maximum 13.464398. Reserved SMT siblings total 0.05 CPU-seconds.

| Comparison | Window | Simulation CPU ratio [95% CI] | Simulation wall ratio [95% CI] | Whole-child CPU ratio [95% CI] | Peak RSS ratio [95% CI] |
| --- | --- | --- | --- | --- | --- |
| Approved fixes → merged9084 | Maxima | 1.016682 [1.006697, 1.027823] | 1.022085 [1.013494, 1.030273] | 1.016934 [1.006928, 1.027997] | 1.053157 [1.041109, 1.064832] |
| Approved fixes → merged9084 | Cortex | 0.904629 [0.901049, 0.908133] | 0.886511 [0.882846, 0.890295] | 0.906932 [0.903280, 0.910417] | 1.000000 [1.000000, 1.000000] |

Approved fixes → merged9084 equal-scenario aggregate simulation CPU ratio: **0.959020 [0.953669, 0.964435]**. Reserved busy minus whole-child CPU averages 0.177586 cores, maximum per run 0.365592; complementary CPUs average 3.000289 busy cores, maximum 12.801209. Reserved SMT siblings total 0.06 CPU-seconds.

The approved fixes add 3.17% aggregate CPU [2.72%, 3.70%]; the subsequent merged engine uses 4.10% less aggregate CPU [3.56%, 4.63%] than that approved control across these two fixed windows. Maxima and Cortex differ substantially, so aggregate values do not replace individual results. Do not multiply separately measured ratios to manufacture a direct pure→merged confidence interval. The historical `regression` label in the first report is retained as a threshold diagnostic; report-only exited successfully and the former gates do not define acceptance.

Simulation CPU covers all threads through finishSession/final gradient drain and excludes setup/requested save; whole-child wait4 CPU includes setup and exit. Busy residuals include wrapper/kernel activity and coarse-jiffy/boundary skew, not attribution to unrelated processes. Outside work is expected under the verified partition; shared power, thermal, memory-bandwidth and interrupt effects remain possible. Bootstrap intervals quantify sampling uncertainty, not systematic bias. Flat reported Cortex RSS reflects the raw OS measurement; no finer memory-cost claim is inferred. Timing outputs do not replace per-tick compatibility evidence.

[Raw attribution evidence](isolated-reference-attribution.zip) contains every measurement/result/log, commands, input and restoration audits, independent analysis and exact runner sources. No optimization-retention conclusion follows from these reference campaigns.

## Completed isolated 98-window legacy campaign: aggregate benefit with unresolved individual costs

Frozen merged77fb → v2 candidate4a068 completed all 98 windows, one warmup plus eight measured alternating pairs each: 1764 children, 1568 measured runs/784 measured pairs. Independent audit recomputes all reported ratios, 4000-draw bootstrap intervals and medians exactly, validates exact command order/arguments and every window length, and confirms equal initial/final checksums across both variants and all repeats. These are final checksums, not per-tick traces. Frozen binary-role bindings, metadata/input audits, actual governor stabilization/restoration and exclusive cpuset cleanup pass; no forced kill. Per-tick and pending-save evidence remains separately linked above.

| Equal-window aggregate metric | Candidate/reference ratio [95% CI] |
| --- | --- |
| simulation_cpu_s | 0.978905 [0.977087, 0.980760] |
| simulation_wall_s | 0.985114 [0.983559, 0.986659] |
| cpu_s | 0.980214 [0.978470, 0.981962] |
| wall_s | 0.986387 [0.984986, 0.987846] |
| peak_rss_bytes | 1.033454 [1.032499, 1.034436] |

Aggregate simulation CPU decreases 2.11% [1.92%,2.29%], but 54/98 windows have CPU point slowdowns, with 34 per-window intervals wholly above 1. Of 98 windows, 26 have wall point slowdowns (three intervals wholly above 1), and 69 have RSS point increases (45 intervals wholly above 1). Intervals are unadjusted for multiple comparisons and identify investigations, not automatic retention/rejection. The old percentage gates do not decide acceptance. The worst CPU point increases cluster in water Nicowar/mixed windows, while the Nicowar group overall benefits; do not generalize the subset to the whole AI.

| Largest CPU point slowdowns | CPU ratio [95% CI] | Wall ratio [95% CI] | RSS ratio [95% CI] |
| --- | --- | --- | --- |
| water-2-1002-nicowar-early | 1.049073 [1.027843, 1.072725] | 0.999264 [0.967156, 1.043671] | 0.999529 [0.998824, 1.000000] |
| water-2-1002-nicowar-middle | 1.048501 [1.013260, 1.079686] | 1.004655 [0.974064, 1.035423] | 1.000000 [1.000000, 1.000000] |
| water-4-1002-nicowar-middle | 1.042474 [1.020665, 1.063866] | 0.991455 [0.971755, 1.011306] | 1.000000 [0.999485, 1.000516] |
| water-4-1002-nicowar-early | 1.039635 [1.016229, 1.064044] | 0.981523 [0.962151, 1.002431] | 1.000173 [0.999653, 1.000693] |
| water-4-1002-mixed-early | 1.039428 [1.023537, 1.056349] | 0.998126 [0.988185, 1.009503] | 0.999831 [0.999494, 1.000000] |

Wall intervals wholly above 1 occur for land-4-1001-maxima-late (+0.695%),water-2-1002-mixed-late (+0.561%) and water-4-1002-maxima-late (+0.230%). Worst RSS point increase is land-2-1001-cortex-middle (+14.06%, interval+13.29% to+14.73%). Exact per-window values and all slowdown ranks are in the archive. These costs are not yet causally explained; the candidate remains provisional pending attribution, separate large/custom stress and final review.

Measured reserved busy minus whole-child CPU averages 0.158124 cores (maximum 0.483322); outside CPUs average 7.523006 busy cores, maximum 16.020142. SMT siblings accumulate 1.16 CPU-seconds. Slightly negative residuals and the outside maximum just above 16 are retained coarse-jiffy/boundary effects, not clamped. Residuals include wrapper/kernel work and do not attribute processes. Outside work is expected under verified exclusivity; shared memory/power/thermal/IRQ effects remain possible. The wrapper enforces complementary allocation on every check, but saved snapshots only retain child masks, limiting independent reconstruction of parent history.

[Complete raw corpus evidence](gradient-reuse-v2-isolated-legacy.zip) retains all 1764 outputs, commands, measurements, summaries, independently rerunnable audit and source/provenance/restoration data. No frozen engine binary or duplicate input-save tree is embedded; their exact hashes and prior fixture archives identify them.

## Ranked remaining investigations and evidence rejections

1. **Indexed-ring candidate rejected with controlled CPU evidence.** Original production code is restored. Preserve the diagnostic reduction in instructions alongside the null aggregate CPU result and Maxima slowdown; do not resurrect this patch from instruction counts alone.
2. **Exact-input diagnostics now demonstrate repeated periodic propagation; a bounded reuse candidate is under investigation.** Across all 8192 ticks, Maxima repeats 2187 of 8160 comparable prepared inputs (26.80%); Cortex repeats 2221 of 8151 (27.25%). Material fields account for 1662/6120 and 1600/5966 comparable duplicates respectively; the remainder are guard/clear fields. Complete seed/water/terrain contents and scalar parameters match, with zero conservative immutable-registry/profile pointer misses in either window. Both instrumented full 8192-record traces match frozen merged execution byte-for-byte; inputs remained unchanged. This is approximately 27% of comparable **job counts, not CPU cost or saved CPU**. Periodic propagation accounts for 30.02% of Maxima samples; another 5.89% is synchronous propagation and is outside this opportunity. Low discarded-job counts (89 Maxima, 5 Cortex) still limit cancellation, but cannot bound duplicate-input reuse. A new candidate must preserve fixed publication deadlines, synchronous invalidation, worker/error behavior and pending-save continuation, and earn retention through controlled CPU measurements. The isolated priority result above now demonstrates a repeatable CPU benefit in those two windows; corpus-wide retention remains undecided. [Complete diagnostic evidence](gradient-input-identity.zip) includes the exact patch, frozen binary hash, build provenance, commands, counts, input audits and full before/after traces; the binary itself is omitted.
3. **A second full-map seed enumeration is demonstrable but bounded.** Producers seed every cell; `propagateField` then scans the entire buffer to enqueue seeds. Sampled initial seed-scan instructions account for roughly 5.4% of the 30.02% Maxima periodic kernel, about 1.6% of total samples attributable to that part. This is an approximate cost bound, not a speedup projection. Carrying ordered seed indices from preparation might avoid the scan, but requires extra per-job storage, correct supplier/deferred-cost seeds, parallel-initialization ordering and completed-snapshot save semantics. No implementation is justified before frequency/cost measurement establishes that these costs outweigh new work.
4. **Do not repeat existing propagation optimizations.** Current kernels already hoist coordinates/terrain costs/destination buckets, specialize uniform swim classes, vectorize relaxation, reserve append capacity by chunks and discard stale queue entries. Historical commits `9e8b378eb` and `05207be36` cover earlier repeated-work reductions; current seed templates already deduplicate dirty cells and patch goals/fog/forbidden state. Growth/seed caches are not unexplored blank slates.
5. **Complete toroidal Food distance field: rejected by existing diagnostic evidence.** The prior implementation preserved 8192 GCS records but increased instructions from 172.600 billion for the byte snapshot to 181.968 billion (+5.43%). Construction overwhelmed the avoided scans. Retained source/profile evidence prevents treating that approach as untried.
6. **Per-coordinate nearest memo: reject before implementation.** Each placement pass visits a coordinate at most once for its cap-5 food gate; only four retained candidates repeat at cap 12. Non-food buildings make only those four queries. A map-sized memo would add writes for predominantly unique requests to save at most four small scans.
7. **Reusing the existing food snapshot in cluster counts is low priority.** The current direct count accounts for 0.62% of Cortex samples. It may remove duplicate material queries but does not presently support a substantial-win claim. Maxima discarded expansion-query/hoisting ideas likewise had less than 1% old self attribution.
8. **No evidence for ecology mutex removal or mixed-stock layout redesign.** Existing profiles do not attribute meaningful contention/storage cost to those mechanisms. Keep them as unproven investigations, not committed changes.

Cross-platform per-tick verification and final corpus/stress performance remain separate requirements. Expanded gameplay trials, physical ARM/iOS testing and maintainer gameplay review remain deferred with the original documented limits.
