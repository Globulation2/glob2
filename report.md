# PR1045 optimization and consolidation

[Eight-map two-pass study](reports/whole-games-ea72.md) · [Final large-map check](reports/whole-games-final.md) · [Pitch24 measurement](reports/kernel-pitch24.md) · [Activation measurement](reports/kernel-activation.md) · [Initial kernel pool](reports/kernel-pool.md) · [Archive and reproduction](README.md)

This report supersedes the initial PR review evidence for the revised implementation. The final source retains exact integer GPU gradients, independent per-worker execution queues, a compact six-kernel pool, and adaptive placement. Unsuccessful prototypes remain evidence artifacts rather than runtime switches. The opt-in asynchronous device helper is removed after the measured worker path proved faster in its completed comparison.

## Source and validation

Final source is `c2ab2628bad246b616d0cd03d73fb03533877503`, with release game SHA256 `260ac3b46cd8621fe11ad4bd08b8822a427cbc0d805591b2694135ed25979212`. Source/build hashes, integration base, exact commands and final test counts are recorded in `artifacts/gpu-gradients/campaign/consolidated-final/validation.json` inside the archive. Current master `978a0ea18a04f9952ae52f63064021cebc2fc926` merges without conflicts (merge tree `383a13089a96f00b5a7b47558840da04a782f261`); validation executes the PR head, not a merged binary. The preceding `ea72d9add7d15936c4a3c7688ffb1858eb27c2b9` release passed 97 unit and 113 engine cases plus 42 CLI runs. The final c2ab consolidated release passed 91 unit and 113 engine cases plus the same 42 CLI runs. The six removed unit cases exclusively exercised the removed helper or its request queue-delay field; retained behavior was not skipped. CLI verification compares per-tick simulation traces, replay bytes, final saves, and cross-backend checkpoint continuations, as well as outputs from the preceding validated source. No simulation revision or save/replay format change is intended.

The eight-map whole-game campaign froze ea72 source and binary before all 48 runs. The final source removes the disabled helper and includes the measured pitch24 kernel; its release binary and bounded large-map confirmation are recorded separately. Different binary revisions are never pooled into repeats.

## Retained kernel work

The compact pool uses the same 16×16 output core with independent 64/128/256 workgroup sizes, one Jacobi fallback, expanding-halo four-color updates, and frozen one-cell-halo local convergence. Factored signed acceptance arithmetic, uniform-cost paths, fewer reduction barriers and a 65,536-global-dispatch guard preserve exact fields. The long 32×32768 regression field requires 9,360 exchanges on frozen variants and passes.

The first broad 46-field development comparison found kernel-only geometric speedups of 1.182× at 256² and 1.310× at 512², rising to 1.272×/1.383× with the host flag-clear change. These are captured-field measurements, not complete-game gains. Inputs came from two development fixtures; already-fixed fields are reported separately. The final game cohort covers different generator/seed pairs.

Parallel tile activation replaces one work-item's serial neighboring-tile atomics with one atomic per participating work-item. Existing barriers publish eight immutable partial flags; the implementation has no subgroup-width assumption. The46-field fixed-choice sum fell 2.43% on GPU1 and 1.61% on GPU0; the512 subset on GPU0 fell 1.39%. Identical-source controls and all outliers are retained. These modest effects do not imply the same full-game change.

Actual NVIDIA driver assembly was extracted and inspected. The activation epilogue removes 400–440 static instructions per configuration, with zero spills/stack in all 18 baseline/generic/specialized variants. Register changes vary by configuration. Static instruction counts are not dynamic instruction timings. No per-instruction cycle measurement, occupancy improvement or dynamic instruction-count reduction is asserted.

The final already-coded pitch24 candidate retains the logical 18×18 frozen patch while padding rows to 24 elements. With the baseline first-field kernel choice fixed, the 46-field native sum fell 3.34%, and all 21 changing 512² fields improved for a 4.10% subgroup reduction; identical-source controls were +0.34% and +0.20%. The 256² subgroup improved 1.59% overall but retained a +12.12% worst field. These development measurements support the large-map priority, not a universal win. Candidate-specific first-field choices give stronger estimates, reported only as secondary selection-model results.

All 1,770 pitch24 native field outputs passed, including corners and the 9,360-dispatch guard. Frozen variants retain the same instruction/register counts and no spills, while adding 648 bytes of local storage. Three non-frozen cubins are byte-identical; expanding8 compiles to more instructions/registers after the equivalent index rewrite but measured neutral against its identical-source control. The final embedded kernel is byte-identical to the measured pitch24 source, SHA256 `ee15f6d2cd7cdd07fdb7d4378bd39410e7bc769b69608766373e1129705800b6`.

## Completed alternatives

| Candidate | Disposition |
|---|---|
| Frozen-halo convergence, compact workgroups and arithmetic/reduction changes | Retained with exactness and captured-field evidence |
| Parallel tile activation with constant3/5 coordinate mapping | Retained after cross-device paired confirmation |
| Already-fixed field scan | Retained; exact scanner and native API equivalence recorded |
| Cached source strengths / packed mixed-cost alternatives | Rejected: synthetic gains did not transfer to ordinary captures |
| Extra barrier merging / unrolling / scalar flags / goal shortcuts / restrict | Rejected: no robust broad benefit sufficient for added complexity |
| Skip unread field-global atomics | Rejected: neutral versus identical-source controls |
| Bounded calibration and two-sample CPU proof | Rejected for ordinary workloads despite useful pathological-case behavior |
| Optional asynchronous submission helper | Removed; measured complete-match regressions |
| Frozen-halo physical pitch24 | Retained: one predefined paired run, independent layout model, native corners/guard and actual driver assembly |

