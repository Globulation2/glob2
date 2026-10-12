# Markets V2

Markets V2 is an off-by-default gameplay experiment under **Settings → Experiments**.
It combines the market routing and upgrade proposals from PRs #257 and #258.
The setting applies to new games only. Saves and replays retain their original
setting, and multiplayer uses the host's setting. Disable it before starting the
next game to return to ordinary markets; it cannot be switched during a match.
Campaigns and tutorials keep their authored behavior.

## Shared supplies

With the experiment enabled, workers supplying non-market buildings can fetch
resources from their team's stocked markets, including when no natural tile of
that resource remains. Resource routing charges a market pickup an
extra five tiles, so a nearer natural source can still win. Existing worker
attachment between deliveries is preserved.

Markets share a team-wide stock. Delivering a resource to one market makes it
available from every market of that team whose level accepts that resource.
Workers still walk to deposit and withdraw supplies. Markets replenish from
natural resources rather than other markets, preventing stock circulation.
Stock availability and market lifecycle changes invalidate market routing snapshots;
market fields use the existing fixed-tick gradient scheduler and optional pipeline.

| Level | Stored resources | Construction/upgrade cost | HP / armor |
| --- | --- | --- | --- |
| 1 | Three fruits | 4 wood, 4 stone | 400 / 6 |
| 2 | Fruits, wheat, wood | 6 wood, 6 stone, 4 algae | 600 / 8 |
| 3 | All eight resources | 8 wood, 4 papyrus, 8 stone, 8 algae | 800 / 10 |

Each accepted resource has a capacity of 200 in the shared stock, rather than
200 per market. The existing fruit-only inter-team exchange code is preserved. Its controls are
currently disabled in the market panel; Markets V2 does not repair or enable them. Normal upgrade
requirements and custom-game restrictions still apply. Costs, HP and armor are
provisional; all levels reuse the original artwork. Level 2 currently finishes
with 560 of its 600 HP and needs repair before another upgrade, preserving the
proposal's HP-per-resource balance. Existing AIs can use the
routing behavior but have no new strategy for building market upgrades.

## Compatibility and testing

With Markets V2 disabled, markets retain the original fruit selection and delivery
paths. Higher levels are unavailable, and market-specific fields are neither
allocated nor scheduled. Static building IDs 49–50 are preserved; levels 2 and 3
append IDs 51–54. Script observations omit unavailable types.

Format 135 persists market fields, refresh flags and pending publications without
renumbering older pending-gradient destinations. Earlier saves keep their original
behavior. Binary and text continuation tests cover the new state. Text saves also retain
qualified statistics field names used by existing games. Saved upgraded
markets or market routing state require the experiment in the game header; a file
with inconsistent state is refused rather than silently changing its rules.

Run `python3 test/run_tests.py --filter 'MarketFetch/*' --filter 'MarketsV2/*'`.
The disabled-path golden trace is generated from the pre-integration master and
covers real fruit-market trips and fixed-delay routing. Enabled tests cover
resource eligibility, pickup, depletion, upgrades and continuation. For playtesting,
compare the same map and seed with `--experiment markets-v2` enabled and disabled;
look for supply throughput, worker travel, starvation, and gradient CPU/memory cost.
Automated checks do not establish balanced costs or replace human playtesting.

Related: [features and content](README.md).
