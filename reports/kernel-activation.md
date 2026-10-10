# Parallel tile activation candidate

The proposed change replaces one work-item's serial neighboring-tile atomic updates with one update per participating work-item, while keeping the eight partial change flags immutable after their existing barrier. It preserves all local relaxation, halo, tile mask, convergence and host scheduling behavior. The production source was not edited by this investigation.

The preferred candidate is `warp_special`, whose 3/5-wide coordinate mapping uses compile-time divisors. “Warp” is only the experiment name: neither correctness nor source code assumes a subgroup width or NVIDIA-specific operation. The proposal's comment uses “work-item.” See `proof.md` for ordering and exact activation-set reasoning. The parent adopted its executable code in commit `ea72d9add7d15936c4a3c7688ffb1858eb27c2b9`, with additional explanatory comments; parent integration validation is separate from this report.

## Paired performance

Hardware is the existing Linux x86_64 RTX 2070 SUPER GPU 1, NVIDIA 580.178.04. CPU 7 submits benchmarks; observerCPU 15 rejects overlapping build/test affinity on reserved CPU 0–7 and 16–23. Disjoint background builds are allowed and logged. All performance phases passed that guard. Game compute workers remain fixed at 8; kernel workgroup sizes are the existing 64/128/256 configurations.

The native C++ O 3 submission harness uses an unprofiled OpenCL queue and requests no events. Timings include seed/descriptor upload, ordinary tile-mask fills, dispatches, convergence checks, field retirement and output readback. Cost buffers are resident; allocation, compilation and output comparison are outside timings. Its preserved historical host cadence includes the final retired-descriptor upload equally in every comparison. It is not an end-to-end game timing.

The 20-case screen used 6 changing captured singles, two 8-field batches, and open/mixed/dense/maze controls at 128²/256²/512². Each of 18 variants (baseline/generic/specialized ×6 configurations) ran 2 complete warmup passes and 15 sampled passes. Sample 0 was also excluded from medians. All 6,120 executions were exact. Specialized candidate/base geometric time ratios were 0.978 for captured field/config pairs,0.971 for batches and 0.983 for controls. These were screening results, not the final selection claim.

The confirmation used all 46 changing captures plus two 8-field batches. It added a separately named kernel with byte-identical baseline source, giving 24 variants. Every case ran 2 complete warmup passes plus 17 sampled passes, with sample 0 excluded:16 samples formed each median. All 21,888 executions /28,272 field comparisons were exact. The capture-wide field/config geometric time ratio was 0.976 for specialized,0.979 for generic and 0.997 for the identical-source control. Individual outliers were much larger than these averages, including in the identical-source control; do not infer a universal improvement from individual medians.

To avoid per-field oracle selection, one kernel configuration was chosen from the numerically earliest captured sequence in each size/family group and then held fixed. Sequence comes from the binary capture header, not lexicographic filename ordering. The specialized candidate chose the same configurations as baseline. The 46-field sum changed 18.9477375→18.4880260 ms, a 2.43% reduction. The identical-source control at those same configurations changed 18.9477375→18.9656635 ms, a 0.095% increase. Specialized improved 38/46 field medians; the largest observed regression was 3.16%. Excluding the 3 initial selection fields still gives 2.21% lower summed time over 43 fields. See `first-selected-results.json` for every field and retained configuration.

Generic mapping reduced the sum 1.78% when holding baseline choices fixed, versus 2.62% when independently choosing its initial configuration. Its worst fixed-config field was 16.5% slower in this sample. Specialized is preferred for its simpler compiled coordinate arithmetic and stronger fixed-config confirmation, not because it wins every case. On the broad two-batch screen, specialized's aggregate across configurations was approximately neutral (0.997 time ratio), while generic was 0.985. Whole-game speedup remains unmeasured for this candidate.

These captures are development data from two prior game fixtures. The parent campaign's separate procedural-map holdouts remain necessary for whole-game generalization; no claim is made that this microbenchmark covers every gameplay distribution.