Historical reports retain their original source hashes and unresolved statements as audit history. Later confirmation resolves the early128-maze activation outlier; it did not reproduce. A512-maze follow-up was highly noisy, including its byte-identical control. No sample was silently trimmed to create a win.

## Whole-game evidence

The frozen ea72 campaign completed 48/48 exact, valid runs with zero exclusions or observed reserved-core overlaps. Across the three 512² maps, automatic had +3.03% runtime/+2.85% full-wall regret against the faster forced backend, with worst map +5.58%/+5.39%. Forced GPU versus CPU was approximately flat (−0.35% runtime/−0.03% full wall). Automatic's process CPU was 10.07% lower, and forced GPU's 12.77% lower, without a corresponding elapsed-time win. Across all eight maps automatic's regret was +2.73%/+2.98%; worst map +16.17%/+16.25%. These two-observation medians are descriptive and both passes/ranges are retained in the linked eight-map study; differences of a few percent remain uncertain. The final c2ab binary completed 9/9 exact, valid observations on the same three 512² fixtures, with no excluded timings or observed reserved-core overlaps. Its automatic aggregate was −0.13% runtime/+0.28% full wall versus CPU, which was the faster forced mode in all three cases. Automatic won bajada and contested-commons but lost watershed by +6.41% runtime/+6.36% wall. Forced GPU averaged +5.47% runtime/+5.29% wall versus CPU. This one-pass final check supports an aggregate near tie for automatic, not consistent superiority or an isolated pitch24 game speedup. See the linked final large-map check for every runtime, process-wall result and audit. All performance compute counts are fixed at 8. The eight predeclared maps are even-ground 128², islands 256², maze 256², canals 256², watershed 512², bajada 512², swamp 128² and contested-commons 512². Each uses a saved initial fixture, fixed map/game seeds, four AI players, 20,000 ticks, and exact original-CPU checksum/save references. Two rotated passes of all three modes give 48 runs. Run time includes all 20,000 ticks; process wall also includes startup and saving. The 1,000-tick warmup only affects tick-statistic windows.

CPUs 0–7 are reserved for games and SMT siblings 16–23 are excluded from recognized background builds. Background builds on 8–15/24–31 are permitted. A 0.5-second affinity watcher records overlap; the guardian's original-mask restoration is included. Historical earlier campaigns used different CPU affinity and are not pooled.

Both RTX 2070 SUPER GPUs also run desktop graphics. Endpoint telemetry observed competing graphics work. Cooperative GPU locks exclude only our own jobs, and CPU reservation does not provide an exclusive GPU or an idle machine. Timing uncertainty and the small repeat count remain material.

## Assessment and limits

The kernel approach is effective on expensive captured gradients, but whole-game performance also includes preparation, copies, driver submissions, calibration, other simulation work and publication waits. Automatic placement can still pay exploration costs and choose poorly under changing contention. Faster kernels alone do not establish that automatic mode beats both forced backends; the final table reports its actual regret.

Linux x86_64, GCC 15.2, NVIDIA 580.178.04, Ryzen Threadripper 2950X and two RTX 2070 SUPER cards are the exercised environment. Both cards share one architecture. Windows/macOS/AMD/Intel execution and cross-platform per-tick checksum equivalence remain unverified. Earlier Android-disabled stub and injected-failure/TSAN checks retain their exact revisions; they are not relabeled as final-source executions. No expensive hosted matrix or maintainer playtest is claimed. The PR remains draft and is not merged.

## Reproduction and evidence

Build used the existing SDL3 and recording prefixes with `CCACHE=1 taskset -c 24-27 scons release=1 server=0 linker=auto -j4 build/linux/client/release/dev-linker-auto/src/glob2 unit-tests engine-tests`. Prefix paths, compiler/dependency details, compile/link commands and final binary hashes are retained in the archive and final manifest. These are release `-O3` builds, not fast-development/PCH/unity performance comparisons.

After restoring archive paths into the checkout, the final correctness command is:

```sh
taskset -c 24-27 flock /tmp/glob2-gpu-optimization-gpu0.lock \
  flock /tmp/glob2-gpu-optimization-gpu1.lock \
  bash artifacts/gpu-gradients/campaign/consolidated-final/validate.sh
```

The script records the exact targeted unit/engine filters, JUnit inventories and 42 CLI invocations. Existing fixture/capture hashes, full match commands, map/game seeds and mode schedules are retained in `wholematch-fresh3/*-plan.json` and each campaign's metadata/results. Native kernel commands and actual-driver extraction commands are in the linked component reports. Scripts retain original local dependency/fixture paths; adjust those paths when reproducing on another checkout.

The archive preserves raw logs, exact saves/replays/checksums, all candidate results, source snapshots, disassembly and exclusions. Executables/dependency caches are excluded and identical bytes are deduplicated. `README.md` explains checksum verification and restoration. Historical performance strata and source revisions remain separate. Final CPU-affinity restoration receipts are included. No new experiments or follow-on agent work remain scheduled.
