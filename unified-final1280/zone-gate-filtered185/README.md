# Final zone-gated, filtered family models (185 / SIM95)

Selected deployment uses threshold 24, minimum refresh spacing 32, publication delay 8,
H128 labels and a demanded-field age cap 1024. Combined plus four measured family
experts use 12 covariates. Markets and unknown family IDs use the combined fallback.

* Model SHA256: `e828b0a1d0d39881825be5f08b8b8145aed90f3d52709913a244a28d50d11f03`
* Header SHA256: `d22e1eb738d69131984c36f5329cd5673c7e5fc26bc27b6c9040c06582d43985`
* Production source: `4285ca7ea6db02cd114d10692074e9fa7796f06a`; fixture-only source `5f0f3abdfd60f5956d67186ec37d2d542f105e6e`.

See `comparison/READOUT.md` for the32OFF and6ON comparison, raw per-family outcomes,
CPU/process CPU/wall/ticks/throughput and local paired repeat ranges. Final CPU/tick
is 21.50% below matched master, but only 0.27% below the same 185 zone-gated unfiltered
control (paired range -3.04% to +4.05%, effectively flat locally). The master gain
includes the complete PR and different trajectories. These two previously examined
states do not establish population generalization or individual-feature causality.

Empty-zone gating removes zero-cost observations:184 to 185-unfiltered leaves raw
extra-cost totals/stalls unchanged here while removing 90 Guard queries in 4FFA and
47 Guard plus 106 Clear queries in 8FFA. Ratios alone therefore mislead. The refit
reduces 4FFA raw extra cost 1645 to 1352, missed moves 21 to 13 and stalls 7 to 5;8FFA is
unchanged. Planning query populations also differ. Report raw totals and extra/tick
alongside rates; remaining stalls are not automatically proven congestion.

The immutable comparison PLAN retains obsolete inherited operator fields
`final_native_qualification`, `baseline_threshold_milli` and the shorthand
`qualification` directory. They are not this run's policy or qualification proof.
Use each arm's explicit actual threshold/minimum and result metadata, plus
`SOURCE-QUALIFICATION.json` and `MACOS185-QUALIFICATION.json`. No experiment was
rerun or modified to hide those inherited metadata fields.

Training retains 1280 original slots (500 normal,780 censored). The row filter
removes 49,925 eligible Guard rows attributed to six nonpainting AI seats, leaving
237,120 eligible rows and 19,791 Guard rows. This is a conservative, accepted
AI/provenance approximation: raw paint history was not recorded. Zero captured
goals does not prove absent paint; all Maxima/Warrush zero-goal rows remain.
Original archives and split assignments stay unchanged; the same hierarchical
weighting algorithm recomputes weights on the retained population.

`training/PAIRED-REPORT.json` compares both fits on the same55,582 retained heldout
rows: regret 2.51479369 to 2.50415512 (0.42% lower), MAE 8.19911220 to 8.18961835.
Calibration and Guard MAE worsen, so this is not a dominance claim. Offline metrics
apply neither minimum spacing nor forced age cap. Full fit, exact exclusions,
derived episode bytes/manifests and original split registry are archived, not
substituted for the original five training archives elsewhere in this evidence.

`training/reproduce-filtered-fit.py` reproduces combined and all four expert heads
from those original extracted five archives, terminal-inputs.json and map-splits.json
using the pinned fit environment and tested permanent tools. It was syntax checked;
no redundant refit was run while packaging. `fit-tooling-source.tar.gz` preserves
the exact fit tooling, and FIT-COMMAND.json records the original one-time fit.
The numeric receipt covers 4,240 actual Python/C++ UBSan cases and 42 model tests.

`comparison/raw-cpu-cas.tar.gz` is content-addressed: verify with CAS-VERIFIED.json
and reconstruct via `python3 comparison/unpack.py OUTPUT`. It includes commands,
load observations, results, quality/work CSVs and exact two 175 input maps/saves.
`training/filtered-training-raw.tar.gz` has a per-file SHA256 index and a full
readback-verification receipt. Both exclude executables, object/build trees and
runtime dependency payloads. Earlier 184/183 evidence remains separate history.
