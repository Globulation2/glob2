# Resource properties

Companion to [resource catalogs](resource-catalogs.md).

## Simulation properties

| Field | Meaning |
| --- | --- |
| `primaryMaterial` | One of the deposit's yields; defaults to its lowest material slot |
| `habitatMask` | Bit set: land 1, aquatic 2, shore 4, desert 8; defaults to land |
| `requiresGrowthTerrain` | Placement additionally requires terrain that supports resource growth |
| `requiresPermanentDepositsTerrain` | Placement additionally requires terrain that permits permanent deposits |
| `ecology` | Fertility source: `land`, `shore`, `uniform`, or `none` |
| `growthRate` | Growth opportunities before the selected ecology factor; zero disables natural growth |
| `spreadRate` | Probability of a neighbor-spreading opportunity |
| `stockDependentGrowth`, `stockBranchDivisor` | Select the stock-dependent in-place/spread arbitration; divisor defaults to 8 |
| `blocksGround`, `blocksAir`, `blocksBuilding` | Independent ground movement, flight and building-placement obstruction |
| `clearable`, `clearConsumption` | Whether clearing applies, and whether it removes `one` unit or `all` of the deposit |
| `visibleToHarvest` | Harvesting requires the relevant visibility permission |
| `persistsWhenEmpty` | An exhausted deposit stays present and can regenerate |
| `farmable` | Allows the sustainable farm-area harvesting behavior |
| `smoothPlacement` | Allows generator smoothing to mature and extend author-placed patches |

Rates use exact integer units: **196608 means one opportunity/probability one**.
Growth opportunities are calculated from a completed-tick snapshot and published
eight ticks later by default. Replenishment accepts a matching resource type;
a positive increment can recreate an empty destination with configured initial stocks.
Spread can survive removal of its source. Destinations are revalidated at publication,
and accepted increments preserve intervening harvesting. See
[delayed growth](../architecture/resource-growth.md#delayed-resource-growth).

`growthRate` may be 0–786432; `spreadRate` and yield growth probabilities are
0–196608. This represents wheat's one-third opportunity rate exactly. Runtime
queries use compiled tables and terrain ecology fields, not JSON or floating-point
parsing. Synchronized randomness affects simulation only; artwork choices use a
separate deterministic coordinate hash.

Each material in `yields` has a positive `capacity` up to 65535, `initial` stock
(default 1), `seedReserve` (default 1), `growthRate` (default 196608), and a
`consumption` of `one`, `all` or `infinite`. `all` removes the entire deposit,
including every other yield, while delivering one unit of the requested material.
`destroysDeposit: true` also makes a `one` harvest remove the whole deposit.
Infinite yields cannot destroy their deposit. `placementMaximum` optionally
selects the upper bound for randomized single-yield map placement; zero uses
`initial`. It must not exceed capacity or be lower than initial stock.

Clearing statistics count successful clearing operations, not removed stock units.
Each operation is attributed once to the deposit's configured primary material,
including removal of an empty persistent deposit.

Terrain definitions can specify `allowedResourceKeys` as an explicit stable-key
allowlist, including an empty list to prohibit all resources; `null` selects
capability-based habitats. Old numeric terrain resource masks are converted to
key lists by the compatibility loader. Habitat predicates still apply to explicit
allowlists. Unused material types do not create natural-material gradient work.

AI source fields use a compiled material mutability mask covering every registered
definition, including unplaced ones. Permanent sources avoid periodic queued
refreshes only when harvesting, clearing and configured ecology cannot change
their availability. Explicit source edits and catalog replacement invalidate these
fields; save/load preserves whether a cached field was current or stale.

Shared generator supply checks count positive material stocks, including secondary
yields. Starting guarantees accept equivalent custom sources; bounded crop repairs
replace only clearable surplus deposits. Material frontage measures renewable
supply under harvesting, so full stocks and infinite yields remain sustainable.
Named planting recipes remain choices of the individual generator.

AI seed reservations require a finite material yield with a positive configured
spreading rate under the source's current ecology. Expansion checks use that
specific donor's habitat, including terrain key allowlists. Infinite supplies and
resources that only regrow in place remain harvestable; whole-tile forbidden
areas cannot provide partial-stock reserves. Farm areas handle those reserves
through the configured material `seedReserve`. Non-land ecology does not require
proximity to the land fertility source. Collection pauses for recovery require
actual local regrowth or a compatible neighboring donor, rather than assuming
that every Food deposit behaves like wheat.

Some stable diagnostic interfaces retain historical names: statistics metric IDs
and Cortex CSV/debug columns containing `wheat` describe Food sources or configured
recipe supply, rather than requiring a wheat deposit. Their canonical engine fields
use material and supply names; the old diagnostic labels remain compatibility aliases.

Related: [features and content](README.md).
