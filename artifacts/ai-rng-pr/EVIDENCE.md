# AI random stream validation evidence

Source commit: `4d6ca13c7` on `codex/ai-random-streams`. Platform: macOS.

Two runs from `maps/balanced_for_2.map`, seed 123, Cortex vs Nicowar, 2,000 ticks:

- `run-a.replay.gz`, `run-b.replay.gz`: complete game replays.
- `run-a.checksums.gz`, `run-b.checksums.gz`: per-tick telemetry/checksum sidecars.
- `run-a/initial.game.gz`, `run-a/final.game.gz`: save continuity fixtures.
- `run-a/result.json`, `run-b/result.json`: headless run summaries.

The repeated runs produced 96 orders each, final checksum `3a679efc`, identical replay SHA-256 `7ba9752d3296e33df28b3119a6188c333e038f161ab7ee675b57d71b2e2ec297`, and identical per-tick sidecar SHA-256 `a00789e95d14701a0ca286448b616139a6d583831078c7d4509713a42c308da4`.

Loading `run-a/initial.game.gz` and running to tick 2,000 reproduced every team/entity telemetry record. The aggregate checksum differs after save/load because `MapHeader::checkSum` includes `versionMinor`, which changes on save/load. The independent `AISavePortabilityHarness` and replay/network gate `TeamStatsSaveHarness` passed. An older save also loaded and ran 10 ticks. Cross-platform execution has not been verified.
