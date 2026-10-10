# GPU CPU-saving campaign closeout

The user accepted the measured improvement on October 10, 2026 as sufficiently close to the original 30% target. Further optimization experiments have stopped. This is acceptance of the development CPU-saving result, not a claim that the original release gates passed.

## Measured result

The original five paired rounds measured 24.2% lower total game-process CPU per simulation tick versus the frozen PR CPU baseline: ratio 0.7581, one-sided 95% upper bound 0.7640. The later epoch configuration measured 25.5% lower CPU versus its same-source CPU control: ratio 0.7450, upper 0.7515. These are different comparisons on one early 512² open-game workload and must not be combined into an aggregate claim.

The original screen's tick p99 worsened 20.4%; the epoch screen's p99 worsened 16.1%. Ambient desktop GPU activity and observer controls also limit qualification. The campaign does not establish a universal map-size improvement, a RAM saving, or a release-ready automatic policy.

The final short nine-core screen compared Frozen8, Frozen16 and Jacobi4 with check intervals 1, 8 and 32. No configuration met both the original CPU and responsiveness gates. Frozen16/check8 measured a diagnostic CPU ratio of 0.7671 and tick p99 ratio of 1.0592; each configuration had only two retained pairs. Longer convergence intervals sometimes increased dispatches through overshoot. Command-count reductions alone did not establish integrated benefits.

## Retained checkpoint

[Draft PR 1045](https://github.com/Globulation2/glob2/pull/1045) contains tested integration commit `252b54563aacc604cb2f10469f361f8665fbb821`, identical in source to evidence freeze `786feaa6a3c19b6a54127959fa4aca564380532c`.

The release game, unit and engine binaries built successfully. All 114 selected unit cases, 106 selected engine cases and 27 fresh real-device cases passed. The selected inventories were frozen and checked; these counts do not imply complete replay/network/failure qualification.

Six 512² configurations preserved complete per-tick checksums for 2,048 initial ticks plus 2,048 ticks continuing each configuration's own save. Four bounded-AI 1024² fixtures passed all 24 CPU/GPU/epoch initial and own-save continuation traces, 512 + 512 ticks each. Original generator and codec limits were preserved.

Displayed CPU/GPU correctness preflights matched exact simulation traces. The default desktop used Mesa software rendering; explicit X11 used NVIDIA rendering. Warm diagnostic presentation CPU varied substantially, so the apparent single-run X11 CPU improvement is not attributed to gradient offloading.

Owned requests and one GPU coordinator let CPU workers proceed while accelerator tickets remain pending. Exact successful output or one original-seed CPU continuation resolves a ticket. Unknown automatic profiles remain CPU; live probing and promotion remain disabled. Experimental epoch masks are retained as an opt-in candidate. Parity argument bindings and worker-side trivial completion did not demonstrate integrated CPU savings.

## Remaining limitations and preserved experiments

The PR remains draft. Complete transitive 64 MiB retained-host accounting is unproved. Required OpenCL failure paths still need a borrowed-buffer lifetime repair when both completion and drain fail. These limitations were found by source review; passing selected device tests does not remove them.

Later local integration `7a5fca4cb` contains disabled or unattached warm-distribution instrumentation, AI resource diagnostics, policy/capture primitives, minimal retained cost views and a CPU empty-layer ablation. These native changes are unbuilt and are not included in the pushed tested checkpoint. The separately reviewed optional backend/quarantine bundle remains isolated and unqualified. No speculative changes were enabled as part of closeout.

Further corpus confirmation, strongest CPU comparison, rendering guards, removal ablations and complete tuning accounting remain unperformed. The user's acceptance ends the optimization campaign without relabeling these gates as passed.

The [first published evidence checkpoint](https://github.com/Globulation2/glob2/blob/45f1f2d191400e2f16e337b5647b37700c7f6cad/report.md) includes raw samples, manifests and reproduction material. Later completed receipts are retained separately for closeout publication.
