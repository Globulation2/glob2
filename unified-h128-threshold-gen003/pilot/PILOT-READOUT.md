# Gen003 mature threshold pilot

The lower thresholds activated substantial model-driven refresh work. On these three mature held-out worlds, threshold1 improved the measured direction-cost rate while using modestly more aggregate CPU; threshold4 had a weaker quality improvement and slightly more aggregate CPU. This is a small descriptive pilot with changing background load, not a production selection.

All30 executed runs finished their requested horizons, with no failures/caps. The predeclared3FFA warmup ended at6370 and its9 paired cells were skipped without replacement. The other three shared warm snapshots were at16384, with4/3/5 active teams and189/163/197 units. Two counterbalanced probe-OFF8192-step repeats per threshold/map measured compute; separate1024-step probe-ON windows measured quality and detailed work. Three short OFF runs matched their ON counterparts exactly across world/snapshot/replay checksum vectors and final saves. All long within-arm repeats also produced identical final checksums.231 raw artifact hashes verified.

| Threshold units | Simulation CPU ms/tick | All-process CPU ms/tick | Model admissions in diagnostics | Extra direction cost/query | Movement stalls | Movement FN/FP |
|---|---:|---:|---:|---:|---:|---:|
|1|1.383|1.471|2006|0.303|31|39/51|
|4|1.415|1.506|1111|0.377|31|30/72|
|128|1.333|1.425|1|0.396|46|26/116|

Each timing arm advanced49152 total simulation ticks over6 runs. All-process CPU includes setup/save-independent exit costs; simulation CPU uses the engine's measured run CPU divided by actual end-start, never the absolute loaded tick. Direction observations differ by policy:2577/2642/2735 at1/4/128. Model-admission counts refer only to the separate3072 diagnostic ticks per arm and are not extrapolated to timing windows.

CPU effects differ by map. Mean simulation-CPU ratios versus128 for thresholds1/4 were1.189/1.242 on4FFA,0.902/0.939 on2v2, and1.037/1.030 on8FFA. Individual repeat timing ranges overlap and vary with order. Raw CPU, wall time, throughput, aggregate periodic/lazy jobs, per-run load/frequency/SMT context, and all command/environment/provenance receipts are retained. CPU2 was a single nice19 worker, not an isolated physical core; concurrent fleet/background work changed. These repeats do not establish confidence intervals or general CPU effects.

The direction-cost improvement is mainly Materials/Clear. Materials cost/query was564/855=0.660 at1,739/902=0.819 at4, and761/943=0.807 at128. Clear was216/563=0.384,242/537=0.451, and318/520=0.612. Guard had zero measured extra cost for all three arms; Forbidden had only15/16/14 observations, and Markets had none. Guard/Forbidden do not establish broad quality superiority. Clear false-unreachable observations increased at lower thresholds, while false-reachable observations fell; stall and raw reachability counters are separate outcomes and not equivalent to direction cost.

Planning observations were38522/39074/39561 at1/4/128, with false-unreachable474/461/512 and false-reachable257/379/364. Those counts have their own planning denominators; they must not be mixed with movement-query rates. Per-family raw totals and denominators are in PILOT-SUMMARY.json.

Policies diverge from the same snapshot, so later worlds and observation mixes differ. All three thresholds use the same gen003 model/H128/minimum8/no forced maximum age. Thresholds1/4 were selected on DEV data before these prospective held-out observations. No coefficients, production threshold, training dataset, or active collector were changed. The paired-horizon follow-up is a separate observer-only collection using baseline128000 and the same mature snapshots; it does not compare the old fitted H32 model.

Evidence: PROSPECTIVE-PLAN.json, PILOT-SUMMARY.json, PILOT-SEALED.json, pilot-remote/OBSERVER-PARITY.json and per-run raw artifacts. Source4a2cf3d, model9981398a, qualified threshold-only ELFaffd7d2e. Plan SHA0f96fffb459c07da81676bf6047ea6ae4b69ea8dbeaa8947670baa620c3a9f25.
