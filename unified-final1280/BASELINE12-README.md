# Adaptive gradient final evidence

The selected model uses twelve generic inputs: seven capture-age/demand/target-turnover inputs, three since-capture movement counters, and two event fractions. It serves all enrolled periodic gradient families under the same deterministic policy: Adaptive16, capture-anchored H128 labels, minimum admission interval8, publication delay8, no maximum-age refresh cap. Spatial experimental inference/telemetry was removed. The five additional inputs were retained after the seven-input prospective candidate failed a reachability check; this is not proof of any individual counter's causal contribution.

Selected compact model SHA256: `16837e1876f21a9f33090058319c596694a44dc2cc61f2e9508f26fc87916df7`.
Generated header SHA256: `d38639258d8cc9f729882633cd6b12ed6dddd33209478df8a348636c541dd21f`.

## Qualification status and source boundaries

**Final production native qualification: PENDING.** The owner must add the final Mac/Linux release, matching-platform checksum/save/probe/thread, golden, replay/version and compatibility receipts before publishing this evidence as final. No final qualification is inferred from the research prototype tests below.

The measured selected12 prototype is based on cleanup source `e01fe4d1aa5780b039c9db3b24b6ab49cbbc4ee6`, with the selected1280 coefficients/header installed as a research-only delta. It retains save/replay version181 and SIM91. Prototype Mac ELF: `1a6634b840db6be73f25122d4443af853d9da8e34f1fcc0573298cd76b994750`. The final production build advances to version182/SIM92 after selection, with the same model/header. Its native qualification must establish that boundary separately.

Accepted gen005 comparison: native `4cf3933fee8cfc63d1ddab802589dac6b0ffeb2e`, ordinary Mac ELF `3781b88988069b18b7588a50608c652aec2459a4ef81a30ae4c80777843a9236`. Master comparison: `84edb385cd740c3979c06ef901ab4871ea8115c2`, ordinary ELF `da5eeacf577e3b29dbfe570dfbc0dd71a529fb8af765995ab8ab3f378de79713`.

## Training and offline results

The terminal dataset accounts for1,280 preselected game slots:500 normal completions and780 censored trajectories. Only complete, newline-terminated H128 episodes are recovered from censored games; incomplete future horizons are excluded. The three explicit user-stopped trajectories retain their original user-stop receipts rather than being called crashes or completed games. Original cohort, binary, model, threshold, map and episode provenance is preserved. Grouped heldout map assignments remain frozen across rounds; format/role/cohort/map/family/collection/episode weighting prevents later candidate cohorts from drowning exploration.

There are293,779 complete rows,287,045 eligible rows, and67,018 heldout rows from315 maps. On identical heldout rows/weights at threshold16, selected1280 generic12 decision regret is **2.87889805**, versus **2.89645965** for the accepted1024 model (-0.61%). MAE is7.81830 versus7.74421. This is a small offline improvement with mixed subgroup changes, not universal improvement.

The seven-input ablation had slightly better heldout regret2.84193155 and essentially flat grouped CV versus12. It was nevertheless rejected prospectively:135 late queries on one guard field used a field1,099–1,350 ticks after its last publication, and the cached selector stayed while a fresh selector would travel. Those events receive zero direction extra cost under the current objective because both selectors must move for that cost to be comparable. Raw reachability and missed-move outcomes therefore remain separate validation measures. See `rejected-seven/` and the decoded benchmark triage. The final12 prototype resolved that particular guard-field starvation.

`generic-research.json`, `PAIRED-1280-VS1024-A16.json`, `BASELINE7-VS12-ABLATION.json`, `terminal-inputs.json`, `map-splits.json`, and the five `round*-episodes-provenance.tar.gz` archives contain the full fit, grouped split and exact training inputs. `TRAINING-ARCHIVES.json` records hashes. Historical spatial analysis is retained only as evidence in `spatial-research.json`; its CV-selected variant did not beat generic12 on its matched heldout subset.

## Prospective CPU and quality

The selected12 panel uses two previously examined175 saved states, four interleaved repeats per ordinary-release CPU arm,256 settling ticks and8,192 measured ticks. CPU runs disable probes, work telemetry and checksum tracing. Exact saved states, commands, environments, dependency/asset hashes, result JSON and OS user/system CPU receipts are in the benchmark CAS archive.

| Arm | Simulation CPU ms/measured tick | Process CPU seconds | Wall seconds |
|---|---:|---:|---:|
| Master84ed |0.150035|12.321|12.859|
| Accepted gen005 |0.117819|10.190|10.784|
| Selected1280 generic12 |0.125927|10.706|11.185|

