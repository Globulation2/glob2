# Fixed-input growth decision results

**This is not an across-the-board improvement.** The 12 mixed-resource cases range from **-38.0% to +39.2% CPU**, comparing median paired final/legacy ratios. All six 128² mixed cases improve; four of six 512² mixed cases regress. The largest mixed regressions occur on river and dense layouts.

Forty-eight representative cases × eleven alternating baseline/final pairs, plus one stone control: 1,078 processes. Every process executes 262,144 fixed tile visits; logical inputs and occupied-visit counts match within pairs. The endpoint source is legacy934c3c588 versus final37d609766. Binaries have counters disabled and production assertion mode. `manifest.json`, `samples.json`, `summary.json`, `analysis.json` and `../assembly/review.json` retain provenance and raw evidence.

## Descriptive ratios, not game-weighted savings

The equal-case geometric mean is **-15.9% CPU** over the48 non-control cases and **-9.5%** over the12 mixed cases. These are descriptive summaries of this chosen synthetic matrix; neither is a whole-game weighting or net terrain-refactor saving. The pure-stone control is kept separate (-50.9%); stone cannot grow, and the new path avoids useless old probability work.

| Width | Mixed | Wood | Wheat | Algae | All24 cases |
| --- | ---: | ---: | ---: | ---: | ---: |
| 128² | -29.7% | -25.2% | -29.6% | -32.2% | -29.2% |
| 512² | +16.4% | -7.4% | -4.6% | -3.3% | -0.1% |

Across all sizes/modes the layout summaries are sparse -23.4%, river -12.4%, dense -11.5%. Natural amount1/5 summaries are -16.3% / -15.6%. Amount does not explain the mixed size reversal.

## Every mixed-resource case

| Size | Layout | Amount | Median paired CPU change | Paired bootstrap95% interval |
| --- | --- | ---: | ---: | --- |
| 128² | sparse | 1 | -38.0% | -38.7% to -31.7% |
| 128² | sparse | 5 | -32.4% | -32.5% to -31.3% |
| 128² | river | 1 | -30.1% | -30.4% to -29.9% |
| 128² | river | 5 | -30.1% | -30.5% to -26.7% |
| 128² | dense | 1 | -23.3% | -25.9% to -20.2% |
| 128² | dense | 5 | -22.9% | -25.7% to -20.1% |
| 512² | sparse | 1 | -7.5% | -10.8% to +7.1% |
| 512² | sparse | 5 | -13.1% | -22.4% to -2.8% |
| 512² | river | 1 | +26.9% | +23.4% to +29.0% |
| 512² | river | 5 | +29.4% | -0.5% to +31.0% |
| 512² | dense | 1 | +35.5% | +32.3% to +36.9% |
| 512² | dense | 5 | +39.2% | +35.1% to +41.1% |

Of48 non-control cases,30 intervals lie below parity, six above parity, and12 overlap parity. The512² mixed amount1 river and both dense cases have intervals above parity; the river amount5 interval overlaps parity despite its positive median.

Intervals resample eleven paired ratios. Each pair uses a different seed (1427–1437), so the interval combines stochastic decision-work variation with runtime noise; it is not an estimate from eleven repeats of one exact RNG trace. They are unadjusted per-case intervals. The runner’s ratio of marginal medians is a different statistic and should not replace the paired median: the first128² sparse mixed case gives −50.6% using marginal medians versus −38.0% paired.

## Retained work and limits

The real extracted production visit body, RNG, cache rate lookup, amount roll and expansion-direction decisions remain. Timed disassembly retains legacy/final RNG calls and dynamic growth-gate byte loads. Instrumented/timed smoke agrees on final RNG state within each version; it is not optimized to an empty loop. Every saved growth-permission bit is false only at the final gate, preventing mutations. This intentionally excludes successful resource mutation and telemetry, full Map cache locking/acquisition, donor gathering, random row/column visit selection and its RNG, and resulting changes to the world. Actual old/new opportunity counts and RNG sequences differ.

Field construction is outside the reported decision CPU. The separately measured final field-preparation medians are 2.74ms at128² and 52.00ms at512² across non-control cases. This timer covers actual field kernels plus local/habitat preparation, but not complete Map cache construction/acquisition. Conditional break-even values in summary.json divide this partial preparation by marginal-median restricted decision savings; they are neither full-cache payback nor a game-tick estimate. Regressing cases have no positive decision saving to amortize construction.

## Why size/layout conclusions remain bounded

- Adaptive batching means128² repeats the same full permutation16times, whereas512² makes one pass. Both count262,144 visits, but their cache reuse differs; this is not a controlled test of map size alone.
- The fixed permutation has stride541 cells. Both sides read36-byte tiles; the final path additionally reads resource-dependent field/habitat/local-growth arrays. Those retained cache planes occupy12Nbytes (192KiB at128²;3MiB at512²), versus tiles alone576KiB/9MiB, plus the common coordinate list128KiB/2MiB. Different per-resource array reads and cache locality are plausible contributors to the mixed512 reversal; this is a source-based hypothesis, not established causal profiling.
- Each process allocates separately; matching addresses are not enforced. ASLR/allocator layout, retained field-preparation buffers and extra final arrays may matter. The timing begins without a separate decision-loop warmup. The construction phase itself leaves different cache/thermal histories.
- Raw samples show substantial variation and occasional bimodality: dense512 amount1 baseline stays near24–25ms while final is21–22ms in two pairs and33–35ms in the others. Sparse128 first baseline is14.1ms versus roughly6–8ms later. Do not delete these samples post hoc; pair medians reduce, but do not eliminate, this concern.
- Coarse host bounds were satisfied (`../../growth-observation/observation.json`), but they do not establish isolated cores, stable frequency or absence of shared-cache/bandwidth contention. Short process CPU timings span roughly0.77–39.05ms. Wall/CPU proximity does not rule out on-CPU cache stalls or frequency shifts.

These observations support reporting scenario-dependent decision costs and motivate future controlled reuse/layout experiments. They do not prove that all live growth is faster, or explain a fixed percentage of whole-game CPU. No additional timing was run for this analysis.
