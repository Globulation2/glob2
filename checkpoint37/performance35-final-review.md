# Independent final performance review — checkpoint35

**The requested3% performance acceptance remains unmet.** The completed seven-cohort run has49 passes, three repeatable failures and three inconclusive endpoints among55 baseline/candidate groups. Both additional catalog-scaling endpoints pass. All samples and earlier results remain retained.

These timings describe candidate `63a3b14098323ef0b5f8657c1d78521f29cb7ade` against pinned master `01848dea7790884bd341c66754253e2b9845e85a`. Checkpoint36's subsequent cold HUD helper correction and validation must be reported separately; no36 timing claim is made.

| Nonpassing endpoint | Candidate/baseline median ratio | Stored95% interval | Classification |
| --- | --- | --- | --- |
| 32×32 natural-resource kernel, swim0 | 1.134220 | 1.131114–1.138866 | Repeatable failure |
| 32×32 natural-resource kernel, swim3 | 1.136924 | 1.134468–1.139477 | Repeatable failure |
| Dense Cortex decision average | 1.136217 | 1.129278–1.142963 | Repeatable failure |
| 32×32 kind0 kernel, swim0 | 1.008581 | 1.004751–1.142627 | Inconclusive |
| Early Nicowar decision average | 1.020590 | 0.991553–1.032804 | Inconclusive |
| Dense Castor decision average | 1.041453 | 1.017699–1.051253 | Inconclusive |

All eight whole-game CPU endpoints pass, with paired median ratios ranging0.7265–0.9605. The no-AI fixture passes at0.942857 [0.903843,0.948390]. Whole-game passes do not waive AI/kernel gates. Kernel object identity and lower profile instruction counts cannot establish timing compliance or justify changing these classifications.

## Audit evidence

Independently checked all1,119 declared processes:84 kernels,22 no-AI, four248-process game/AI cohorts and21 catalog processes. Exact inventory, alternating lane order, scenario identities, successful exits, declared durations/rules and recorded source/binary provenance agree. All13,860 paired kernel digests match. Recomputed all process/group medians and paired median ratios from raw records; every summary agrees. Checked stored confidence intervals against the unchanged1.03 rule; the35 bootstrap itself was not rerun.

All992 game/AI process records reach the prescribed6,000/12,000 tick cap with1,000 warmup ticks. All992 AI-player timing records have the expected full-session call/sample counts. Complete teams, nested histories, players, resolved configuration and checksums repeat exactly within each revision/scenario, including between instrumented AI and whole-game cohorts. This establishes within-revision repeatability, not historical cross-revision trajectory parity.

The main driver completed all seven benchmark/analyzer stages and final input checks. Independently hashed both preserved35 candidate binaries and both baseline binaries; all four match the frozen equivalence report. No changing36 build objects were used to substantiate35 provenance. Machine-readable inventories and checks accompany this review.

## Workload and memory limits

AI averages include the entire session, including warmup; whole-game CPU excludes warmup. Different trajectories limit causal attribution without waiving acceptance. For example, dense Cortex ends with59 units/one building on master versus101 units/five buildings on the candidate. The no-AI endpoints are215 versus214 units,24 buildings each,52,174 versus51,702 total health and760 stored in both lanes. These differences are retained; no historical-RNG compatibility claim is made.

Median process RSS in KiB (master→candidate): kernels48,628→51,116; no-AI30,140→32,296; early games48,698→49,846; dense games48,718→49,620; early AI48,678→49,690; dense AI48,688→49,758. These are whole-process observations, not allocation counts or isolated cache memory measurements. Earlier allocation/routing evidence must retain its original checkpoint and scope.

Catalog256/55 per-tick ratio is0.999143 [0.978892,1.027127];1024/55 is1.005305 [0.995203,1.015316]. Setup medians increase10.090→46.588→201.460ms for55/256/1024 definitions. Catalog median/max RSS is83,608/84,064KiB. Passing unused-definition per-tick scaling does not imply constant setup or memory cost.

## Host-activity limitation

A25.23-second read-only observation near the catalog run found two approximately14.6-hour-old Playwright Firefox content processes actively consuming about105% and100% of one logical CPU; their parent browsers consumed another26% and29%. Their parents/Node launchers originate in a different worktree. All inspected threads were restricted to CPUs4–7, excluding benchmarkCPU12 and its SMT sibling28. Browser L3 sharing group4–7/20–23 is distinct from benchmark group12–15/28–31.

Thus concurrent unrelated activity was observed, but direct core/SMT/L3 competition from these processes was not. They share physical package0, so package power, thermal and memory effects are possible and unmeasured. This short observation cannot characterize every earlier sample or establish the cause of any regression. No process was stopped, no sample removed and no result invalidated on this basis. Affinity was never exclusive host isolation.

## Closed maintenance and delivery status

The separately completed finite checkpoint34 maintenance follow-up remains closed: six passes/two inconclusive512-stocked cases, swim0 ratio1.033401 [1.017573,1.049812] and swim3 ratio1.026926 [1.006610,1.040793] under its prespecified primary median intervals. Its original31-pair cohort and127-pair follow-up remain separate and unpooled. No maintenance sampling was restarted for35, and no further sampling is planned under that protocol.

Retain the PR as draft while acceptance remains unmet. Publish exact revisions, commands, input/provenance records, raw results and the complete gate inventory alongside this review; local-only files are not sufficient public review evidence. Keep platform omissions explicit. Maxima's documented pacing change still requires human gameplay review, which automated fixtures and screenshots do not replace. Any acceptance-scope change requires an explicit user decision after the complete evidence is reviewable.