Selected12 saves **16.07% CPU versus master** and costs **6.88% more than accepted gen005** on this panel. Gen005 paired-repeat differences span+1.32% to+11.66%. Simulation CPU comes from `benchmark_run_cpu_ns / benchmark_measured_ticks` and excludes settling; process CPU and wall measurements include loading and settling. These are short, local, two-state measurements with visible spread, not isolated-core population confidence intervals or full-match outcome estimates.

Separate observer runs improve direction extra cost/query on both states, while8FFA stalls increase. Query/game trajectories differ, so the following describes outcomes rather than matched individual decisions.

| State | Model | Queries | Extra cost/query | Stall rate | False unreachable | Missed move |
|---|---|---:|---:|---:|---:|---:|
|4FFA|Accepted gen005|1,118|0.7862|0.626%|0.089%|0.805%|
|4FFA|Selected12|1,219|0.3782|0.492%|0.082%|0.574%|
|8FFA|Accepted gen005|1,151|1.2181|2.606%|0.261%|1.651%|
|8FFA|Selected12|1,783|0.8817|4.150%|0.337%|1.851%|

Of74 selected12 stalls on8FFA,20 would move under the fresh selector and54 also fail under the fresh selector; accepted gen005 has30 stalls,10 correctable. Stalls that also fail fresh are not evidence that an immediate gradient refresh would fix them. No claim of adaptive dominance or additive/model-only CPU attribution is made. Per-family movement/planning/work data remains accessible.

## Reproducing and inspecting

`BENCHMARK-CAS.json` indexes599 logical files backed by279 unique SHA256 objects in `benchmark-cas.tar.gz`; the archive excludes ELF binaries, object files, dependency trees and source/build trees. Run `python3 unpack-benchmark-cas.py <destination>` to verify and reconstruct commands, measurements, sampled quality/work CSVs and shared175 inputs. Original absolute paths in commands document actual execution; they must be mapped to the reconstructed inputs and a separately built matching binary for replay.

Extract the five training archives beside this README, use the recorded pinned Python numerical environment and tested source, then run `python3 reproduce-fit.py` from that checkout. It invokes the permanent twelve-input fitter once, uses the frozen map registry and compares the four exact integer heads against the selected compact model. It never imputes censored tails. The helper refuses a changed selected-feature default. No reproduction fit was run merely to package this evidence.

The selected prototype passed530 actual-model C++/Python UBSan parity vectors and96 focused tooling tests. These do not substitute for the pending final native/version/platform qualification. Rejected7 parity and tooling results are retained without implying its production acceptance.

## Observed gradient work in the same quality windows

Observer-only work counters cover the same ticks[16640,24832) as the CPU measured windows. Work counters were disabled during CPU timings. Admission field-cells count the output size once at admission; they do not measure propagated cells, elapsed cost, or unique visited tiles. Summing lifecycle events would double-count work, so this table uses admissions only.

| State | Model | Materials jobs/cells | Clear jobs/cells | Guard jobs/cells | Forbidden jobs/cells | Lazy building jobs/cells | Total jobs/cells |
|---|---|---:|---:|---:|---:|---:|---:|
| 4ffa | final12 | 973 / 7,970,816 | 702 / 5,750,784 | 88 / 720,896 | 164 / 1,343,488 | 3,430 / 28,098,560 | 5,357 / 43,884,544 |
| 4ffa | gen005_reference | 866 / 7,094,272 | 858 / 7,028,736 | 84 / 688,128 | 146 / 1,196,032 | 2,943 / 24,109,056 | 4,897 / 40,116,224 |
| 8ffa | final12 | 305 / 4,997,120 | 634 / 10,387,456 | 801 / 13,123,584 | 147 / 2,408,448 | 915 / 14,991,360 | 2,802 / 45,907,968 |
| 8ffa | gen005_reference | 288 / 4,718,592 | 525 / 8,601,600 | 665 / 10,895,360 | 134 / 2,195,456 | 862 / 14,123,008 | 2,474 / 40,534,016 |

On4FFA periodic-family admissions fall1.38% (1954→1927), while lazy-building admissions rise16.55% (2943→3430); total admitted field-cells rise9.39%. On8FFA periodic-family admissions rise17.06% (1612→1887), lazy-building admissions rise6.15% (862→915), and total admitted field-cells rise13.26%. Changed movement, game trajectories and lazy-building demand contribute to observed CPU differences. These totals do not isolate the inference cost or causality of an individual input. Raw work/prepared/resume events and reason counts remain in the CAS; `FINAL12-GEN005-WORK.json` preserves the complete per-family counters.
