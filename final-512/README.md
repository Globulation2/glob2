# Final 512-game model and native verification

Production: ca6365e83900e2668ff2348044a89ba3b2d70c43 (sim80, save174, replay floor174). Test-only follow-up: df25376f8b681dcd8d87dd6832b4e93a935cad96. Base: 3d896cbb95293da6d24caddd247a62c9ca99d77f. The PR was rebased and deliberately composed with current master’s delayed cold routing and AIR namespace. Independent review approved the final source after fixing AIR save visitation, distance-only recruitment liveness and reserved-slot initialization.

## Training

Exactly512 selected original planned slots,64 for each of eight formats. Selection preceded results; all selected slots completed, no duplicates or unresolved failures. Actual coverage:67 generators,10 square/rectangular dimensions64..512,36,570,126 simulation ticks. The retained lossless episode files contain476,107 complete episodes from500 labelled maps;12 completed games had no complete observed episode. All512 remain in the provenance. The fit uses374 development map groups and126 heldout groups, stratified across formats, with three grouped development folds and format/map/collection/episode weighting. No material identity is a feature.

`training/manifest.json` uses relative episode paths and preserves their original SHA256. It can be passed directly to `tools/resource_gradient_model.py fit --manifest ... --output ... --header ...` from the feature source with the pinned development requirements. `training/fit/` contains the selected model, generated header and old/new metrics on exactly the same heldout rows. Original immutable plans, actual specs and source/binary/map/result/episode provenance are retained. `training/collection-source.bundle` preserves the frozen original collector/fix source lineage; fetch it in a clone containing prerequisite64c792a61c4b5a7cdbfc49f33e7c846127645056 to recover the original collection sources. Changing the portable manifest’s paths changes its file hash but does not change collection identities, rows, weights or grouped split.

Heldout weighted MAE6.11865→5.58149; MSE573.842→576.676. At threshold32 the new fit selects0.4173% versus0.9608% of weighted observed episodes; selected observed benefit96.608 versus56.714. These are predictions on frozen observed queries, not closed-loop gameplay or performance claims. Format/generator/dimensions slices remain in the model provenance, including weak strata.

## Native checks

Both hosts used ordinary release O3 compilation, no dev_fast/PCH/unity. macOS ARM64 Apple Clang21 and Linux x86_64 GCC13.3 build flags, linked dependencies and environment are in `build/`. Immutable master/PR binary hashes are in `native/`.

- Mac:385 passing focused cases and38 explicit display skips across423 selected cases. The first attempt found four obsolete fixture assumptions; all27 affected SharedWorker/Terrain cases passed after the test-only follow-up. The aggregate is transparent: `native/final-sim80-mac-qualification.json` names both original/follow-up reports and resolved cases. Original failure reports are retained.
- Linux:305 passing focused engine cases, one display skip; followed by28 passing terrain/lifecycle/statistics-screen cases, including screenshots at640x480 and1024x768. This also rechecks the closed-seats save case that exposed the reserved-slot initialization bug on the earlier revision.
- Same master173-generated starting map, game seed and orders: all4096 per-tick world, snapshot and full replay-checksum sidecars match byte-for-byte on Mac ARM64 and Linux x86_64, with one/four compute threads and probe enabled/disabled. `native/final-cross-platform-parity.json` records the hashes. The full39MB vector sidecar is retained once compressed in `parity/`; redundant variants have the same verified hash. Per-run commands/results and world/snapshot traces remain alongside it.
- Integer predictor:8206 clamp/log/signed-range cases match Python on both native architectures; UBSan reports no diagnostics. Exported header matches the committed header byte-for-byte.
- Permanent Python tooling:17 tests pass. Focused Brush tests:22 cases pass. Sim revision/record gate and CLI reference checks pass.
- Real master173 save loaded by final PR, saved as174 and continued on Linux. Binary/text cold/pending, warm batched, partial-AIR and historical save tests pass.

The earlier broad Mac unit run retained in `native/` has RGBA16 decoding-rounding and OpenCL nonconvergence failures outside this change’s focused CPU coverage; it is not reported as a full-suite pass. Windows, browser, GPU execution, packaging and maintainer gameplay acceptance are unverified. No hosted affected/full CI was requested. Keep the PR draft pending acceptance of these limits and performance results.

## Performance

[Final report and chart](performance/README.md), [machine-readable summary](performance/summary.json), [all32 run records](performance/manifest-portable.json), [maps and actual specs](performance/maps/manifest-portable.json).

Eight preselected heldout starting states, one per format, two repetitions per policy:32 serial ordinary release runs. Same master173 map bytes and seeds per pair, one compute thread, CPU backend, collection disabled,90000-tick cap or earlier engine end. No competing analysis/build was deliberately started on the Mac during timing. Reverse format traversal and opposite per-map policy order counterbalance drift. A parity mistake in the second-repetition plan was corrected before any repetition1 measurement; all completed measurements were retained, only an unfinished next attempt was stopped. Original/corrected plans, correction record, both runner logs and analysis code remain in performance/. A continuous host-load/thermal guard was not recorded.

Against master3d896, pooled simulation CPU/tick increases16.51% (1.2234→1.4255ms), observed process CPU increases16.19%, wall increases16.40%, and total executed ticks differ-0.24%. There are3 CPU/tick improvements and5 regressions. Equal-format geometric mean changes-1.37%; it does not establish a workload-weighted saving. The worst CPU/tick regression is8ffa+30.2%; best reduction2v2-30.4%. Every repeated policy/state gives identical ticks, final checksum, winners and termination. Capped totals are censored observed-prefix costs, not full-game finish costs;8v8master ends68770ticks while adaptive hits90000. Trajectories/populations differ. These8 states with2 timing repetitions do not establish a general CPU win or fresh path-quality validation. The PR remains draft for performance/gameplay acceptance.

## Final save continuation

[Save verification](save-continuity/final-mac-save-continuity/status.json) records zero divergences over2048 overlapping exhaustive world checksums and2049 snapshot checksums when a version174checkpoint at2048 is resumed with4threads, compared with a version174checkpoint512 control continued with1thread. Real master173save→174resave→continuation also gives byte-identical world/snapshot/full replay vectors on Mac/Linux. Commands, saves, traces and the initial failed attempt log are retained.

The first raw continuation comparison used a173-header uninterrupted map and174-header saved checkpoint. The map header checksum explicitly includes the loaded format minor. In this3team/3player setup,173^174 rotated13times contributes constant1572864 to every world checksum; snapshots already matched. The matching174 controls above remove that known header difference without masking any checksum bits. No engine change was made for this diagnostic issue. `save-continuity/verify-final-save-continuity.py` records both assertions; original absolute paths in commands/scripts preserve the invocation and can be remapped to this evidence layout.

Final independent review approved source, benchmark design/calculations/presentation and the matched-header save diagnosis. Source remains df253; master fetched again unchanged before final publication. No merge was performed.
