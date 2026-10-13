# Whole headless engine measurements

Final indexed planner, shared union capture and executor; K=R=1, L=8, margin=1. The dedicated native benchmark replaces only selection in cached mode and fires normal bullets. Shipping gameplay remains original. Every workload runs Numbi/Castor, normal hiring and gradient/growth scheduling. Native full ticks include normal turn checksums and completion/draining of admitted work, exclude setup, renderer, transport/pacing, saves and checksum exports. One untimed three-mode warmup and ten paired seeds per workload/participant cell; 256 ticks each.

**Engineering gate: failed. Do not progress to a live trial on this configuration.**

| Workload | Participants | Original median µs/tick | Cached median µs/tick | Paired median change (95% CI) | Mean tick change | Process CPU change | Workload gate |
|---|---:|---:|---:|---|---:|---:|---|
| empty100 | 1 | 517.1 | 451.5 | -12.6% (-14.4 to -11.0%) | -12.8% | -12.8% | pass |
| empty100 | 2 | 355.5 | 362.6 | +2.0% (-0.8 to +3.5%) | +1.6% | -12.5% | FAIL |
| empty100 | 4 | 246.5 | 189.6 | -23.5% (-27.5 to -19.6%) | -23.4% | -12.9% | pass |
| empty100 | 8 | 270.2 | 203.6 | -25.7% (-28.3 to -20.3%) | -24.9% | -11.6% | pass |
| combat100 | 1 | 795.9 | 851.4 | +7.7% (+6.7 to +7.9%) | +7.3% | +7.3% | FAIL |
| combat100 | 2 | 617.7 | 666.4 | +7.4% (+5.7 to +9.8%) | +7.3% | +9.5% | FAIL |
| combat100 | 4 | 446.3 | 485.4 | +10.3% (+5.0 to +11.5%) | +8.5% | +10.3% | FAIL |
| combat100 | 8 | 451.8 | 468.4 | +4.0% (-0.4 to +7.8%) | +3.5% | +7.7% | pass |
| ordinary4 | 1 | 386.5 | 392.8 | +0.8% (-1.5 to +3.0%) | +0.9% | +0.9% | pass |
| ordinary4 | 2 | 351.5 | 350.3 | -0.1% (-1.4 to +1.1%) | -0.6% | -0.2% | pass |
| ordinary4 | 4 | 170.9 | 170.7 | +0.5% (-3.1 to +4.2%) | +0.6% | +0.1% | pass |
| ordinary4 | 8 | 170.9 | 174.3 | +2.0% (-5.2 to +8.8%) | +1.7% | +0.9% | pass |

Positive change means slower/more CPU. Empty gate requires at least 5% median improvement and paired CI supporting improvement. Combat gate uses mean tick regression ≤5%. Ordinary gate uses mean increase ≤max(3% of original mean,2 µs/tick). Process CPU point estimates stay within +15%; confidence intervals remain in JSON. The final eight-participant point estimates pass these workload gates, but its combat mean CI extends beyond +5%, and earlier valid repetitions failed its combat/control gates. Other participant counts fail combat or empty gates. No robust overall qualification is established.

At four participants, empty-range gains are real in this run, while sustained combat exceeds the 5% regression budget. The current producer refreshes all turrets every admitted tick, even during cooldown. The combat reference performs only 11942 ready/armed searches across 256,000 turret-ticks. Cooldown forecasting already exists as a helper but refresh suppression is not connected. It is a concrete next optimization to investigate, not proof that it alone fixes the regression.

Shadow costs and complete bootstrap results are in summary.json. Cached combat fires 5997 shots versus 5966 original shots across ten seeds; these are different trajectories. The original finds a target in about 50% of eligible combat searches. Empty and ordinary controls fire no shots. No checksum comparisons between different combat firing schemes are asserted.

Correctness: the complete cached trace/orders/final save match across participants 1/2/4/8 on each fixture; shadow trace/orders/final saves match original. Original/shadow save continuation passes all nine cuts (1/2/4/8/16/38/39/40/41). Cached continuation fails cuts 39 and 40, around the first firing opportunity; it passes the other seven. Published/pending cache state and admissions are not serialized: this is a concrete failed live-readiness gate, not an ordinary save-format regression. Traces, final saves and continuation CSV are in correctness-evidence.

Peak reported index accounting: 64,256 bytes; shortlist reservations: 214,400 bytes. These are bounded producer accounting, not incremental process RSS. Host samples preserve RSS/high-water and competing processes. Another benchmark process was active on this host, so quiet-host qualification remains outstanding.

Coverage: Linux x86_64 GCC13 release only. Rendering/network transport overhead, supported-platform comparisons and interference with unmerged hiring experiments are not covered. This base includes normal hiring and shared AI/gradients/growth, without changes to hiring behavior. The invalid first run accidentally used serial execution for every participant label; superseded-serial-* files retain it, and it is excluded entirely. Final setup calls Map::configureCompute and asserts actual participant counts.

Recommendation: retain the indexed planner, stop live progression, implement and test the planned cooldown refresh forecast, then repeat this same matched-seed corpus and quiet-host measurements. Do not widen the four-tick age budget or add owner rescoring/search fallback.

Repeated runs are retained: at eight participants, combat mean regressions were +11.4%, +8.2%, then +3.5%; ordinary-control regressions +5.6%, +8.3%, then +1.7%. The final combat mean 95% CI is +0.9% to +6.3%, and the ordinary-control CI also crosses its budget. These runs must not be cherry-picked into a passing gate. Final binary is archived byte-for-byte with its recorded timing hash. Earlier qualified repetitions use the same timing path; their diagnostic/provenance builds differ, so the final run is reported independently rather than pooling them.

Production verification after adding the benchmark guard: 58 focused release checks passed. The shipping TypeSteps object has no reference to the benchmark selector symbol. The final dedicated benchmark passed two correctness cases (25,336 assertions); cached continuation divergences are explicitly recorded as failed readiness gates. No simulation version or save floor was changed because the actual firing replacement is compiled only into this research program. Full integration with newer master remains unverified.
