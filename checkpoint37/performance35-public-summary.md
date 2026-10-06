# Checkpoint35 performance review

**The 3% acceptance requirement remains unmet.** Of 55 baseline/candidate endpoints, 49 pass, three repeatably fail and three remain inconclusive. Both additional catalog-scaling endpoints pass.

Candidate: `63a3b14098323ef0b5f8657c1d78521f29cb7ade`. Comparator: `01848dea7790884bd341c66754253e2b9845e85a`. These timings do not cover the subsequent checkpoint36 HUD correction.

| Endpoint | Median ratio | 95% interval | Result |
| --- | --- | --- | --- |
| 32×32 natural-resource kernel, swim0 | 1.1342 | 1.1311–1.1389 | Fail |
| 32×32 natural-resource kernel, swim3 | 1.1369 | 1.1345–1.1395 | Fail |
| Dense Cortex AI average | 1.1362 | 1.1293–1.1430 | Fail |
| 32×32 kind0 kernel, swim0 | 1.0086 | 1.0048–1.1426 | Inconclusive |
| Early Nicowar AI average | 1.0206 | 0.9916–1.0328 | Inconclusive |
| Dense Castor AI average | 1.0415 | 1.0177–1.0513 | Inconclusive |

All eight whole-game CPU endpoints and the no-AI fixture pass. These passes do not waive other gates. AI averages include warmup; whole-game CPU excludes it. Trajectories differ—for example, dense Cortex finishes with 59 versus 101 units—so attributing AI overhead requires care without reclassifying its measured failure.

Independent audit verified all 1,119 processes, exact inventories and durations, 13,860 matching paired kernel digests, raw timing medians, final integrity records and all four actual executable hashes. Stored intervals were checked against the declared gate; bootstrap intervals were not rerun. Complete team/history/checksum records repeat within each revision and scenario.

Candidate median process RSS is higher by approximately 0.9–2.4 MiB across these cohorts. Catalog setup medians grow from 10.1 to 46.6 to 201.5 ms for 55, 256 and 1,024 definitions; its passing per-tick scaling does not establish constant setup or memory cost.

A short observation confirmed active Firefox processes from another worktree. Their threads were restricted to CPUs 4–7, sharing neither benchmark CPU12's core, SMT sibling nor L3 group. Shared-package effects remain possible but unmeasured. This does not establish causation or justify discarding samples.

The finite maintenance follow-up remains closed with six passes and two inconclusive 512-stocked cases. Earlier cohorts remain retained and unpooled. Keep the PR draft pending acceptance resolution, accessible evidence publication and human gameplay review of Maxima's documented pacing change. Platform omissions remain explicit.
