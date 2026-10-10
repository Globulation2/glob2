# Engine mechanics for AI and content design

Use this reference when deciding whether to expand a colony or upgrade a building.
These rules describe the engine and stock definitions. Imported catalogs can change
services, recipes and training targets; inspect the resolved definitions before
assuming a stock building identity implies a capability.

## Training eligibility and choice

An idle free unit waits more than 32 job ticks before asking its team for training.
`Team::findBestUpgrade` first respects the game's disabled-upgrades rule, then checks
abilities from `WALK` through `NB_ABILITY - 1`. The unit must be able to learn the
ability and the building must pass `Building::canOfferService`.

Eligible buildings are ranked by squared travel distance divided by free inside
capacity, scaled to Q8 and capped at `INT_MAX`. Zero capacity is explicitly skipped.
Air-route and terrain-movement queries can replace the simple toroidal distance.
This is a service/capacity decision, not a check that a legacy upgrade list happens
to contain a building.

Training uses `BuildingTrainingSpec`: the target ability level, admitted unit mask,
duration, material cost and optional worker construction level. `Unit::needsTraining`
checks whether a visit would improve the unit. `Unit::applyTraining` raises levels
when needed; it does not demote a unit already above a target. Parallel service
buildings apply each eligible training recipe on completion; other buildings apply
the selected purpose. Disabled upgrades stop new visits and let restored inside
units leave without gaining a level.

Sources: [activity](../../src/unit/UnitActivity.cpp), [selection](../../src/team/TeamRouting.cpp),
[training](../../src/unit/Unit.cpp), [service completion](../../src/unit/UnitDisplacement.cpp),
and [catalog semantics](../../src/building/types/BuildingCatalog.h).

## Stock training buildings

| Building | Units and service |
| --- | --- |
| Racetrack | Workers and warriors learn walking. |
| Swimming pool | Workers and warriors learn swimming. |
| School | Workers learn building/harvesting; its highest stock tier also trains explorers in ground magic. |
| Barracks | Warriors learn attack speed and strength. |

Stock level-0, level-1 and level-2 buildings train corresponding abilities to levels
1, 2 and 3. The catalog adapter derives these recipes from the built-in tables;
custom definitions need not use `building level + 1` as their target. Building
construction level and unit ability level are distinct concepts.

Sources: [stock definitions](../../src/building/types/BuildingTypesUpgrade.cpp),
[unit performances](../../src/unit/types/Race.cpp) and [adapter](../../src/building/types/Buildings.cpp).

## Upgrade downtime and expansion

Starting an upgrade removes the finished building from service lists and requests
construction workers. The transition waits for occupants to leave, the construction
footprint to clear and the site to receive its required materials. Completion
restores the finished variant and its services. This resource- and footprint-gated
interval has no fixed universal duration.

An inn under construction cannot replace the feeding capacity of the finished inn.
Keep enough other service capacity and supply during the transition. Expanding
first is one possible strategy; whether it is affordable and useful depends on
population pressure, redundant capacity, hauling distance and the resolved catalog.
`maxUnitInside` measures occupancy slots; requested `maxUnitWorking` measures
workers, not trainee throughput.

| Stock inn tier | Inside slots | Food capacity | Feeding duration |
| --- | ---: | ---: | ---: |
| First | 4 | 10 | 24 |
| Second | 7 | 30 | 15 |
| Third | 17 | 50 | 9 |

Slots alone do not predict sustainable throughput: supplies and repeated visits
matter. See [gameplay measurements](gameplay-statistics.md) and [Maxima food capacity](maxima/food.md).

Sources: [construction transitions](../../src/building/Construction.cpp),
[site completion](../../src/building/Update.cpp), [stock inn values](../../src/building/types/BuildingTypesColony.cpp).

## Food protection

Cortex's food-source reconciliation paints a checkerboard of forbidden harvest
cells over reachable food. Workers can harvest the open parity while protected
cells retain stock and seed regrowth. Forbidden areas affect harvest/path gradients;
resource growth does not consult that team mask. The currently configured open
margin is zero, so no rows near a consumer are exempted from the checkerboard.

The pure scan uses fixed neighbor order, `(x + y) & 1` parity and explicit inputs.
The world wrapper derives consumer seeds and region ownership, reconciles added
and removed forbidden cells, and returns brush changes. The action layer translates
those changes into orders. This is controller policy rather than a universal map
guarantee; inspect resolved food resources and region reachability for custom sets.

Sources: [scan and reconciliation](../../src/ai/cortex/CortexFoodSources.cpp),
[tuning](../../src/ai/cortex/CortexConstants.h),
[harvest gradients](../../src/map/gradient/MapGradientMaterial.cpp),
[growth](../../src/map/MapStep.cpp) and [action binding](../../src/ai/cortex/AICortex.cpp).

## Observations and strategy

Use immutable decision inputs and controller-private queries, not live simulation
objects. Cortex observations already include `walkLevel`, `warriorWalkLevel`,
`buildLevel`, attack level distributions, swimming slices and `upgradableCount`.
Check [CortexTypes](../../src/ai/cortex/CortexTypes.h) and its observation builder
before adding a field; earlier expand-versus-upgrade investigation notes predated
several of these additions.

For map economies, growth, travel and sustainable starting supplies, use
[game rules for map design](../map-generators/game-rules-for-map-design.md).
For custom service fields, use [building semantics](../features/building-semantics.md).

Related: [AI documentation](README.md).
