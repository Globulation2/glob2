# Final runtime-resource behavior tournament

Tested candidate: `b3fab1a48`, executable SHA256 `7de08ee1bcbb74b7d711efd29ad84237a499efc4626055d61fb1bab1db0fbbc9`.

All 42 candidate games completed successfully with no parsing or integrity failures. The pure-master baseline runs were reused only after verifying identical fixtures, commands, environment, driver/extractor, runtime data, binaries, resolved libraries and raw evidence hashes. [Published pure/approved controls and original fixtures](https://github.com/Globulation2/glob2/tree/963b0fe) supply the other arms. This package adds every new candidate raw log and both comparisons. It makes **no performance claim**; the runs overlapped unrelated host work.

The corpus contains 40 primary games across eight terrain/team-count/map-seed geometry clusters and two historical 64×64 controls, reported separately. Seeds are 1001/1002, game seed19, horizon90000ticks, jobs4. Mixed AI configurations include swapped sides. Exact commands and input hashes are archived.

## Primary outcomes

Values below are geometry-balanced paired changes, with descriptive95% cluster bootstrap intervals (10000 resamples, seed4242). All metrics, not merely selected directions, are retained in the CSV files and JSON summaries.

| Metric | Pure → candidate | Approved fixes → candidate |
|---|---:|---:|
| Final units | −10.425 [−25.475,0.275] | +1.6 [0,3.35] |
| New buildings | −4.125 [−8.5,−1.25] | +0.4 [−0.575,1.8] |
| Starvation deaths | +7.775 [−2.975,19.225] | +0.45 [−3.85,6.475] |
| Food deliveries /1000ticks | −1.0702 [−2.5684,0.4428] | +0.6361 [−0.0747,1.4809] |
| First-building upper waiting bound | −19.2ticks [−57.6,25.6] | 0 [0,0] |

Pure, approved-fix and final arms resolved14,15 and16 of40primary games respectively. Winners changed in2/10jointly resolved pure/candidate pairs and0/12jointly resolved approved/candidate pairs. Unresolved/tick-cap games are censored, never scored as losses. Resolved-only outcome comparisons have selection bias. The units residual interval's lower endpoint is numerically1.39e−17; it is displayed as zero, not treated as evidence of a strictly positive effect.

The approved fixes explain much of the difference from pure master; they are not part of a performance waiver or proof of exact equivalence. Eight clusters and two seed values cannot establish gameplay equivalence. Final counts depend on game duration. Construction is interval-observed at512ticks; right censoring and game-end/elimination competing events are retained, but interval uncertainty is not included in the bootstrap. No project-start timestamp exists.

## Integrity and chronology

The enclosing launch wrapper exited143 after all games and the successful final `runtime_stable=true` audit, before analysis finished. No engine failed or reran. Analysis-only recovery revalidated all84records (42reused+42new) and exact original inputs, then ran the unchanged parser/bootstrap. The interruption cause is unconfirmed; its timing near30minutes is consistent with an enclosing-session lifetime limit. The recovery manifest remains in the ZIP.

Later commit41678773c changed non-English translation data **after** the completed b3 run audit and residual-analysis start. This package reconstructs and verifies the exact recorded b3 JSON/TXT/JS data hashes; it does not substitute those later translations or claim current-workspace equality.

Five lossless log shards are each below50MB. Every decompressed member was SHA256-checked against its original run record. `inventory.json` records all member and archive identities. Logs retain their original telemetry verbatim; any embedded timing samples are not accepted performance evidence. No executable, dependency cache, or performance benchmark dataset is included.
