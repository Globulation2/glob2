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
