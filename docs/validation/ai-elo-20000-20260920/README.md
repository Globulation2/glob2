# Expansion to 20,000 compatible-generator duels

The second 10,000-game batch started on 2026-09-20, retaining the completed first
10,000 games. Sample seed 20260921 gives no map-seed or job-ID overlap with the
first batch (20260920). This directory records the plan, not completed results.
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

## Local continuation

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
