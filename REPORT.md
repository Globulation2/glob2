# Terrain CPU investigation: controlled costs and remaining uncertainty

**The evidence does not establish that the terrain refactor is intrinsically faster overall.** Controlled component measurements show both substantial improvements and substantial regressions, depending on the operation and terrain pattern. There is no defensible arithmetic correction that turns the earlier whole-game observations into a single implementation-overhead percentage.

This investigation compares the original engine at `934c3c588bea51ec7421152e15f5168b2aaf5502` with the completed terrain, ecology and gradient implementation at `37d609766374e7efc1679d643c7d18cde84364ab`. The final endpoint precedes unrelated Markets V2 and audio work. No production code was changed.

## Measurements that completed

All changes below are **process CPU changes**, not FPS or whole-game savings. Lower is better. Each case uses eleven alternating old/final pairs. Component runs passed their coarse host observations, which do not establish an isolated machine or control allocator/cache layout.

| Fixed-input operation | Observed result | Interpretation |
| --- | --- | --- |
| Classic land-fertility kernel, dense fixtures, 128²–512² | 46–61% less warm CPU | Identical field outputs; a genuine improvement for these inputs |
| Classic land-fertility kernel, striped 256² / 512² | 55% /38% more warm CPU | The generalized weighted calculation is more expensive for these inputs |
| Restricted mixed-resource decisions, 128² | 23–38% less CPU | Same fixed tile visits; excludes successful mutations and cache construction |
| Restricted mixed-resource decisions, 512² | 13% less to 39% more CPU | Four of six case medians regress; results depend on pattern and cache reuse |

The growth pairs use seeds 1427–1437, so their intervals combine stochastic decision-work variation with runtime noise. The growth range is not an estimate of the complete live growth loop. Its two methods deliberately consume different random draws and produce different decisions even with identical tile inputs. Nor is the128²/ 512² contrast a controlled map-size experiment:128² repeats 16 passes, while 512² makes one pass. Separate allocations, no decision-loop warmup, and some bimodal samples limit causal attribution of that contrast.

[Fertility results, paired confidence intervals and memory tables](FERTILITY.md) · [Growth results and fixture limitations](GROWTH.md)

### Fertility comparison

The old production entry takes binary water/sand planes; the new entry takes prebuilt Q8 contribution/inhibition planes. Both compute exactly the same classic land field for these fixtures. All 60 correctness cases, including thin/wrapped shapes, algorithm choices and workspace reuse, match an independent direct 31×31 oracle byte-for-byte.

Input-plane preparation is separately recorded and excluded from both kernel timers. Actual Map property gathering, aquatic fields, habitat/local-growth work, cache acquisition and request frequency are outside this comparison. Constructor plus first rebuild is reported separately from warm retained-workspace calls. New temporary scratch allocation remains inside timing. Retained-memory tables exclude temporary peak allocation and allocator overhead.

The striped regression has a plausible source explanation: the old binary algorithm skips zero-water probes, while the generalized algorithm performs weighted arithmetic at those positions. This is source analysis, not hardware-counter proof. Earlier ecology optimization results used an already-refactored weighted implementation as the baseline; they cannot substitute for this original-to-final comparison.

A separate binary-adapter control was also built. Production cache rebuilding bypasses that adapter, and its timing failed the host-load protocol; its results are excluded from the table.

### Growth comparison

The benchmark extracts the actual archived per-tile production decision bodies, resource metadata and RNG helpers. Both receive identical tile coordinates, terrain, resource types and amounts. The saved growth-permission override is false at the final gate, after probability/amount/direction decisions; mutation helpers abort if unexpectedly called. Separate counter builds and disassembly confirm that the timed binaries retain real RNG and decision work.

All 240 smoke runs passed input/state checks. The timing matrix covers 48 non-control cases and one stone control: 1,078 processes. Counters are disabled in timed binaries; production assertions remain enabled. These are restricted decision costs, excluding random coordinate selection, full Map cache acquisition, successful mutation, bookkeeping and downstream AI. Final field preparation is measured separately, but even that timer excludes complete Map gathering/acquisition. It must not be treated as full cache payback or double-counted with the fertility experiment.

## Why population normalization does not fix whole-game numbers

The refactor changes the work generated by a game, not merely the cost of each operation. Separate diagnostic runs with the same initial maps and seed 12345 produced these exact call counts over 16,384 ticks:

| Scenario | Operation | Original | Final | Change |
| --- | --- | ---: | ---: | ---: |
| Allotments | Resource-path calls | 6,652 | 5,526 | −16.9% |
| Last Treeline | Area-path calls | 6,696 | 4,126 | −38.4% |
| Last Treeline | Point-path calls | 1,427 | 1,790 | +25.4% |
| River | Building-path calls | 96,567 | 63,211 | −34.5% |

