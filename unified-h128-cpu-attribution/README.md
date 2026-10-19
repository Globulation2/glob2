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

## Separate pathfinding diagnostics

All rates use observed queries during ticks16640–24831, excluding256 settling ticks. Query exposure differs by policy. These ON runs supply no CPU estimates.

| State | Policy | Movement n | Extra direction cost/query | Stalls | False unreachable | False reachable | Planning n | Planning false unreachable | Planning false reachable |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 4ffa | adaptive16 | 1118 | 0.786 | 0.626% | 0.089% | 4.562% | 36296 | 0.964% | 0.490% |
| 4ffa | fixed8 | 1273 | 0.483 | 0.236% | 0.236% | 1.885% | 35105 | 0.048% | 0.088% |
| 4ffa | fixed32 | 1069 | 0.504 | 0.561% | 0.000% | 4.303% | 31408 | 0.096% | 0.513% |
| 4ffa | fixed128 | 1449 | 1.156 | 0.414% | 0.207% | 4.831% | 45283 | 0.115% | 0.411% |
| 8ffa | adaptive16 | 1151 | 1.218 | 2.606% | 0.261% | 3.475% | 2313 | 0.086% | 4.064% |
| 8ffa | fixed8 | 1585 | 0.973 | 3.281% | 0.946% | 0.946% | 1752 | 0.000% | 1.712% |
| 8ffa | fixed32 | 1491 | 0.816 | 2.414% | 0.335% | 2.012% | 1786 | 0.336% | 2.912% |
| 8ffa | fixed128 | 1905 | 0.994 | 2.677% | 0.682% | 2.257% | 2746 | 0.364% | 3.168% |

Adaptive16 saves9.39% CPU versus demanded fixed32 on this panel, while extra direction cost/query and stall rate are worse on both states. Fixed8 costs more and lowers extra direction cost on both states; stall and planning-error comparisons remain mixed. Fixed128 is near CPU parity with Adaptive16 (Adaptive16 +0.77%) and direction quality is mixed. This is a tradeoff, not evidence of adaptive dominance or an assignable learned share of the whole-branch CPU gain.

## Class-gate diagnostics

Separate8192-tick measured windows aligned with CPU runs, after256 settling; counters ON here and OFF in CPU runs. Initial/final checksums match corresponding CPU arms. The original shorter1024-tick diagnostics are retained separately in SUMMARY.json.

| State | Gate | Candidates | Known-zero candidates | Skipped | Admissions | Admissions/tick |
|---|---|---:|---:|---:|---:|---:|
| 4ffa | class_off | 205230 | 11684 | 0 | 8192 | 1.000 |
| 4ffa | class_on | 201795 | 1946 | 1946 | 8192 | 1.000 |
| 8ffa | class_off | 203019 | 68122 | 0 | 8192 | 1.000 |
| 8ffa | class_on | 275361 | 140264 | 140264 | 8192 | 1.000 |

Across the aligned full windows, 4FFA skipped1,946 candidates and8FFA skipped140,264, yet every arm admitted8192 jobs in8192 measured ticks. Freed slots were filled under the old one-job policy. Candidate pruning therefore shortened/reordered the eligible schedule without lowering the job count here. Field cost mix and changed gameplay can still affect CPU; this experiment does not establish zero benefit on other maps/epochs. OFF/ON candidate populations differ after gameplay diverges and must not be treated as identical opportunities. The retained first1024-tick4FFA diagnostic had no zero-class candidates, but that does not describe its full8192-tick window.

Per-family movement/planning denominators, work admissions/publications, lazy prepared/resume work and final per-team8-bin counts are retained in SUMMARY.json. Immutable reference controls quantify research bookkeeping/selector overhead; negative or small differences may reflect local noise/order/load. No additive causal percentage is inferred.

All64 CPU arm/state repeats are deterministic; off/default references match immutable binaries. Eight observer-ON runs match corresponding OFF final checksums. Asset tree and dependency hashes were unchanged before/after. Qualification and commands are retained alongside this report.

## Reproduction evidence

Research patches and their base revisions, ordinary release build inputs/logs, root review, frozen execution plans, switch-off/default checksum/save parity proofs and final receipts are included. These research switches are not shipped production changes; active fleet collectors and production coefficients were untouched. Extract raw-objects.tar.gz and use raw-files.json to locate each exact original file at sha256/<hash>. Files include CPU commands/results, separate probe/work/class diagnostics and focused parity artifacts. Profile preferences and executables are excluded. Common input maps/saves are published in [the master comparison](../unified-h128-generation005-a16/master-comparison/), with hashes bound by PLAN.json. Packaging import preflight failure and its environment-only recovery are preserved. Supplemental full-window class counts address the initial shorter diagnostic window without repeating or selecting CPU outcomes.
