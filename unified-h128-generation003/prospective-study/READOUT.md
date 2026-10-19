# Gen003 short prospective comparison

All 18 probeOFF timing attempts (4096ticks), 9 separate probeON diagnostic attempts (1024ticks) and 6 fresh equivalence runs passed with zero caps. The root-qualified ordinary ELF was unchanged; the isolated performance binary uses only the same three CLI validation-guard changes, with exact checksum/final-save equivalence before timing. All three rebuilt translation units match their current ordinary native compiler flags. Evidence is mirrored locally and sealed.

| Arm | Run CPU ms/tick | Total process CPU ms/tick | Wall TPS |
|---|---:|---:|---:|
| Adaptive | 0.210214 | 0.346705 | 2817 |
| Fixed32 | 0.220180 | 0.359656 | 2695 |
| Fixed128 | 0.198749 | 0.336891 | 2880 |

Within this short sample, adaptive run CPU is 4.53% below fixed32 and 5.77% above fixed128. These are shared-host CPU 2/nice19 observations with SMT sibling CPU 3 active, not isolated speed estimates. All arms ran 27–37% faster than at the gen002 walltime; the eighteen terminal simulation checksums and shared/lazy work counts exactly match gen002, so that cross-generation timing change cannot be attributed to coefficients.

The separate quality aggregates also exactly match gen002. Adaptive observed 116 direction-cost excess over 530 movement observations; fixed32 observed 26/492 and fixed128 30/514. Planning false-unreachable counts were 75/13420, 15/12922 and 49/12960. Policies can visit different worlds/queries, so these are sampled descriptive outcomes, not comparable whole-game loss estimates. Adaptive timing admits were reactive only (20), with no model-triggered admission in these early 4096 tick workloads. This study therefore provides little evidence about the new coefficient generation itself.

No installed-master comparison, stable-promotion claim or training ingestion. Same three persistent heldout maps, fixed seeds, schedules and limits as gen002. Detailed attempts/caps, process CPU/wall/ticks/TPS and raw diagnostics are preserved in TIMING-SUMMARY.json, QUALITY-SUMMARY.json and per-attempt outputs. CROSS-GENERATION-TERMINAL-CHECKS.json records unchanged behavior. Source4a2cf3d, frozen ELF5f6616ed…b848, isolated performance ELF0d062280…2d91.
