Rebased onto master now that #253 is in. Two commits: per-delivery hiring, then Leo's gate on top.

## Summary

A worker stayed subscribed to its building until the building's whole wish list was satisfied. After every drop-off it picked its own next resource from `attachedBuilding->computeWishedResources()`, so the trip belonged to whoever had delivered there last, never to whoever was closest to it and never to another building that wanted a worker more.

**First commit:** release the unit after every deposit (`stopAttachedForBuilding(false)`), one gig at a time. `Team::updateAllBuildingTasks` runs later in the same tick, after every unit has stepped, and `considerUnitForBuilding` only accepts `ACT_RANDOM` units, so a unit freed during its own step is back on the market before that tick ends. The random step it starts on release is still in flight while the auction runs, so a unit that wins its own job back pays one tile for it.

**Second commit (Leo, after the numbers below):** release only while at least 20% of the team's living workers are idle (`RELEASE_IDLE_WORKER_PERCENT`, `Team::idleWorkerShareAtLeast`). The auction only moves work when idle workers exist that may by chance stand closer to the next job; with nobody idle it could only hand the trip back to the deliverer a tile later. Otherwise the worker keeps its building and picks its next trip as on master, market path included, so the "market fetch gap" of the first commit only exists while a fifth of the team is idle.

Simulation behaviour changes. On current master, `SIM_REVISION` is 13 and the golden match record and verification trace are regenerated. Save format 133, save compatibility floor 58, and replay floor 127 are preserved.

## Measurement of the first commit (release always)

Eight Nicowars on Playground, resumed from a tick-25000 save, run to 40000, both builds on top of #253, ASLR off, `MALLOC_PERTURB_=0` (every cell identical across fills 0/85/170). M = master, G = release always.

| seed | deliveries M / G | starvation deaths M / G | population M / G |
|---|---|---|---|
| 7 | 3289 / 3258 | 44 / 35 | 244 / 277 |
| 11 | 2839 / 2918 | 23 / 12 | 228 / 267 |
| 12 | 1792 / 1616 | 53 / 29 | 169 / 161 |
| 13 | 2669 / 2752 | 35 / 30 | 282 / 298 |
| 14 | 2939 / 3015 | 21 / 17 | 287 / 297 |
| 15 | 2987 / 2505 | 30 / 69 | 243 / 187 |
| **total** | **16515 / 16064** (−2.7%) | **206 / 192** (−6.8%) | **1453 / 1487** (+2.3%) |

petri2 (four AINone colonies, ten seeds, 120000 ticks): 37559 master vs 37411 (−0.4%), flat per 2048-tick window from the first minutes on (comment below). ~25% of deliveries changed employer the same tick, ~59% re-hired at the same building: the mechanism works, it just moves no work while free workers are plentiful.

## Measurement of the gated version

Running; tables follow in a comment.

## Feel

Per CLAUDE.md, this changes how the economy feels: while a fifth of the colony is idle, workers no longer "belong" to a building between trips, so units switch employers more often and the "N units working" counters fluctuate. With everyone busy nothing changes.

## Verification

- `gig-release-test` harness (new, in CI): four of five idle → released; nobody idle → keeps the inn and heads for the next wheat; exactly one in five → released.
- `TestsRunner` (187) plus the hiring harness set.

Authored by Carol (first commit) on Leo's idea; gate and rebase by Bob.

🤖 Generated with [Claude Code](https://claude.com/claude-code)


## Current-master refresh

Rebased onto `a05d6cd8c`. The regression now uses `EngineFixtures` and the engine doctest registry, including the renamed `WHEAT` resource. The older benchmark tables above describe their stated historical revisions. The existing changes-requested Econo balance review remains open; this refresh does not claim to resolve it.


## Separate market proposal

#257 now contains market-fetching support independently on master and no longer includes this PR. #258 is stacked only on #257. This hiring branch and its unresolved Econo balance review are unchanged by that separation.
