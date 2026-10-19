# Gen005 vs gen004 warm-state comparison

Three previously examined warm states, not a new test-set estimate. Four interleaved OFF repeats per state, then one independent ON diagnostic per arm. Positive CPU deltas mean gen005 used more CPU.

|State|Format|Gen004 CPU ms/tick|Gen005 CPU ms/tick|Change|Process CPU change|Wall TPS change|
|---|---|---:|---:|---:|---:|---:|
|66294c58700ef1b4|4ffa|0.141660|0.147299|+3.98%|+3.30%|-9.23%|
|1d043310cbecdceb|8ffa|0.221313|0.211656|-4.36%|-3.66%|+5.21%|
|0083541d8ee4d18b|2v2|0.204534|0.200643|-1.90%|-1.63%|+0.30%|

Pooled clean OFF (98304 ticks per arm): simulation CPU 18.596071→18.336899s; process CPU 21.895735→21.648468s; wall 23.229343→23.377694s; pooled throughput 4231.89→4205.03 ticks/s (sum ticks divided by sum wall).

All repeat ranges, total CPU, wall time, TPS, per-family work and distinct movement/planning outcomes are retained in SUMMARY.json. ON timings are excluded from CPU comparisons.

- Already examined three-state warm panel; not fresh generalization
- Single Mac host; no processor pinning; OS scheduler may migrate between performance/efficiency cores
- Load, process activity and available thermal diagnostics recorded at cell boundaries; no continuous thermal/host-load control and no cold/uncontended claim
- ON timings excluded from CPU claims
- Different policy trajectories may change workload
- Research only; no model promotion/admission changes
- Retired Dev observations excluded completely
- OFF CPU measures normal headless benchmark simulation with probe/work/compute/checksum tracing disabled. ON diagnostics never enter CPU claims.
- Retired Dev and Mac traced observations excluded completely.

show all repeats and within-map range; three examined states cannot support population confidence intervals.

## Separate ON quality and work diagnostics

|State|Cost per movement observation, gen004→gen005|Stalls per movement observation, gen004→gen005|Movement observations, gen004→gen005|
|---|---:|---:|---:|
|4ffa|0.34167→0.34255|0.500%→0.189%|1399→1591|
|8ffa|1.72401→1.30877|6.809%→5.492%|1645→1493|
|2v2|0.19952→0.30175|0.443%→1.538%|2937→3705|

Probe observations follow different policy trajectories and query exposures. These rates describe the observed three-state panel; they are not identical-decision counterfactual labels or a fresh generalization estimate. Planning outcomes have their own query denominator in SUMMARY.json.

Shared jobs: 7217→7232. Lazy building jobs: 9677→9453; search resumes: 1905→1709; summed lazy settled-cost advancement: 5524248→5224265. Per-family work is retained in SUMMARY.json.
