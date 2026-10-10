# Existing pitch24 candidate: final measurement

Recommend integrating the already-coded frozen-halo pitch24 candidate. This recommendation is supported by its native propagation results and exactness checks, especially the 512-map capture subgroup. It is not a claim of whole-game speedup, universal improvement, or an exclusive-GPU measurement. No further variants or timing repeats were created.

## Frozen inputs and commands

Baseline kernel comes from production `ea72d9add`, original host SHA256 `8ac0430c9781d79dfd94fb9120a46c833e40e4b866ce97c945102c9030a03ace`; embedded baseline SHA256 `b3d17fa93506a378cca7a22aa15015ab173783f729f7ec337e0adad853e73c41`. Candidate embedded SHA256 `ee15f6d2cd7cdd07fdb7d4378bd39410e7bc769b69608766373e1129705800b6`.

Only the existing baseline, pitch24 and byte-identical baseline-copy control were measured. The mutable production host was not an input. `frozen-inputs.json` records all reused native/helper hashes; copies of source inputs are in `frozen-runtime-sources/`. The minimal `candidate.patch` changes only the embedded kernel, applies to the current host cleanup, and passed `git apply --check`. Patch SHA256 `cea4d462d6b7c65ffa67eca474d7192f4b0fe020eafe31372452c2f35ad3bcd6`.

From the repository root:

```text
artifacts/gpu-gradients/kernel-lab/venv/bin/python artifacts/gpu-gradients/kernel-memory-fresh14/run-guarded.py broad
artifacts/gpu-gradients/kernel-lab/venv/bin/python artifacts/gpu-gradients/kernel-memory-fresh14/run-guarded.py correctness
taskset -c 24-27 artifacts/gpu-gradients/kernel-lab/venv/bin/python artifacts/gpu-gradients/kernel-memory-fresh14/assembly14.py
```

The coordinator released GPU work only after all 48 whole-match repeats completed. Both GPU locks were held during each native phase and during each actual-driver compilation. All agent GPU processes finished and released those locks afterward. No production edit was made by this agent.

## Native performance

One predefined run: 46 existing changing captures, two existing eight-field batches, all six existing kernel configurations, two full warmup passes, 21 randomized/interleaved samples per configuration, and sample zero discarded. There are 17,280 retained native samples and 23,436 recorded exact field outputs. Allocation/compilation and exact comparisons are outside timing; uploads, host submission, convergence checks and output reads are inside. The frozen native harness still uploads final retired descriptors, unlike current production; both sides use that same cadence.

The table sums per-field median times while retaining the baseline's kernel selected from the first captured field of each shape/family group. All three groups initially select frozen8/128. This avoids selecting the fastest kernel separately for every field.

| Captures | Count | Pitch24 change | Identical baseline-copy change | Pitch24 field wins |
|---|---:|---:|---:|---:|
| All | 46 | **−3.34%** | +0.34% | 36/46 |
| 256×256 | 25 | −1.59% | +0.67% | 15/25 |
| 512×512 | 21 | **−4.10%** | +0.20% | 21/21 |

Allowing each candidate its own single first-field choice yields −5.11% overall and −6.63% for 512 captures, where pitch24 first selects frozen16/64. This is a selection model over development captures, not an observed automatic-mode game result. Excluding initial selection fields retains −3.18% with fixed choice and −5.03% with candidate-specific first choice.

Across all 46 captures for each frozen configuration, frozen8/128 improves summed medians by 3.34% (36/46 wins), and frozen16/64 by 5.64% (45/46 wins). Their two-batch summed medians improve 10.15% and 3.33%, respectively. The 256 subgroup is less consistent: its worst fixed-choice field is +12.12%; retain that observation. All six-config results, including losses and identical-copy variation, are in `confirmation-summary.json`, `broad-results.json` and `first-selected-results.json`; width subgroups are in `size-summary.json`.

## Assembly and storage consequences

Actual NVIDIA driver cache cubins were extracted and disassembled; these are not PTX recompiled by another assembler.

| Configuration | Baseline → candidate static instructions | Registers | Shared bytes |
|---|---:|---:|---:|
| Jacobi4/256 | 688 → 688 | 43 → 43 | 5636 → 5636 |
| Expanding2/128 | 720 → 720 | 44 → 44 | 2920 → 2920 |
| Expanding4/128 | 720 → 720 | 44 → 44 | 3976 → 3976 |
| Expanding8/256 | 728 → 880 | 44 → 46 | 7176 → 7176 |
| Frozen8/128 | 672 → 672 | 43 → 43 | 2464 → 3112 |
| Frozen16/64 | 664 → 664 | 47 → 47 | 2208 → 2856 |

Every configuration has zero local memory/stack in the resource output. The first three cubins are byte-identical. The semantically unchanged expanding8 configuration compiles differently after the index expression rewrite: its capture sum is −0.54% versus identical-copy −0.47%, with no material observed speed difference. This compiler effect is explicitly retained rather than claiming every expanding variant has unchanged machine code. Static instruction counts are not dynamic instruction measurements, and no occupancy measurement was made.

## Correctness

The existing inventory passed: 5,310 exact field outputs across baseline, pitch24 and copy, including **1,770 pitch24 outputs**. This includes captured fields, heterogeneous corner batches, tiny/odd/rectangular tori and the pre-existing long convergence guard. Every candidate guard dispatch count equals baseline; both frozen variants require **9,360 dispatches** and complete exactly. `correctness-summary.json` records all configurations. Earlier independent Clang OpenCL 1.2 checks and the CPU coordinate/differential proof also passed.

The independent model and native corpus support the address remapping, but final production engine/replay integration checks remain the coordinator's responsibility. Other GPU vendors and other operating systems were not tested.

## Environment and limits

Linux x86_64, NVIDIA RTX 2070 SUPER GPU1 (`GPU-78234fbe-b975-9f64-2209-9bd4acec265b`), driver 580.178.04, unprofiled OpenCL queue, no events. Native submission was on CPU7, the affinity observer on CPU15, with game benchmark policy fixed at eight compute workers. The native harness itself has one submission thread; it is not an eight-worker full-game run. Background builds were permitted on disjoint cores. The guarded broad phase completed in 25.86 seconds and the correctness phase in 7.83 seconds with no detected protected-core overlap.

This was a **shared desktop GPU**, not an exclusive compute device. Locks exclude cooperating agent GPU jobs but cannot exclude desktop/compositor or other GPU use. Ninety-six GPU endpoints were captured immediately around the 48 performance cases: temperature 57–65°C; observed SM clocks 1605/1905/1920 MHz; memory 6801 MHz; P2. Clocks were not locked, and endpoint samples do not establish continuous clocks or utilization during every timed call. GPU process inventories and separate compilation/assembly phase endpoints were not recorded. Identical-source controls and randomized order reduce interpretation risk but do not remove those limitations.
