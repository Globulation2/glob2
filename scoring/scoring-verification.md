# Scoring update verification

PR head: `3c59e1a019a10c0a8f450bef698d66236fb90bdb`. Gameplay source and the prior native516/Maxima77 validation are unchanged from857761d70d.

-67 tournament tests pass, including trajectory integration, full/compact compatibility, missing-history handling, grouping, reports and worker protocol.
-310 Markdown documents checked without errors; sim-version contract passes.
-All960 retained team histories score identically before and after outcome-only compaction.
-Two actual saved-game continuation fixtures produce matching scores.
-Retained288-game cohort re-scored with `log_time_outcome_v1`, c=2, fixed90,000-tick horizon, original model/stopping/seeds and all96 paired configurations. No new games or tuning.

| Format | Contrast | Mean change, pp |95% seed-block interval, pp |
| --- | --- | ---: | --- |
|1v1|adaptive_minus_baseline|+1.626|[-1.568,+4.891]|
|1v1|single_minus_baseline|+1.626|[-1.568,+4.891]|
|1v1|adaptive_minus_single|+0.000|[+0.000,+0.000]|
|ffa|adaptive_minus_baseline|+0.618|[-0.772,+1.865]|
|ffa|single_minus_baseline|+0.229|[-1.300,+1.627]|
|ffa|adaptive_minus_single|+0.388|[+0.000,+1.158]|

Intervals use10,000 whole-seed-block draws, seed19,16 blocks per format. The cohort is still SIM117 before master/farming integration, so it does not establish final integrated SIM120 effectiveness. No claim of cross-platform execution is added.

[Machine-readable scores](scoring-verification.json) · [Test log](tournament-tests.log)