An independent review identified a 31.2% initial-screen regression on the 128² maze with step 2/128 work-items, and an 11.6% regression with frozen 8. A predeclared follow-up reran all four 128² controls plus 256²/512² mazes, all six configurations, candidate and identical-source control, with 41 sampled passes plus two warmups and sampled 0 excluded. All 4,644 executions were exact. The flagged 128² maze comparisons became +0.28% and −3.05%; the worst candidate median across all 128² control/configuration combinations was +0.71%. This resolves those large initial small-map outliers without selecting a preferred configuration. The follow-up newly showed +3.98% and +5.81% on two 512² maze configurations, so those were checked directly.

One final dedicated 512² maze comparison used all six configurations and 60 retained back-to-back paired samples per configuration. All six permutations of baseline/copy/candidate order occurred equally; configuration order rotated and reversed. Two complete warmups and sampled 0 were excluded. All 1,134 executions were exact, and every mode retained identical dispatch counts. Candidate/baseline paired median ratios were 0.924, 0.946, 0.966, 0.968, 0.965 and 0.936 in configuration order. The two newly flagged regressions did not persist. However, the byte-identical control's paired medians ranged 0.929–1.072 and its median absolute deviations were 0.078–0.164. These apparent improvements must not be reported as stable 3–8% gains. `mazeconfirm-paired.json` retains all configurations, paired medians, dispersion, percentiles and first/second-half comparisons.

The added phase-boundary process monitor established a material measurement limit: GPU 1 also runs the desktop. Before the final dedicated run, gnome-shell reported 10% SM usage and Xwayland 8%; afterward gnome-shell reported 41% while benchmark Python reported 44%. Endpoint temperature rose 56→63°C and SM clock 1605→1905 MHz. This directly observes competing GPU work during that phase; it does not identify the cause of each earlier timing outlier. CPU reservation and agent GPU locks do not make these runs GPU-exclusive. The broad 2.43% captured estimate remains useful paired evidence with uncertainty, not a precise whole-game gain or no-regression guarantee. No desktop processes were stopped or changed.

That concrete interference evidence motivated one cross-device confirmation on GPU 0, the device used by the whole-game campaign. It retained the same frozen source, 46 singles and two batches, all six configurations, and baseline/copy/specialized candidates. Within each configuration, order rotated through all six permutations; configuration order rotated and reversed. There were 17 sampled passes plus two complete warmups, with sampled 0 excluded. All 16,416 executions /21,204 field comparisons were exact. First-numeric-field fixed choices reduced the sum 17.958615→17.668838 ms, or 1.61%, compared with a 0.67% increase for the identical-source copy. The size-specific reductions were 2.13% at 256² and 1.39% at 512²;40/46 field medians improved. Across all 276 field/config pairs the geometric ratio was 0.983 versus 0.999 for the control. All 12 batch/config pairs improved, geometric ratio 0.986. This confirms a modest captured-field effect on the second GPU, not a whole-game speedup.

GPU 0 was also shared: the remote-desktop process showed 4% SM usage before and 30% afterward, while benchmark Python showed 32% afterward. Endpoint temperature rose 58→64°C and SM clock 1605→1905 MHz. This is a second device confirmation under shared graphics, not an exclusive-GPU experiment. `device0confirm-environment.json` and `device0confirm-first-selected-results.json` record the process evidence and every retained field/config choice. No further performance reruns were performed by this subtask after this bounded confirmation.

## Exactness

- CPU activation-set model:9,417 map shapes,136,145 tile groups,2,883,321 mapped neighboring-tile operations; all 256 Boolean partial-flag combinations agree in every publishing work-item. Includes tiny/odd/rectangular shapes, both 32×32768 orientations, thin maps and large squares.
- Native GPU final check: baseline, generic and specialized across all 6 configurations ×134 captures +160 extreme/cap/tiny/odd/rectangular fixtures +the long guard field =5,310 exact field comparisons in 684 native batches. This is 1,770 fields for the adopted specialized candidate; the remainder cover baseline and generic.
- The 32×32768 alternating-tile guard passed all 18 variants; the frozen-halo paths still require 9,360 global exchanges, preventing a short convergence guard from masking the test.

