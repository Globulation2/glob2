# Fresh 10,000-game Elo campaign

This replaces the Symmetric Arena-only rating cohort. It is a new sample: no prior games or preflight games count toward the 10,000.

- 128×128, two distinct AIs per game, all eight AIs.
- 60 compatible generators, each allocated 166 or 168 games.
- All 28 AI matchups, each allocated 356 or 358 games.
- 5,000 map/game-seed blocks, each with two games swapping the AI starting sides. Bootstrap whole blocks.
- Seed 20260920; five candidate map rolls; 90,000-tick cap; existing prestige adjudication at the cap. Probability victory is disabled.
- The same master source `f2cfcfeb05b39cc17eab43a532ebb551172de8de` on both platforms, including the canonical map-saving fix. No new claim of cross-platform per-tick checksum equivalence.
- 36 game slots: localhost 8, devlaptop.local 16, pharaoh-dev-{1,2,3}.local 4 each. Separate immutable build IDs and weighted assignments accommodate platform differences.

The [configuration](comparison.json) is consumed by the canonical `tools.tournaments.experiments.Planner`; [coverage](coverage.json) verifies the complete schedule. [Compatibility](compatibility.json) lists every included and excluded generator and the explicit Rice Terraces `slant=0` override. The editor-only Uniform tool is not a playable generator. All other generator controls retain their defaults.

The six exclusions are Emoji, Gauntlet, Comb, Encircled Kingdom, Faulted City and Bastion Keys: their engine validators require a larger map or more colonies. Excluding incompatible generators was explicitly authorized. They were not replaced by repeated Symmetric Arena games.

[Preflight evidence](preflight.json) records 240 successful short real games: two seeds per included generator on each platform, including generation, map serialization and startup. This is a smoke test, not a claim that every possible seed succeeds or every map is balanced for every AI. Failed games in the full run must be reported and cannot count as completed rating observations.

The final rating exporter requires exactly 10,000 successful unique games, the planned per-generator counts and all 5,000 complete pairs. #364 stays draft until that result replaces its provisional UI scores and passes validation.
