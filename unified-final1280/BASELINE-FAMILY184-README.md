Historical report preserved before the final zone gate and filtered refit. Its selected/pending statements refer to that earlier state.

# Final adaptive gradient evidence

The selected architecture uses separate material, guard, clearing and forbidden-escape models with twelve shared covariates. A combined model trained on all observed families supplies the fallback for unmeasured Markets and unknown future family IDs. Family IDs choose a model; no one-hot inputs are deployed. Markets remain unmeasured, not zero-benefit or qualified by fallback.

Deployment is Adaptive16 with capture-relative H128 labels, minimum admission interval8, publication delay8 and a demanded serving-capture age1024 safety refresh. Admission is subject to capacity and normal publication delay; idle fields remain idle. Historical uncapped cohorts and model metadata remain unchanged.

**Final family-model source and ordinary release qualification are still being assembled.** Prototype comparisons below do not substitute for the final183/SIM93 Mac/Linux qualification, save/thread/observer checks and independent source review. No completion is claimed here yet.

## Training and selected-model comparison

The frozen final dataset accounts for1280 planned slots,500 normal completions and780 explicitly censored trajectories, including3 user stops. It supplies293779 complete episodes,287045 eligible rows and67018 held-out rows across315 maps. No incomplete future horizon is imputed. Weights and persistent whole-map splits are identical across comparisons. Exact archives and hashes are listed in TRAINING-ARCHIVES.json.

Shared12 held-out direction regret is2.878898; family experts2.125068,26.18% lower. Family IDs already existed as diagnostic metadata, so this experiment uses the same rows. The combined fallback reuses the all-family fit, rather than averaging expert predictions. Markets had no training examples. Guard balancing was off.

Offline improvement does not imply universally better guidance. The direction-cost target misses some cached-unreachable stays, so missed moves, stalls, planning and reachability are separate outcomes. In the original capped8FFA diagnostic window, experts produce40 Clear false-unreachable responses versus2 for shared12;21 versus2 also miss a move a fresh selector would take. These occur12–189ticks after publication, rather than at the age cap. A frozen-input example with actual benefit214 scores5.678 under the Clear expert versus19.132 under combined, against threshold16. That isolates underprediction on those observed inputs, but does not establish identical live admission demand across trajectories.

## CPU comparison

Three frozen ordinary Mac release prototypes share the same native sources, cap and inference inputs. Four counterbalanced repetitions per arm on two existing common saved states measured8192ticks after256settlingticks. Probes/work/checksum tracing were disabled for CPU. Background Docker load was recorded and left untouched; no uncontended-core claim is made.

| Capped model | CPU ms/measured tick | Measured simulation CPU s | Process CPU s | Process wall s | Measured ticks |
|---|---:|---:|---:|---:|---:|
| Combined12 |0.122695|8.040955|10.513620|11.182969|65536|
| Family experts |0.123021|8.062313|10.521990|11.025720|65536|

Experts are+0.27% versus combined, with paired repeats−0.49%..+1.35%; essentially parity on this panel. Gross process throughput including settling is6129.7ticks/s for experts and6043.5 for combined; CPU/tick excludes settling. These measurements do not replace the older master comparison or establish universal quality. Diagnostic on/off runs reached identical checksums for each arm.

The one-hot17 alternative is retained only as research evidence. It is excluded from production. All3 prototype headers passed2650 actual-model Python/C++ UBSan parity vectors each.

## Additional bounded quality check

Two additional held-out maps were selected by frozen group hash, distinct generators and Maxima/Warrush roster, without looking at outcomes. Their original179/178 map bytes were loaded by the capped shared12 prototype for8192warmup ticks; each arm then used identical182 save bytes and ran256settling plus8192diagnostic ticks. All four continuations completed, without timeout or replacement. This is a shared-policy-conditioned panel, not an unbiased full-game tournament.

Orchard Commons direction extra cost/query is0.0773 combined versus0.1076 experts; Shattered Coast is0.01484 versus0.01212. Aggregate cost/query is0.04378 versus0.06828, and stalls0 versus2. Query populations differ. Despite the roster, all complete Guard episodes had zero captured goals, so this panel does not establish painted Guard support or erase the earlier8FFA limitations. No extra collection or fit rows were added.

`FAMILY-CAS.json` and `family-cas.tar.gz` contain the frozen prototype build patches/headers,24CPU runs, original6quality runs, extra warmups/4quality continuations and exact input maps/saves. The archive excludes ELF/compiler/dependency outputs. `unpack-family-cas.py` verifies hashes before reconstructing395 logical files from292 objects. Family fitting and per-AI goal-support reports are under `family-models/`.

## Historical baseline and reproducibility

BASELINE12-README.md preserves the earlier shared12 selection, rejected seven-input reachability case, old CPU comparison against master and acceptedgen005, and its reproduction instructions. Its 'selected' and pending-qualification wording refers to that earlier baseline, not the final family-model decision. Old source182/SIM92 and research181/SIM91 claims must not be relabelled as final183 evidence.

benchmark-cas.tar.gz and BENCHMARK-CAS.json preserve those baseline runs and commands. The training archives, split registry and reports remain unchanged. New family reports, raw prototype receipts and final native evidence will be added alongside them with source/binary hashes. Binaries, object files and dependency trees are excluded from published archives; reproduce ordinary release builds from the recorded source/toolchain/options.
