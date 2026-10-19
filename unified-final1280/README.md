# Adaptive gradients: final zone gate and filtered family models

The final engine uses separate Materials, Guard, Clear and Forbidden models with
a combined fallback for Markets and unknown future family IDs. All heads share
the same twelve covariates and deterministic integer evaluator. No one-hot model
or AI-identity input is deployed. Markets remain unmeasured.

The selected policy is threshold24 direction-cost units (24000 milli), a32-tick
minimum between new warm-field admissions, eight-tick publication, H128 labels
(offsets8..127), and a demanded capture-age1024 safety refresh. Idle fields remain
idle; cold first-use fields may initialize immediately. Committed jobs preserve
their deadlines. Lazy building/consumer paths keep their separate policy.

## Empty-zone request gate

Map maintains exact per-team painted counts for Guard, Clear, Farm and Forbidden
masks. Painting/erasing updates only changed team bits; load reconstructs counts
from the saved masks. Unit queries do not scan the map. Guard fallback and idle
clearing skip empty zones, with enabled Farm paint included for clearing. A
painted but blocked zone stays eligible. Forbidden unit escape already requires
a painted current tile. Tests cover first paint, published-field erase/repaint,
RNG and demand suppression, overlapping teams, idempotent edits, tile replacement,
historical mask padding, binary/text saves, resizing, tiling and Farm rules.

## Conservative historical refit

The original frozen campaign remains1280 planned trajectories:500 completed and
780 explicitly censored, including3 intentional user stops. It contains293779
complete episodes and287045 eligible rows. Original archives and persistent
whole-map splits are unchanged.

The derived fit excludes49925 eligible Guard rows from six nonpainting AI seats:
Cabino12345, Castor9680, Cortex10698, Econo613, Nicowar8999 and Numbi7590.
All1280 initial map files and actual game command/AI rosters were verified: pure
initial map generation and no external order stream. The old telemetry has no
raw painted-zone history. All ambiguous Maxima/Warrush zero-goal rows remain;
zero eligible goals does not prove zero paint. This is a historical campaign
filter, not a runtime AI blacklist or a general zero-benefit filter.

The derived population has237120 eligible rows,19791 Guard rows, and55582 held-out
rows across the same315 held-out maps. On identical retained rows/weights, old
versus refitted held-out regret is2.514794 versus2.504155 (0.42% lower); MAE is
8.199112 versus8.189618. Guard regret is unchanged at0.149955, while Guard MAE
and magnitude calibration worsen slightly. These are modest mixed results,
not proof of universal improvement. Bounds, allfive coefficient sets and4240
actual-model Python/C++ UBSan vectors pass;42 permanent model tests pass.

`zone-gate-filtered185/` contains the original/derived identity and exclusion
proof, fit/paired reports, selected model/header, reproduction helper and final
matched CPU/quality receipts. `native185/` contains final release and compatibility
proof. `reviews/` contains independent source/model review receipts.

## Final CPU and diagnostic comparison

Four counterbalanced repeats on two common saved states measured 65,536 ticks
per arm. Final CPU/tick is **0.121302 ms**, versus **0.154532 ms** for matched
master: **21.50% lower**, with paired local savings of 20.18–23.50%. Final measured
simulation CPU totals 7.9496 s; process CPU 10.5008 s; wall 11.5031 s; gross
throughput 5,875.3 ticks/s. Master is 10.1274 / 12.7390 / 13.9791 s and 4,834.7
respectively. The tiny refit-only CPU difference is effectively flat locally.
The complete master comparison includes other PR changes and resulting
trajectories; remaining savings are not pure model-intelligence attribution.

Zone gating removes empty observations without changing raw extra-cost/stall
counts in these warm-state windows. The refit reduces four-player direction
extra cost 1,645 to 1,352 and stalls 7 to 5; eight-player counts are unchanged.
Planning queries and error counts change, and removal of zero-cost queries
mechanically changes aggregate rates. The full per-family/readout tables report
raw totals, denominators, missed moves, stalls and planning separately. No broad
quality or cold/full-game generalization is claimed.

## Final verification

Production source4285ca7ea, exact regenerated golden fixtures5f0f3abdf;
version185/SIM95/replay185, unchanged state layout178 and save floor58.
Model e828b0a1d0d39881825be5f08b8b8145aed90f3d52709913a244a28d50d11f03;
header d22e1eb738d69131984c36f5329cd5673c7e5fc26bc27b6c9040c06582d43985.

Mac ordinary O3 release passed17 golden checks,72 scheduled focused checks
(97 JUnit cases),8 runtime observer/thread/save-continuation cases and the
simulation-version/CLI gates. Linux ordinary O3 release also passed the same 17 goldens, 72 focused checks / 97 JUnit cases and eight runtime cases. All 32 raw checksum/save pairs and 16 diagnostic CSV pairs match Mac byte for byte. Full final CPU results are in the linked receipts. The initial
zone-gate build also passed49 focused JUnit cases, including40 MapQuery cases.
No development/PCH/unity build is used for timing.

CPU repeats disable probes and run in counterbalanced order; diagnostic quality
runs are separate. Background load is recorded, not described as uncontended.
Direction-cost labels omit some stalls/false-unreachable consequences; those
outcomes and planning denominators are reported separately. Guard balancing was
off; empty Guard traffic does not establish painted-area quality. Windows,
browser/mobile and interactive gameplay feel were not executed for this revision.

## Historical evidence

The root baseline12 files and BASELINE12-README.md preserve earlier combined12
and rejected-seven experiments. BASELINE-FAMILY184-README.md and family-models/
preserve one-hot/family prototypes, the earlier family production policy and
threshold/minimum sweeps. native184/ is the preceding policy's proof. These
remain identified by their original source/binary hashes and must not be
relabeled as final185 measurements. In particular, the earlier direct-master
CPU result for16/min8 is not the final24/min32/zone-gate result.

Training archives and CAS indices verify exact logical-file/object hashes;
unpack helpers reconstruct raw maps, saves, replays, checksums, CSVs and command
receipts. Published archives exclude ELF binaries, object files, dependency trees
and credentials. Reproduce ordinary builds using recorded source, flags and
toolchain. Artifact reports belong on this evidence branch, not the production PR.
