# Maxima streaming and wave-failure validation

Streaming now divides the available attack budget across persistent flags using the existing per-flag cap (default 20): 45 warriors request 20/20/5. Flags grow and shrink with the force, and surviving flags retain their identities. Training and defense reservations still apply.

After four consecutive failed wave attempts, Maxima permanently switches to streaming. A failure is the existing rally timeout without launch, or a spent/retired wave whose peak healthy enrollment near its objective was below 33% of its launch force. A successful delivery resets the streak. Both conditions and the permanent switch survive reload.

## Real-game sweep

Production revision `5f64ee3f3`; macOS arm64. Twenty generators, two seeds (2101 and 7919), 128×128 maps, Maxima and Nicowar opponents: 80 games and 120 Maxima participants. Runs stop at 90000 ticks or fewer than two surviving teams; 21 reached the cap.

**28/120 Maximas (23.3%) switched, across 23/80 games (28.75%). Isles triggered in all 6/6 Maximas.** Nine generators had no switches. After fallback, 17 of the 28 participants were observed using multiple active streaming flags, with up to four simultaneously. Flag observations sample every 1000 ticks.

| Generator | Games switching | Maximas switching |
| --- | --- | --- |
| braided-delta | 1/4 | 1/6 |
| canals | 0/4 | 0/6 |
| contested-commons | 0/4 | 0/6 |
| drowned-forest | 2/4 | 3/6 |
| even-ground | 0/4 | 0/6 |
| forts | 1/4 | 1/6 |
| hills | 2/4 | 2/6 |
| islands | 0/4 | 0/6 |
| isles | 4/4 | 6/6 |
| maze | 3/4 | 3/6 |
| old-town | 0/4 | 0/6 |
| portage-lakes | 0/4 | 0/6 |
| river | 0/4 | 0/6 |
| rugged-archipelago | 1/4 | 1/6 |
| savannah | 0/4 | 0/6 |
| shattered-coast | 1/4 | 1/6 |
| stone-highlands | 1/4 | 1/6 |
| swamp | 2/4 | 3/6 |
| switchbacks | 2/4 | 2/6 |
| symmetric-arena | 3/4 | 4/6 |

This measures trigger frequency, not false-positive accuracy or win rate. Some unnecessary switches are an accepted tradeoff. The fresh-game matrix had no switches before a first launch; the saved assembly case below directly exercises repeated failures without further launches.

## Saved Isles cases

- Original “maxima not attacking”: switched at observer tick 60137. At tick 67424, multi-flag streaming left 24 enemy buildings / 17466 HP, versus the historical no-fallback endpoint of 27 / 23112. Single-flag fallback reached 20 / 14114; multiple flags are not uniformly better at this endpoint.
- Earlier assembly-stall save: four rally timeouts at observer ticks 44256, 55096, 62477 and 69124, with no intervening launch, triggered streaming. At tick 83432, multi-flag streaming left 22 enemy buildings / 12780 HP; historical no-fallback left 37 / 19634, and single-flag fallback left 25 / 15095.
- Later snapshot: two new waves launched and no fallback occurred before the run ended at tick 102970.

Observer event ticks follow the AI decision by one simulation step. Historical comparisons are descriptive: one archived wave-only fresh game did not fully reproduce with its unchanged old executable, despite identical input hashes. Retained old/new reruns agreed through their common 40000-tick prefix. No controlled win-rate claim is made.

## Verification and reproduction

[Download inputs, logs, observer source and reproduction scripts](evidence.tar.gz). Extract the archive and follow `evidence/README.md`; `manifest.json` records file hashes and `production-plan.json` records all 83 successful jobs and exact input hashes.

- Native combat, lifecycle, diagnostics, implementation integration, economy and director regressions passed. Multi-flag tests cover real construction, force accounting, growth, shrinkage, primary loss, retargeting and reload.
- Python source contracts passed (61 tests, one skip); save-safety and replay-boundary harnesses passed.
- Detailed per-tick continuation matched for 1000 records after a pre-switch reload, 876 after reload with pending streaming flags, and 500 after a post-switch reload. All 2000 baseline state hashes are retained.
- Retained format-115 checkpoint: 512 detailed hashes and 256 subsequent reload records matched. Save floor remains 58; new format is 116. Network protocol 40 rejects older clients; recorded-order replay floor remains 99.

New-behavior cross-platform checksum equivalence remains unverified. The fallback does not override strategic gates before a rally exists or change engine pathfinding. More simultaneous flags and the fallback change Maxima's attack pacing; independent human gameplay review is still needed before merge.
