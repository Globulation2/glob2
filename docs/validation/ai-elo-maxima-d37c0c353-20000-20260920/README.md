# Fresh 20,000-game tournament with updated Maxima

This is a new source-version cohort, not an extension of the previous 20,000 games.
It uses master `d37c0c353d9c2c68b4543286aa9b14e23ae7e526`, including #367 (open rally placement) and #368
(army birth/training capacity). The other seven AIs and the previous 60 generators
are unchanged. The prior-version results remain separate reference evidence.

Format: 20,000 random 128×128 duels, eight AIs, 60 generators, 10,000 full swapped-side
map/game-seed pairs, 90,000-tick cap, prestige adjudication, probability victory off,
five map candidate rolls. Seed 20260922 has no map-seed or job-ID overlap with the
prior 20,000-game cohort. Generator quotas are 332 or 334; matchup quotas 714 or 716.
Rice Terraces uses `slant=0`. The six format-incompatible generators and editor-only
Uniform remain excluded; Last Treeline is omitted to preserve the reference pool.

All 240 short-game preflight cases passed: 120 on macOS and 120 on Linux, including
36 on the newly added therig.local. Preflight games are not rating observations.

## Fleet

| Host | Slots |
| --- | ---: |
| localhost | 8 |
| devlaptop.local | 16 |
| pharaoh-dev-1.local | 4 |
| pharaoh-dev-2.local | 4 |
| pharaoh-dev-3.local | 4 |
| therig.local | 32 |
| Total | 68 |

Build assignment is reproducibly shuffled with macOS:Linux weights 1:5, informed by
prior throughput; native execution remains constrained to the matching binary.

The immutable binary IDs are in comparison.json and source revision in source.json.
The same standard-library Bradley–Terry batch fit weights every new game equally;
95% uncertainty uses 1,000 whole-pair bootstrap resamples within generators, seed 1.
No old-Maxima outcomes will be pooled into the new ratings. Report point differences
against the previous cohort separately, with model/selection limitations explicit.

## Progress and completion

Runtime root: `/Users/bradley/glob2-pr-help-5/artifacts/tournaments/ai-elo-maxima-d37c0c353-20000-20260920`.
`results/` retains the immutable experiment, worker package, job ledger and results.
The LaunchAgent `com.bradley.glob2-elo-maxima-d37c0c353-20260920` checks every five
minutes, records two-hour snapshots, and restarts an exited coordinator while work
remains. Recognized Hilbert River pre-play rejections are replaced as complete pairs
with fresh seeds; unexpected failures are recorded for investigation.

`finish-results.py` asserts 20,000 successful unique games and complete paired blocks,
then fits this fresh cohort alone. `publish-results.py` updates #364 only from a clean
UI checkout containing the pinned master revision, runs release/UI/rating checks,
commits, pushes and updates the PR description. A fresh screenshot and CI still need
review after publication. `monitor-error.json` records failures; `published.json`
stops further monitor actions. No automatic merge is enabled.

Current scores in #364 remain those from the previous source revision until this
new tournament finishes. All previous datasets and analysis are preserved.