The final GPU correctness run uses the same frozen kernel sources as the performance run. `candidate-measured.cl` is the measured specialized source, SHA256 `2f538563522685eb67832cea2001694d5f502eb62c37b86bf2374d52c523cace`. `candidate.cl` changes only one comment from “lane” to “work-item”; executable tokens are identical. Its SHA256 is `398164b1cc80075fb8a801ebded17d7d422611bd4ac90c6d37f9c81f4e68fdcc`.

The minimal `candidate.patch` applies only to the kernel epilogue against frozen production C++ SHA256 `d46be8c57b4fb623e2603a04186bbd82e0d08a480c34269e0d6d3c3671ae1161`. Proposed C++ SHA256 is `ff6569a1876bb94cb0e28edaa57117785f144adb8dc00acf273cdb2f6bc6cf57`. No policy, host API, save format, field representation or simulation rule changes are included. Parent integration and game replay/save validation still need to be run after application.

## Actual driver assembly

All 18 baseline/generic/specialized driver-generated cubins were extracted from isolated NVIDIA caches and disassembled. This is actual driver output, not a ptxas recompilation. All have zero local spills and zero stack, with identical shared-memory usage per configuration.

Specialized static instruction count falls by 400–440 per configuration (roughly 35–38%). The common frozen 8 variant changes 1080→672 static instructions and 45→43 registers; frozen 16 changes 1064→664 instructions and 45→47 registers. The Jacobi configuration's register count rises 36→43. These changes are recorded rather than converted into unsupported occupancy or cycle claims.

The serial epilogue generated many repeated wrap/atomic instruction blocks. The specialized candidate has one shared coordinate path with constant division by 3 or 5. Generic adds one reciprocal-based runtime division relative to specialized. Normal power-of-two maps already bypass generic wrap division, so the fall in total reciprocal opcode count is not a claim that all removed reciprocal instructions used to execute on normal maps. Actual paired runtime, not static count, supports the modest 2.4% measured captured-field improvement.

## Reproduction

```
artifacts/gpu-gradients/kernel-lab/venv/bin/python artifacts/gpu-gradients/kernel-activation-fresh12/run-guarded.py screen
artifacts/gpu-gradients/kernel-lab/venv/bin/python artifacts/gpu-gradients/kernel-activation-fresh12/run-guarded.py broad
artifacts/gpu-gradients/kernel-lab/venv/bin/python artifacts/gpu-gradients/kernel-activation-fresh12/run-guarded.py correctness
artifacts/gpu-gradients/kernel-lab/venv/bin/python artifacts/gpu-gradients/kernel-activation-fresh12/run-guarded.py smallconfirm
artifacts/gpu-gradients/kernel-lab/venv/bin/python artifacts/gpu-gradients/kernel-activation-fresh12/run-guarded.py mazeconfirm
artifacts/gpu-gradients/kernel-lab/venv/bin/python artifacts/gpu-gradients/kernel-activation-fresh12/run-guarded.py device0confirm
taskset -c 24-27 artifacts/gpu-gradients/kernel-lab/venv/bin/python artifacts/gpu-gradients/kernel-activation-fresh12/assembly12.py
python3 artifacts/gpu-gradients/kernel-activation-fresh12/summarize.py broad
python3 artifacts/gpu-gradients/kernel-activation-fresh12/first-selected.py
artifacts/gpu-gradients/kernel-lab/venv/bin/python artifacts/gpu-gradients/kernel-activation-fresh12/paired-summary.py
python3 artifacts/gpu-gradients/kernel-activation-fresh12/first-selected.py device0confirm
```

Raw phase outputs, affinity/load observations, clocks, source hashes, build logs, driver assembly and native exactness results are retained in this ignored artifact directory. No Windows/macOS/AMD/Intel GPU verification or integrated whole-game candidate measurements were performed by this subtask.
