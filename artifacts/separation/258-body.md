Draft, stacked only on #257's independent market-fetching support. This PR no longer includes or depends on #254's gated per-delivery hiring. Its base is `fix/market-round-trip`, so the displayed diff contains only market upgrades.

Markets gain two upgrade levels without renumbering existing buildings: IDs 49–50 stay unchanged and IDs 51–54 append higher-level sites and completed buildings.

| Level | Stored resources | Site cost |
| --- | --- | --- |
| 1 | Three fruits | Wood 4, stone 4 |
| 2 | Fruit, wheat and wood | Wood 6, stone 6, algae 4 |
| 3 | All eight resources | Wood 8, papyrus 4, stone 8, algae 8 |

Only markets whose level accepts a resource can hand out the team's shared stock. The scene-based building panel shows basic-resource stock below the fruit exchange rows. Markets use the ordinary upgrade chain and level-1 sprites.

SIM_REVISION is 17, distinct from the previous stacked builds, with a regenerated golden match record and trace. Save format 134 is inherited from #257; save floor 58, replay floor 127 and protocol 54 remain unchanged. Registered MarketFetch tests cover resource acceptance, stable IDs and stock/type preservation through binary and text saves, plus the preserved worker attachment behavior.

Costs and hit points remain placeholders. Inter-team exchange remains fruit-only, AIs do not build upgrades, and distinct upgrade art and human balance/visual review remain pending. Current-revision validation and platform limitations are recorded in the separation comment. The PR remains draft.