Calls are not equal-sized jobs: search distance, obstacles and goal density also change. Dividing total CPU by final units, buildings or gradient-job counts cannot account for this. Diagnostic final states and counters matched corresponding non-timeline runs; diagnostic elapsed timings are excluded from performance claims.

The earlier approximately 4.4% reduction from summing three initial-refactor normal-growth CPU medians was an observation about divergent games. It was neither intrinsic terrain overhead nor a measurement of this completed implementation. Subsequent optimization percentages have different baselines and cannot simply be added to it.

## Whole-game and gradient timing status

The new whole-game plan specified three classic maps × three seeds × eleven alternating pairs: 99 pairs. Maxima versus Nicowar, normal growth, one compute thread and zero gradient workers; 16,384 ticks with 2,048 warmup ticks excluded. Process CPU excludes setup/save. Timeline and per-tick checksum emission are off. **Normal built-in production scope collection remains on**: Headless clears inherited `GLOB2_PERF_DISABLE`, a behavior verified in both frozen endpoints. These are default-production measurements, not collector-free binaries.

Only **5 of99 pairs** were accepted before repeated unrelated builds/tests prevented completion under the declared host bounds. Two additional pair attempts were rejected, with many preflight waits. The runner was stopped while idle. **No new whole-game aggregate or confidence interval is reported.** Raw accepted/rejected attempts and the complete runner remain available for repeating the experiment on a quiet host.

The complete gradient matrix finished **217,728 full-vector oracle checks** across 756 logical cases, three input-reader representations, two allocation layouts and two link orders. It covers all seven swimming profiles, square/thin/rectangular shapes, goal distributions, obstacles, deferred seeds and propagation caps. Shared input/output addresses and separate allocations expose memory-layout sensitivity; private queue addresses remain unmatched. Production assertion settings were preserved.

Gradient timing failed its coarse host bound: sibling CPU activity reached 67.3% in an assessed window. Its numerical correctness checks remain valid, but **its timings are exploratory and excluded from the headline findings**. [Gradient methodology and excluded timing analysis](GRADIENT.md)

## Controls and reproducibility

- Linux x86-64, GCC 15.2, AMD Ryzen Threadripper 2950X. Frozen executable hashes and dynamic-library hashes are recorded; installed library dictionaries match across endpoints. A review of 61 production compile records covering 55 source files found no meaningful flag difference. Neither endpoint removes assertions.
- All 26 intervening first-parent commits were inspected. No unrelated native `--run-game` simulation hot-path change was found; Studio polling is browser-only and replay-import telemetry changes are outside this path. This does not eliminate binary-layout effects.
- Frozen runtime data was verified against the exact revisions: 7,336 original and 7,398 final files, zero mismatches or extras. The final game runner explicitly sets `GLOB2_ASSET_DIR` because generated asset roots otherwise precede the working directory. Earlier nine bounded pairs with unpinned roots are retained as superseded; only localization/graphics-index drift was found, but those pairs are excluded from the final protocol.
- Whole-game children use CPU 15. Their monitor excludes CPU 15 and sibling 31; acceptance requires load1≤10, at most two recognized compiler/linker processes, and at most 20% observed external busy time in assessed target/sibling windows. The target subtracts child CPU time; five-second rolling windows limit dilution. Decisions depend only on host observations, never whether the new engine wins. Component monitoring is weaker: sibling/compiler/load observations only, without attributing competing target-core work.
- Earlier uncontrolled pilots, superseded protocols, the rejected adapter run and rejected gradient timing are preserved separately. No favorable subset is substituted for a failed or incomplete matrix.

The archive contains exact commands, raw samples and result hashes, monitors, benchmark sources, correctness evidence, compiler/input manifests, and detailed limitations. Large binaries and runtime asset trees are identified by hashes rather than included.

## What would settle the remaining question

Completing the 99-pair matrix on a quiet machine would establish an observed endpoint CPU change for those nine game scenarios. It would still include changed gameplay.

A single same-workload whole-engine figure needs a more complete common-workload replay or a legacy-behavior build validated against old per-tick states, orders and RNG consumption. Restoring only old growth or disabling growth does not make AI decisions, routes and predicates equivalent. That additional engine work was not implemented here.

The supported conclusion is therefore narrower: **the new architecture has measured component wins and costs; a net intrinsic engine speedup is not established.**

[Raw evidence archive](terrain-net-performance-evidence.tar.gz), SHA-256 `bbbe024bb05fb45adb3e5f840e6f1371f8c4e3040a01b18f22a6fcdd97fd0630`. Files remain outside product history. The archive includes raw samples, rejected attempts, monitors, source-extracted benchmark code, compiler/input hashes and fixed map inputs; large runtime asset trees and executable binaries are omitted. Rebuild source from the recorded revisions and reproduce the recorded compiler/dependency setup.
