# Final 512-game model and native verification

Production: ca6365e83900e2668ff2348044a89ba3b2d70c43 (sim80, save174, replay floor174). Test-only follow-up: df25376f8b681dcd8d87dd6832b4e93a935cad96. Base: 3d896cbb95293da6d24caddd247a62c9ca99d77f. The PR was rebased and deliberately composed with current master’s delayed cold routing and AIR namespace. Independent review approved the final source after fixing AIR save visitation, distance-only recruitment liveness and reserved-slot initialization.

## Training

Exactly512 selected original planned slots,64 for each of eight formats. Selection preceded results; all selected slots completed, no duplicates or unresolved failures. Actual coverage:67 generators,10 square/rectangular dimensions64..512,36,570,126 simulation ticks. The retained lossless episode files contain476,107 complete episodes from500 labelled maps;12 completed games had no complete observed episode. All512 remain in the provenance. The fit uses374 development map groups and126 heldout groups, stratified across formats, with three grouped development folds and format/map/collection/episode weighting. No material identity is a feature.

`training/manifest.json` uses relative episode paths and preserves their original SHA256. It can be passed directly to `tools/resource_gradient_model.py fit --manifest ... --output ... --header ...` from the feature source with the pinned development requirements. `training/fit/` contains the selected model, generated header and old/new metrics on exactly the same heldout rows. Original immutable plans, actual specs and source/binary/map/result/episode provenance are retained. Changing the portable manifest’s paths changes its file hash but does not change collection identities, rows, weights or grouped split.

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

The final matched comparison is running: eight preselected heldout maps, one per format, master/PR, two repetitions, serial execution with reversed/interleaved order, one compute thread, CPU backend, collection disabled,90000-tick cap or earlier engine end. It uses identical master173-generated map bytes for both engines. The stopped pre-initialization timing attempt is excluded. Do not infer current-master savings from the earlier experimental baseline. Completed results and honest CPU/tick,totalCPU,wall,ticks,throughput comparisons will be appended here.
