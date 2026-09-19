# Final detached-island review

Frozen v9 executable; 512×512, appetite 2, map seed 11, game seed 1, four Nicowar players, 40,000 ticks. The parent verified this map is byte-identical under the final executable.

**The isolated-colony pool trap is resolved in this representative run.** Team 0 begins alone on the western detached island. At tick 40,000 it has 269 units and 30 completed buildings. Its two completed level-2 pools are at **(58,253)** and **(65,255)**, on that same western island as its original swarm **(57,232)** and four additional swarms. These are not inaccessible mainland pools. Final statistics contain **15 swimming workers**.

The last full gameplay-counter sample in stdout is tick **38,400** (the saved game and result are confirmed at **40,000**). At that sample team 0 had **158 worker births, 3,287 wheat and 458 wood harvested, 99 worker and 146 warrior swim ability gains**. Worker and warrior starvation deaths were both **zero**. Explorer starvation was **82**, so this is not a claim of zero starvation across every unit type. Combat contact is demonstrated by **7 worker and 1 warrior combat deaths**, **8,568 melee damage dealt to units**, and **114 melee damage dealt to buildings**. These cumulative counters are explicitly not presented as exact tick-40,000 totals.

**Late visual verdict:** the final preview retains the recognizable rounded bites, pond clearings and wooded interior. Coastal farms have expanded and the home island is developed, but the island silhouette remains legible; there is no visually overwhelming forest takeover. The native preview omits building sprites, so exact pool location and completion were checked directly against the saved building records, not inferred from the picture. Appearance remains approximately 8/10 at defaults and 7/10 at high appetite.

**Code review:** the detached-component capacity and spaced-footprint filter implement the intended generator-only remedy. The final any-corner sand mask matches Nicowar's placement rule. Hoisted sine/cosine preserves AxisFrame projection arithmetic. The early return when every secondary island has less than 1,200 grass tiles preserves zero secondary capacities, the mainland allowance, and a safely sized zero service mask. No remaining blocker found in this review scope. The global service-space count is a capacity screen, not a formal guarantee of every future AI placement; broad human fun/balance remain outside this one-game result.

Artifacts:
- `512-a2-s11.map`, `512-a2-s11.json`, `512-a2-s11.png`: initial world and report.
- `play-512-a2-s11/result.json`, `play-512-a2-s11/final.game.gz`: 40,000-tick outcome and save.
- `play-512-a2-s11.log.gz`, `team0-final-counters.json`: logged telemetry (counter snapshot at 38,400).
- `team0-buildings.json`, `inspect_team0.py`: validated 180-byte Building::save records; every used slot/gid checked.
- `final.png`, `final.json`: saved-world preview and report at 40,000.

Command: `SDL_VIDEODRIVER=dummy artifacts/who-ate-the-map/v9/glob2 --run-game --map-file /Users/bradley/glob2-pr-help-4/artifacts/who-ate-the-map/review6/512-a2-s11.map --game-seed 1 --player nicowar --player nicowar --player nicowar --player nicowar --ticks 40000 --telemetry team-timeline --save final --output-dir /Users/bradley/glob2-pr-help-4/artifacts/who-ate-the-map/review6/play-512-a2-s11`
