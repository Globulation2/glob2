# Completed 20,000 compatible-generator duels

The second 10,000-game batch started on 2026-09-20, retaining the completed first
10,000 games. Sample seed 20260921 gives no map-seed or job-ID overlap with the
first batch (20260920). Both batches are complete. All 20,000 successful games are retained in 10,000 complete swapped-side pairs; the combined generator totals are 332–336 games. Fourteen pre-play Hilbert River rejections in batch two were replaced with fresh paired seeds. The files here preserve the original second-batch plan.
The same frozen binaries/source f2cfcfeb05b39cc17eab43a532ebb551172de8de are used:
60 compatible generators, 128×128 maps, two swapped-side games per block,
90,000-tick cap, prestige adjudication, probability victory disabled.
Each new generator quota is 166 or 168 games; matchup quotas are 356 or 358.

All five hosts use their logical CPU counts: localhost 8, devlaptop.local 16,
and pharaoh-dev-{1,2,3}.local 4 each. Total: 36 execution slots.

Ratings now fit all outcomes equally with Bradley–Terry; the 1,000-draw bootstrap
resamples complete pairs within each generator. Do not append sequential Elo
updates, average rounded batch ratings, mix AI versions, or count duplicate jobs.
The prior sequential analysis remains historical evidence in PR #364.

## Preserved completion workflow

Runtime root:
`/Users/bradley/glob2-pr-help-5/artifacts/tournaments/ai-elo-all-generators-10000-batch2-20260920`

- `results/experiment.json`, `results/worker.pyz` and SQLite pin immutable work.
- `monitor.py` is installed as LaunchAgent `com.bradley.glob2-elo-batch2-20260920`,
  checks every five minutes, records `health.json` and two-hour snapshots, and
  restarts the coordinator if it exits while work remains.
- `finish-results.py` replaces only the recognized pre-play Hilbert River rejection,
  as whole swapped-side pairs with fresh seeds. Unexpected failures stop explicitly.
  Raw original/replacement attempts are retained. It exports combined outcomes and
  fitted scores only after 20,000 successful unique games and 10,000 full pairs.
- `publish-results.py` updates the clean `split/ai-ratings-ui` worktree, attaches
  evidence, runs build/regression/interactive tests, commits, pushes and updates
  #364. It refuses a dirty checkout and never merges. The new screenshot and CI
  still need review. `monitor-error.json` records failures; `published.json`
  records successful publication and stops further action.

The user explicitly requested that #364 remain unmerged. No auto-merge is enabled.

## Final scores and uncertainty

| AI | Final fitted Elo | 95% paired-bootstrap interval |
| --- | ---: | ---: |
| Maxima | 1857 | 1842–1871 |
| Cabino | 1689 | 1678–1701 |
| Nicowar | 1656 | 1645–1666 |
| Cortex | 1599 | 1586–1611 |
| Warrush | 1390 | 1377–1401 |
| Econo | 1308 | 1296–1320 |
| Castor | 1277 | 1265–1290 |
| Numbi | 1224 | 1211–1238 |

Intervals use 1,000 whole-pair bootstrap resamples within each generator, seed 1. [Combined outcomes, analysis, validation and reproduction](https://github.com/Globulation2/glob2/blob/split/ai-ratings-ui/docs/ai-strength.md) are retained in #364. The UI values are committed and pushed; the PR remains unmerged.
