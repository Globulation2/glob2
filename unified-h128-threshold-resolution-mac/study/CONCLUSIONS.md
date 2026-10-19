# Mac threshold comparison

Status: COMPLETE; 144/144 executed runs.

Same gen003 coefficients, H128; mature tick16384 saves. CPU is probe OFF; quality is separately sampled in probe ON runs.
Quality source: extended8192-steps-two1024-tick-sample-windows.

| Policy | CPU ms/tick (equal maps) | CPU timing runs | Direction extra/query (equal maps) | Direction extra/query (pooled) | Stall/query (pooled) |
|---|---:|---:|---:|---:|---:|
| A1 | 0.2209 | 12 | 0.5307 | 0.4143 | 0.0216 |
| A4 | 0.2048 | 12 | 0.4055 | 0.3611 | 0.0106 |
| A16 | 0.1918 | 12 | 0.6327 | 0.4804 | 0.0172 |
| A32 | 0.1906 | 12 | 0.9025 | 0.8075 | 0.0316 |
| A128 | 0.1893 | 12 | 0.7726 | 0.6461 | 0.0210 |
| recovery-only | 0.1869 | 12 | 1.1031 | 0.9581 | 0.0166 |
| fixed32 | 0.2054 | 12 | 0.5548 | 0.5844 | 0.0240 |
| fixed128 | 0.1919 | 12 | 0.8204 | 0.5748 | 0.0180 |

Do not compare partial aggregates until every arm covers the same maps and repeats.

These same three mature worlds were already examined in prior pilot. This is a controlled followup, not new independent validation. Three formats/generators, small/moderate maps, no all-map generalization. Prior unusable3FFA stays excluded explicitly with no replacement.
Actual simulation CPU/tick and sampled direction-extra/query versus fixed32, fixed128, A128 and recovery-only; per-map results plus equal-map CPU aggregate. Separate movement/planning/stall/reachability outcomes. Diagnostics differ from timing horizon; never extrapolate their job counts.
Timing repeats measure within-world timing variation, not independent games. Observed repeat ranges are not confidence intervals.
Diagnostic worlds and query mixes diverge by policy. Show pooled and equal-map quality; keep planning and stalls separate.
The observer samples1024ticks per4096. Initial2048-step runs repeated only the original window; final extended8192-step diagnostics sample two1024-tick windows. Quality is not observed on every timing tick.

## What this establishes

The requested128-unit cutoff is too conservative for frequent discretionary action by this fitted model. That is a policy choice on predicted benefit, not a measured CPU price, and not a milli-unit conversion error. Offline122901-row analysis shows only23heldout rows selected at128(0.0454%weighted). Its extreme score tail is not reliably calibrated: a few correlated Clear examples on two maps drive observed negative tail benefit; a few Materials examples drive the favorable tail. This cannot be repaired confidently by multiplying all scores by a constant. Generic12 sign/magnitude heads fit their own logistic/Ridge losses; the fixed128 regret metric does not directly train those current coefficients, though it did influence spatial/model-class research selection.

This controlled followup leaves source model/minimum8/publication8/no maximum age unchanged. A4 and A16 are research shortlist choices, not automatically promoted production defaults. Four timing repeats on each of three worlds are repeated compute measurements, not12independent games per policy.

- A4 versus fixed32: essentially equal measured CPU(−0.30%, too small to claim a saving),26.9%lower equal-map direction error. A4 is better on polder/islands and worse on fjord for this metric. Its pooled stall rate is lower; planning outcomes remain separate.
- A16 versus fixed128: essentially equal measured CPU(−0.056%),22.9%lower equal-map direction error. A16 is better on polder/fjord and slightly better on islands in this sample.
- A16 versus A128: about1.32%more measured CPU and18.1%lower equal-map direction error. That small CPU gap should not be treated as a precise general cost estimate.
- A1 is worse than A4 in both aggregate CPU and direction error. Closed-loop worlds/query populations diverge, so lowering the threshold need not improve measured quality monotonically.
- A128 admits only2model refreshes in the matched8192-tick diagnostic runs across the three maps. Recovery-only admits0model refreshes. Both still execute substantial cold/reactive/lazy work; neither is a zero-gradient engine.

The old32/H32 policy used a different model, fields/scope and a demanded-age cap. Scaling its usable horizon24→120 suggests160only under constant benefit rate. The paired same-state sample has no short-horizon examples near32, so it supplies no empirical conversion and cannot justify160as equivalent scheduling behavior.

Next useful decision: validate A4 and A16 on new varied map groups, keeping the current generic model fixed first. A4 targets more quality at approximately fixed32 compute; A16 targets a leaner budget near fixed128. Keep both fixed baselines and separate stalls/planning outcomes. Cumulative training/refits continue remotely; this experiment is not ingested into training and does not silently change active collector policies.
