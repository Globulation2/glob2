# CPU attribution

Two preselected surviving master175 warm states; 256 settling +8192 measured ticks per run, four balanced serial repeats, Mac release, all telemetry off. Same master assets and native dependencies. Engine run CPU excludes setup/settling; process CPU and wall include them. Pooled TPS is summed advanced ticks / summed wall.

| Arm | CPU ms/measured tick | Process CPU s | Wall s | Pooled ticks/s |
|---|---:|---:|---:|---:|
| master_reference | 0.148148 | 12.216 | 12.701 | 5321.1 |
| class_off | 0.146302 | 12.085 | 12.678 | 5330.7 |
| class_on | 0.146727 | 12.117 | 12.503 | 5405.3 |
| gen005_reference | 0.114982 | 10.003 | 10.619 | 6364.5 |
| adaptive16 | 0.114866 | 10.070 | 10.680 | 6328.2 |
| fixed8 | 0.160278 | 13.055 | 13.584 | 4975.1 |
| fixed32 | 0.126771 | 10.799 | 11.369 | 5944.7 |
| fixed128 | 0.113991 | 9.932 | 10.339 | 6537.0 |

Contrasts use ratios of pooled equal-tick means; negative CPU change saves compute.

- class_on / class_off: CPU +0.29%; paired repeat range -0.18% to +1.17%.
- class_off / master_reference: CPU -1.25%; paired repeat range -2.08% to -0.42%.
- adaptive16 / gen005_reference: CPU -0.10%; paired repeat range -0.52% to +0.19%.
- gen005_reference / master_reference: CPU -22.39%; paired repeat range -22.56% to -22.25%.
- adaptive16 / fixed8: CPU -28.33%; paired repeat range -29.62% to -27.28%.
- adaptive16 / fixed32: CPU -9.39%; paired repeat range -10.43% to -8.46%.
- adaptive16 / fixed128: CPU +0.77%; paired repeat range -0.89% to +1.55%.

Already examined two-state panel, not fresh generalization or whole-game outcomes. Third planned state ended before common warm checkpoint and was excluded before attribution outcomes.
Four repeats characterize local spread, not independent gameplay samples or population confidence intervals. Mac was checked for competing builds/games and sampled load/thermal/power; it is not an isolated physical core or continuously controlled thermal/load environment.
Class gate uses unchanged master round-robin one-job admission; freed scans may fill the same quota. Same-binary off includes class bookkeeping; immutable reference quantifies its overhead.
Modern fixed timers retain modern demand, cold/reactive admission, history initialization, minimum age and batching. Their differences from adaptive identify model versus simple demanded timers, not class elimination alone. Whole-branch minus class differences are not additive/model-only causal attribution.
