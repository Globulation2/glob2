# JavaScript API reference

The sections below describe the preserved profile 1 contract;
[custom AI profile 2](#custom-ai-profile-2) extends it with native services.
Embedded sources execute automatically and must be trusted developer code. Start with the
[scripting guide](javascript.md) for loading, callbacks, persistence and failures.
The [TypeScript declarations](../../examples/javascript/glob2.d.ts) mirror these
shapes for editors; the runtime executes JavaScript, not TypeScript. Only the
fields and methods listed here are exposed. There are no engine pointers, live
entity objects, pathfinding queries or direct mutation methods.

## Conventions

- Coordinates and dimensions are in map tiles. X increases rightward, Y downward.
  Arrays and IDs are zero-based. Times are simulation ticks unless stated otherwise;
  performance and resource counters retain engine units, not seconds or percentages.
- Integer arguments require finite integer **numbers** in the specified range;
  strings and booleans are not coerced. Boolean and string fields require their
  actual JavaScript types. Required fields have no implicit defaults.
- Read records have a null prototype and are detached copies. Use
  `Object.keys(record)` or `Object.prototype.hasOwnProperty.call(record, key)`;
  `record.hasOwnProperty()` is unavailable. Mutating a record does not change the
  game. Nested arrays are ordinary arrays.
- An absent optional field reads as `undefined`. It is different from a present
  field containing `null`, `false`, zero or an empty array. Do not save an absent
  field as `undefined` in returned data: omit it or use `null` explicitly. Global variables can contain undefined.
- A hidden, dead, missing or stale entity lookup returns `null`. Malformed query
  arguments throw a JavaScript `TypeError`. Unsupported scenario queries also
  throw in AI scripts. Budget and failure behavior is described in the guide.
- Extra descriptor fields are ignored by order/effect validators, but still must
  be serializable script data and consume the conversion budget. Use the signatures
  below; unused arguments are not a versioning or extension mechanism.

## Context

| Member | Meaning |
| --- | --- |
| `ctx.tick` | Current simulation step counter; not the callback count |
| `ctx.myTeam` | AI team ID; `-1` for map scripts |
| `ctx.random()` | Next private deterministic random number in `[0, 1)` |
| `ctx.game` | Read API described below |

`Math.random()` consumes the same stream as `ctx.random()`. Other scripts and the
simulation RNG have separate streams. Changing `ctx.myTeam` or another context
property does not change the host's visibility or order permissions.

## Teams and entity queries

```javascript
ctx.game.teams()
ctx.game.units({team, offset, limit})
ctx.game.buildings({team, offset, limit})
ctx.game.unit({id, generation})
ctx.game.building({id, generation})
ctx.game.buildingTypes()
ctx.game.experiments()
ctx.game.terrainTypes()
ctx.game.resourceTypes()
ctx.game.materialTypes()
ctx.game.rules()
```

`rules()` returns a detached read-only snapshot of effective match settings in
both AI API profiles. Keys and numeric ranges match [headless custom rules](headless-replays.md#glob2_test_rules):
`noGrowth`, `scarcity`, `instantConstruction`, `stockpile`, `noHunger`,
`noUpgrades`, `glassCannon`, `fearless`, `noPermadeath`, `peaceful`, `fortress`,
`suddenDeathTick` and `winProbabilityPermille`. Experiments remain available through
`experiments()`. Use these settings when selecting plans and prerequisites.
`noUpgrades` disables both unit training and building upgrades, while repairs,
healing, production and starting levels remain available. The engine rejects
healthy-building upgrades even when a script does not adapt its strategy. The
returned values cannot be changed by a script; requesting another snapshot reads
the authoritative header again. No rule fields are added to serialized AI state.

Read capabilities before allocating workers or waiting for prerequisites. For
example, `ctx.game.rules().noUpgrades` should prevent a controller from requesting
training-only buildings and from waiting for newly trained workers. It must not
disable hospitals, swarm wheat deliveries or all uses of an existing barracks:
healing and production remain available. The shipped
[`ai.js`](../../examples/javascript/ai.js) demonstrates rule-aware staffing;
arbitrary user scripts retain responsibility for their own strategic decisions.

The filter record and all its fields are optional. `team` is a current team ID;
`offset` and `limit` are integers in `0..32768`, defaulting to `0` and `32768`.
Omitting `team` includes all permitted teams; use `ctx.myTeam` to select your own.
Passing `null` for a filter field also uses its default. Omit the field instead
of passing `undefined`, which is not supported boundary data.

Lists have stable ascending team/slot order. Offset skips **visible results**,
not hidden slots; a zero limit returns an empty array. Each list call has a
fixed work charge as well as charges for its returned records. Read small pages
and distribute large scans across callbacks using persistent top-level variables.

```javascript
const page = ctx.game.buildings({team: ctx.myTeam, offset: 0, limit: 50});
const candidate = page.find(b => !b.virtual);
if (candidate) {
  const ref = {id: candidate.id, generation: candidate.generation};
  const current = ctx.game.building(ref); // Building or null
}
```

IDs identify native engine slots, with separate unit and building namespaces.
An `EntityRef` is `{id: number, generation: number}`; list records already contain
both fields and can be passed directly as references. IDs are accepted in
`0..65535`; IDs outside the entity slot domain return `null`. Generations change
for each new entity, slot reuse and conversion into the destination team/slot.
Converting back cannot restore an earlier reference. Level resets, upgrades,
repairs and ordinary state changes preserve identity. Deletion invalidates lookup.
Save format 125 and later preserve and validate live identities and generation
counters; released formats 58–124 receive identities during version-gated loading.
Format 124's experiment header remains compatible. Preserve the returned pair;
never invent a generation or
assume an ID still identifies the same entity after loading or a later callback.
A missing or nonnumeric generation returns `null`; a missing/invalid ID throws.

### Visibility

| Data | AI script | Map script |
| --- | --- | --- |
| Team ID and alive status | Every team | Every team |
| Team resources/allies | Own team only | Every team |
| Owned units/buildings | Full exposed record | Full exposed record |
| Other units | Visible outside buildings; basic fields only | Full exposed record |
| Other buildings | Visible at their anchor tile and not cloaked; basic fields only | Full exposed record |
| Objectives, hints, interface | Query throws | Available |

Allied teams also use the other-team restrictions; alliance does not grant full
records. A moving enemy unit is visible when its current or previous tile is
visible. Unit/building lists omit dead entities. These are ordinary information
hiding rules, not protection against statistical inference from gameplay.

### Team record

| Field | Meaning |
| --- | --- |
| `id`, `alive` | Team ID and boolean alive status |
| `allies?` | Bitmask of allied teams; bit `t` corresponds to team `t` |
| `resources?` | 15-entry team stock array, indexed by resource ID |

### Unit record

Basic fields appear for every returned unit. The remaining fields appear only
for owned units or map scripts.

| Basic field | Meaning |
| --- | --- |
| `id`, `generation`, `team` | Reference and owner team |
| `type` | Worker `0`, explorer `1`, warrior `2` |
| `x`, `y` | Current map tile; an owned/map-visible unit inside a building still has coordinates |
| `hp`, `maxHp` | Current health and `performance[16]` maximum health |
| `levels`, `performance` | 17-entry arrays indexed by ability (see numeric values below) |

| Owned/map field | Meaning |
| --- | --- |
| `experience`, `experienceLevel` | Engine experience counter and derived experience level |
| `hunger` | Remaining food counter; decreases as hunger advances, not a percentage |
| `fruitCount`, `fruitMask` | Fruit count and resource-ID bitmask of fruit kinds |
| `activity`, `movement`, `medical` | Numeric engine states, listed below |
| `action` | Ability ID for the current action |
| `carriedResource` | Resource ID, or `-1` when carrying nothing |
| `speed` | Engine advancement units per tick; tile advancement wraps at 256 |
| `direction` | Compass code `0..7`, or `8` for no direction |
| `insideTimeout` | Negative inside a building; nonnegative outside |
| `destinationPurpose` | Engine purpose: resource ID when fetching, ability ID when training/healing/feeding; `-1` for none. Interpret with activity, not as one unified enum |
| `targetX`, `targetY` | Stored target-line coordinates; validity is not exposed, so these alone do not establish an active target |
| `attachedBuilding`, `targetBuilding` | Visible building reference or `null` |

### Building record

| Basic field | Meaning |
| --- | --- |
| `id`, `generation`, `team` | Reference and owner team |
| `type` | Building variant ID; matches `buildingTypes().id` |
| `shortType` | Building family ID, listed below |
| `x`, `y` | Anchor tile, not center; width/height come from its type |
| `hp`, `maxHp` | Current health and effective maximum health |
| `level`, `virtual` | Variant level and whether this is a flag |
| `construction` | Result state: none `0`, new building `1`, upgrade `2`, repair `3` |

The following appear only for owned buildings or map scripts. Fields are present
for all such buildings even where their meaning applies only to a particular
family; their presence does not imply that an order is supported.

| Owned/map field | Meaning |
| --- | --- |
| `workers` | Current requested worker limit, **not** number of workers actually present |
| `futureWorkers` | Requested worker limit after construction completes |
| `priority` | Low `-1`, normal `0`, high `1` |
| `range`, `minimumLevel` | Attraction radius in tiles and minimum warrior combat level |
| `requireBombing` | Whether attracted explorers must have the bombing ability |
| `workerMinimumLevel` | Minimum construction qualification for attracted workers |
| `resources` | 15-entry stock array; some types use shared team stock |
| `wishedResources` | 15-entry engine resource demand array |
| `production` | Three swarm ratios, indexed worker/explorer/warrior; relative weights, not percentages |
| `productionTimeout` | Swarm production countdown in ticks |
| `receiveMask`, `sendMask` | Market resource-ID bitmasks |
| `bullets` | Stored tower ammunition |
| `clearingResources` | Five booleans indexed wood/wheat/papyrus/stone/alga |

### Building type record

`buildingTypes()` returns every registered variant in registry order. It is
static configuration available to both script capabilities. An optional `{offset, limit}`
argument pages by catalog ID, allowing large catalogs to fit callback budgets.
Unavailable experimental variants are omitted; `offset` still refers to raw IDs.

| Field | Meaning |
| --- | --- |
| `id`, `key`, `name`, `shortType` | Match-local variant ID, stable authored key, authored family name, legacy numeric family metadata |
| `capabilities` | Available semantic operations, such as `feed`, `heal`, `produceWorker`, `trainConstruction`, `trainBombing`, `projectileDefense`, or `attractWarriors`; construction sites describe their completed variant |
| `nextType`, `previousType` | Explicit transition IDs, or -1; never infer adjacency from IDs |
| `placeable`, `instantPlacement`, `occupiesGround`, `relocatable` | Independent placement and relocation properties |
| `requiredWorkerLevel`, `admittedUnitMask`, `maxUnitsInside`, `maxRadius` | Construction qualification, admission bitmask, interior capacity, attraction radius limit |
| `feeding`, `healing` | Records with `enabled`, `unitMask`, `duration`, and 15-resource `cost` |
| `training` | Ability-indexed records: `enabled`, `unitMask`, `duration`, `targetLevel`, independent `constructionLevel` grant (-1 means none), and `cost` |
| `production` | Unit-indexed recipe records: `enabled`, `duration`, and `cost` |
| `repairable`, `regeneration` | Repair support and health regeneration per tick |
| `projectileDamage`, `projectileRange`, `projectileSpeed`, `projectileRhythm`, `ammunitionResource`, `ammunitionCost` | Damage by target unit type, firing parameters, and ammunition input |
| `suppliesStock`, `suppliesDirectStock`, `fetchesStock`, `exchangesFruit` | Effective resource-routing and exchange capabilities |
| `level`, `site`, `virtual` | Variant level, construction-site boolean, flag boolean |
| `width`, `height` | Footprint in tiles |
| `maxHp`, `maxWorkers`, `usesWorkers` | Configured health, assignment limit, and whether the building requests hauling/construction labor |
| `resourceCapacity` | 15-entry configured resource capacity/cost array |

Discover variants by capabilities and follow explicit transition IDs. Names and legacy family numbers remain available for authored scenarios, but do not imply behavior:

```javascript
const site = ctx.game.buildingTypes().find(
  t => t.placeable && t.capabilities.includes('feed')
);
```

### Experiments

`experiments()` returns the keys of the
[experimental features](../features/experimental-features.md) this game carries,
as an array of strings in a fixed order, for example `["farm-areas"]`. It is empty
in a default game. The set is fixed for the game's whole life, so a script can read
it once. Both script capabilities may call it.

```javascript
const farms = ctx.game.experiments().includes('farm-areas');
```

## Map queries

```javascript
ctx.game.map.width
ctx.game.map.height
ctx.game.map.tile(x, y)
ctx.game.map.region(x, y, width, height)
```

Query X/Y must be integers in `-32768..32767` and wrap around the toroidal map;
returned coordinates are canonical `0..map.width-1` / `0..map.height-1`.
Region dimensions are integers in `0..256`; either zero returns an empty array.
Region requires exactly four arguments and returns a flat row-major array:
`tiles[dy * width + dx]` corresponds to wrapped `(x + dx, y + dy)`.
Maximum dimensions are validation limits, not promises that a region fits the
callback budget. For example, a 256×256 request exceeds the work budget.

| Tile field | Presence and meaning |
| --- | --- |
| `x`, `y`, `visible`, `explored` | Always present; `visible` means current permission to see the tile |
| `observedTick`, `terrain`, `terrainType`, `resource` | Present only when explored; current data if visible, last observation otherwise |
| `resource.type`, `.variety`, `.amount` | Map-local resource ID, visual variety and total stock; no resource is `{type: 65535, variety: 0, amount: 0}` |
| `materialStocks` | Twelve quantities indexed by fixed material ID; current stocks if visible, remembered stocks otherwise |
| `groundUnit`, `airUnit`, `building` | Present only when currently visible; permitted occupant ID or `65535` for empty/hidden occupant |
| `fertility` | Map scripts only; raw `0..65535` wheat-growth fertility value |
| `forbidden` | AI scripts only, on explored tiles; current own-team forbidden-area boolean, even if terrain is remembered |
| `farmArea` | AI scripts only, on explored tiles, in a game with the `farm-areas` experiment; `true` where the own team painted a [farm area](../features/farm-areas.md), absent otherwise |

An unexplored AI tile is exactly `{x, y, visible: false, explored: false}`.
A remembered tile has no occupant fields. Map-script tiles are always visible
and explored. AI exploration is the controller's recorded observation history,
not a request to reveal the engine's hidden current map. The history survives
save/load and is updated during simulation independently of which tiles the
script asks for.

**`terrain` is the raw terrain graphic index, retained for compatibility.** Use
`terrainType` to index the immutable definitions returned by
`ctx.game.terrainTypes()`. Both tile fields follow the same visibility and
remembered-observation rules. Unexplored tiles do not expose either field.
The registry is static public metadata and does not reveal map contents.

`resourceTypes()` and `materialTypes()` also return immutable public metadata in
both profiles, including commander AIs. Resource descriptors expose `id`, `key`,
`name`, `yields`, mobility and placement obstruction, habitat and ecology fields,
growth/spread rates, farming/clearing behavior and experiment requirements. Each
yield names a numeric `material`, capacity, initial stock, seed reserve, growth
probability and consumption policy (`0` one, `1` entire deposit, `2` infinite).
Rates use 196608 units per opportunity. Material descriptors have fixed `id` and
canonical `key`; see [resource catalogs](../features/resource-catalogs.md).

Unit `carriedMaterial`, building `materials` and `wishedMaterials`, and team
`materials` distinguish inventory from map deposits. Historical `carriedResource`,
`resources` and `wishedResources` names remain legacy script aliases; their numeric
inventory positions mean materials, not resource IDs.

```js
const definitions = ctx.game.terrainTypes();
const tile = ctx.game.map.tile(x, y);
if (tile.explored) {
  const terrain = definitions[tile.terrainType];
  const groundSpeedMultiplier = terrain.groundSpeedQ8 / 256;
  // Buildability is a terrain capability; occupancy and space still matter.
  if (terrain.buildable) { /* consider a placement query */ }
}
```

Each entry has `id`, stable `name`, `experiment` (a required experiment key or
`null`), `editorSelectable`, and these gameplay properties:

| Fields | Meaning |
| --- | --- |
| `walkable`, `swimmable`, `flyable` | Terrain movement permissions; swimmers may also walk |
| `resourcesGrow`, `fertilitySource`, `nonGrowingResources` | Resource growth, nearby fertility contribution, and placement of non-growing resources |
| `buildable`, `projectileBlocks`, `shoreline` | Building placement capability, projectile obstruction, and shoreline classification |
| `groundSpeedQ8`, `airSpeedQ8`, `growthQ8` | Multipliers: `256` is normal, `128` half, `512` double |
| `groundHealthQ8`, `airHealthQ8` | Signed HP per exposed tick, divided by `256`; negative damages |
| `fertilityQ8`, `inhibitionQ8`, `shoreSupportQ8` | Nearby contribution, inhibition, and aquatic shoreline support in Q8 units |
| `allowedResources` | Array of resource IDs that the terrain supports |
| `farmMaterial` | Preferred renewable farming material key, or `null` for none |

The array is ID-indexed and includes internal shoreline profiles and experimental
materials even when the current match has not enabled their authoring options.
All nested registry values are read-only in both scripting profiles, including
commander and map scripts. Existing IDs remain water `0`, sand `1`, grass `2`,
ice `3`, Trail `4` (legacy registry name `road`), grass/sand shore `5`, and sand/water shore `6`; scripts should
query capabilities instead of comparing those IDs or graphic frame ranges.
`ctx.spatial.passable` additionally checks known occupancy and movement rules;
spatial placement and connectivity use the same canonical terrain properties.

Tile occupants are bare IDs, not complete references. To read an occupant,
match its ID against the appropriate visible entity list and use that record's
reference. `game.unit({id: tile.groundUnit})` returns `null` because it lacks a
generation. Empty occupants use `65535`, **not** `null` or zero.

```javascript
let wheatTiles = [];
function step(ctx) {
  const tiles = ctx.game.map.region(10, 20, 8, 6);
  const wheat = tiles.filter(t => t.visible && t.resource.type === 1);
  // A resource may be eternal with amount zero: test type !== 255 for presence.
  wheatTiles = wheat.map(t => ({x: t.x, y: t.y, amount: t.resource.amount}));
}
```

## Numeric values

These values describe profile 1's engine encodings. They are not injected
JavaScript globals. Define local constants in your single module when useful:

```javascript
const RESOURCE = Object.freeze({WOOD: 0, WHEAT: 1, STONE: 3, NONE: 255});
const UNIT = Object.freeze({WORKER: 0, EXPLORER: 1, WARRIOR: 2});
const NO_ENTITY = 65535;
```

| Domain | Values |
| --- | --- |
| Resource IDs / array indices | Wood `0`, wheat `1`, papyrus `2`, stone `3`, alga `4`, cherry `5`, orange `6`, prune `7`; array positions `8..14` are reserved |
| Resource sentinels | Tile no-resource `255`; unit not-carrying `-1` |
| Building families (`shortType`) | Swarm `0`, inn `1`, hospital `2`, racetrack `3`, swimmingpool `4`, barracks `5`, school `6`, defencetower `7`, explorationflag `8`, warflag `9`, clearingflag `10`, stonewall `11`, market `12` |
| Ability array indices / actions | Stop walk `0`, stop swim `1`, stop fly `2`, walk `3`, swim `4`, fly `5`, build `6`, harvest `7`, attack speed `8`, attack strength `9`, magic attack air `10`, magic attack ground `11`, magic create wood `12`, magic create wheat `13`, magic create alga `14`, armor `15`, health `16` |
| Additional destination purposes | Heal `17`, feed `18`; these are not indices in the 17-entry ability arrays |
| Activity | Random `0`, filling `1`, flag `2`, upgrading `3` |
| Medical | Free `0`, hungry `1`, damaged `2` |
| Movement | Random ground `0`, random fly `1`, going target `2`, flying target `3`, going dx/dy `4`, harvesting `5`, filling `6`, entering building `7`, inside `8`, exiting building `9`, attacking target `11`; `10` is unused |
| Direction | NW `0`, N `1`, NE `2`, E `3`, SE `4`, S `5`, SW `6`, W `7`, none `8` |
| Objective type | Primary `0`, secondary `1`, invalid `2` |
| GUI element ID | Construction panel `0`, flags panel `1`, statistics text `2`, statistics graph `3`; `4` is accepted by the boundary but reserved (engine view-count sentinel) |

Unit levels normally use `0..3`. Performance values have ability-specific engine
units: movement/build/harvest/attack speed are advancement rates, attack strength
and armor are combat values, health is the HP limit. They are not normalized.
For resource/team masks use `(mask & (1 << id)) !== 0`.

## AI order descriptors

Return one descriptor from `step`, or `null`/`undefined` for no order. There are
no imperative `issueOrder` methods and AI scripts cannot return a batch. All
building-targeted orders require an owned building reference with a current
generation. A returned entity record is acceptable as that reference.

| `type` | Required fields / validation |
| --- | --- |
| `create` | `buildingType`: registered variant ID; `x`, `y`: canonical in-map coordinates; `workers`, `futureWorkers`: `0..20`; virtual flags also require `range`: `0..255` |
| `workers` | `building`, `workers`: `0..20` |
| `delete`, `cancelDelete` | `building` |
| `construction` | `building`, `workers`, `futureWorkers`: `0..20` |
| `cancelConstruction` | `building`, `workers`: `0..20`; cannot cancel an initial site construction this way (use `delete`) |
| `priority` | `building`, `priority`: `-1..1` |
| `production` | Swarm `building`, `ratios`: exactly three integers `0..16`, in worker/explorer/warrior order |
| `exchange` | Market `building`, `receiveMask`, `sendMask`: integers `0..32767` |
| `range` | Virtual `building`, `range`: `0..255` |
| `minimumLevel` | Warrior-attracting `building`, `level`: `0..3` |
| `requireBombing` | Explorer-attracting `building`, `requireBombing`: boolean |
| `workerMinimumLevel` | Worker-attracting `building`, `workerMinimumLevel`: `0..3` |
| `moveFlag` | Virtual `building`, canonical `x`, `y` |
| `clearingResources` | Clearing-flag `building`, `resources`: exactly five booleans; index `3` (stone) must be false |
| `forbidden`, `guardArea`, `clearArea`, `farmArea` | Canonical `x`, `y`; `width`, `height`: `1..256`; `mode`: add `1` or remove `2`; `mask`: exactly `width * height` booleans in row-major order |

Creation accepts level-zero construction sites or virtual flags. The engine
still decides whether an accepted order can execute: placement, occupancy,
construction conditions and scenario restrictions remain gameplay checks. An
accepted descriptor is not a promise that the requested building will appear.
Order coordinates **do not** use the read API's signed/wrapped input convention.
Area-mask `(0,0)` is anchored at `(x,y)` and the engine wraps affected tiles.
`false` mask entries leave tiles unchanged; mode adds/removes the selected area
on true entries. Guard/clear zones are not exposed by the tile read API yet.
`farmArea` is accepted only in a game with the `farm-areas` experiment
(`ctx.game.experiments()`); elsewhere validation rejects it as `not_permitted`.
The engine refuses farm paint on ground nothing can grow on, so check the tile
`farmArea` field afterwards rather than assuming every masked tile took.

## Map-script records and effects

`ctx.game.objectives()` returns `{id, scriptNumber, text, type, visible, complete,
failed}` records. `ctx.game.hints()` returns `{id, scriptNumber, text, visible}`.
Completing clears failure; marking incomplete clears both completion and failure;
marking failed clears completion. Visibility is independent of completion/failure.
Both use list order and zero-based `id`; `scriptNumber` is the separate editor/
legacy-script label, commonly `1..16`, and is **not** the effect's `id`.

`ctx.game.interface()` returns the persisted presentation overrides:

```typescript
{
  message?: string | null;
  translations?: {[language: string]: string};
  buildings?: {[name: string]: boolean};
  flags?: {[name: string]: boolean};
  elements?: {[numericId: string]: boolean};
}
```

Initially this is `{}`. Missing choice/element overrides mean enabled; absent or
null `message` means no base message. This describes authoritative script state,
not local window controls or user preferences. The keys of `elements` are strings
such as `"0"`. Queries during a callback see committed state, not effects that
callback will return.

Return an array of at most 256 effects, or `null`/`undefined` for no effects.
The full batch is validated before anything is applied. Effects apply in array
order; for repeated changes, the later effect wins.

| `type` | Required fields / behavior |
| --- | --- |
| `message` | `text`: string; replaces base message and clears translations |
| `messageTranslated` | `language`, `text`: strings; replaces the translation for that exact language key |
| `hideMessage` | Clears base message and translations |
| `objective` | `id`: existing zero-based objective index; `action`: `complete`, `incomplete`, `failed`, `hidden` or `visible` |
| `hint` | `id`: existing zero-based hint index; `visible`: boolean |
| `buildingChoice`, `flagChoice` | `name`: supported family name; `enabled`: boolean |
| `guiElement` | `id`: `0..4`; `enabled`: boolean; prefer the implemented panels `0..3` |

Building-choice names: `swarm`, `inn`, `hospital`, `racetrack`, `swimmingpool`,
`barracks`, `school`, `defencetower`, `stonewall`, `market`. Flag-choice names:
`explorationflag`, `warflag`, `clearingflag`. Disabling a choice also restricts
engine creation orders, including orders from other controllers. GUI visibility
and translated-message display still follow the existing GUI/replay behavior;
the script's persisted presentation state does not depend on local settings.
The GUI resolves the exact language translation before comparing the displayed
message. History receives a message only when that displayed text changes to a
nonempty message; unchanged callbacks do not repeat it. Loading restores the
persisted message, choices and hidden elements without running a callback or
publishing a duplicate history entry. Leaving the session clears its presentation.
There are no spawn, terrain-write, objective-create or gameplay-order effects.

## Maintaining the boundary

Profile 1 is stored in AI source configuration (`glob2-js/1\n`) and saved runtime
state; unsupported stored profiles are rejected. `ctx` has no runtime API-version
property and there is no negotiation mechanism. Save format and network protocol
versions are separate engine compatibility gates: current saves use format 125
with minimum 58, the network protocol is 48, and replay acceptance starts at 123.
See the [guide](javascript.md) for released-format loading behavior.

When changing the exposed contract, update this reference, the declarations,
examples and regression coverage together. Changes to numeric behavior,
serialization, budgets, visibility or callback execution can change deterministic
results: decide the profile/compatibility impact explicitly and compare the
[frozen fixture](../../test/fixtures/javascript/README.md) across supported
platforms. Build success alone is not proof of equivalent execution.

## Custom AI profile 2

Profile 1 remains supported with its original execution and snapshot contract.
A custom AI opts into profile 2 with `metadata()` returning `{apiVersion: 2,
name: "My AI"}`. Optional string fields are `description`, `version`, and `author`.
Named exports, including renamed export bindings produced by a bundler, and
standalone `function step()` / `function metadata()` declarations are supported.
Metadata runs in a separate restricted runtime without randomness or game data.
Use `glob2 --check-ai bundle.js` to check startup, metadata, callback resolution,
and initial global serialization. `--check-script` remains compile-only.

[Profile 2 declarations](../../examples/javascript/glob2-v2.d.ts) extend the read
API above. All imported code must be bundled into one ES module; runtime imports
remain unavailable. The external [example repository](https://github.com/Globulation2/glob2-javascript-ai-starter-exampler)
contains modular authoring sources and the pinned, atomic esbuild build/watch wrapper.

### Managed properties and actions

```javascript
export function metadata() { return {apiVersion: 2, name: "My AI"}; }
export function step(ctx) {
  const buildings = ctx.game.buildings({team: ctx.myTeam, limit: 32});
  for (const b of buildings) {
    if (b.workers !== 3) b.workers = 3;
  }
  ctx.telemetry.set("colony.buildings", buildings.length);
}
```

Owned building records have native getters and setters. Repeated lookups return
the same object within a callback. Writable properties are `workers`, `priority`,
`production` for enabled unit recipes, `receiveMask` and `sendMask` for inter-team exchange,
`x` and `y` for relocatable buildings, and `range` for attraction providers. Warrior
attractors support `minimumLevel`; explorer attractors support `requireBombing`;
worker attractors support `clearingResources` and `workerMinimumLevel`. These controls apply to mixed
buildings according to their capabilities. Other fields and enemy
records are read-only. Assign complete arrays for production and clearing settings.

Getters show pending desired values; `building.observed` shows simulation values.
Managed objects and field handles are callback-local: persist `building.ref`
and reacquire with `ctx.game.building(ref)`. Never store managed objects or spatial
fields in module globals. Plain module variables retain automatic persistence.
Persistent closures, class instances, runtime-created functions, external imports,
I/O, and clocks remain unsupported. Callbacks return nothing in profile 2.

`ctx.actions` offers `create`, `build`, `upgrade`, `repair`, `delete`, `zone`,
`status`, and `cancel`; exact descriptor shapes are in the declarations.
Actions return controller-local IDs. Creation IDs track pending, issued,
constructing, completed, failed, or cancelled states. Ordinary gameplay rejection
and stale queued targets become failed statuses. Malformed operations throw.
Cancelling affects only commands still pending. Completed history is bounded;
`status` returns null when a record is no longer retained.

Changes are staged until the callback and global snapshot succeed. Repeated
property writes coalesce; replacing a pending edit retains its queue position.
Flag coordinates form one move. One ordinary order is dispatched per AI poll;
queued actions and construction tracking survive saves. Queue limits are 256
pending operations, 256 newly staged operations per callback, and 1,024 history
records. The combined action and telemetry state must fit the serialization limit,
including reserved space for construction tracking. Oversized callbacks fail before
any staged state is committed. A script failure disables the controller and preserves the previous
committed globals and RNG.

### Synchronous spatial services

`ctx.spatial` computes native fields lazily from the controller's observations.
`distance`, `displacement`, `footprintDistance`, and `overlap` wrap across map
seams. Coordinates use tiles; footprint distance is the gap between rectangles.
`summary` returns observed fertility, resource amounts, and known/visible tile
counts for a wrapped region. Fertility is remembered under fog in profile 2.

`distanceField({sources, movement, metric})` builds a reusable callback-local
field. Sources select points, resources, materials, permitted units, and buildings.
Use `{material: "food"}` (or a numeric material ID) to include all deposits carrying
food, independent of their resource identity. `weight: "amount"` uses that
material's stock; `harvestable: true` applies forbidden-area and visibility rules.
Hidden cells use remembered stocks and never reveal current hidden inventory.
Building filters
accept `capability` (one operation from `buildingTypes().capabilities`), `buildingType`
(an exact match-local variant ID), or `type` (an authored name/key or a legacy numeric
family). Capability filtering includes construction sites for their completed service.
Movement is `walk`, `swim`, or `fly`; metrics are `path`, `manhattan`, or
`chebyshev`. Path fields use eight-neighbor movement, known obstacles and forbidden
areas. Their distances are terrain-weighted travel costs rounded up to neutral
tile equivalents. Cardinal and diagonal steps have equal base cost, preserving
the strategic Chebyshev metric; `swim` treats
walking and swimming as equally fast before terrain modifiers. They do not
predict moving-unit congestion. Resource and building sources
seed obstacle tiles so their neighbors measure distance to the target.
`fieldValue(field,x,y)` returns `known`, `reachable`, `distance`, and
`observedTick`. Null distance is unavailable; reachability stays null when unknown
map regions prevent a definitive unreachable answer. Geometric fields ignore
obstacles and terrain speed. `passable` and `components` expose the same movement rules.

`hotspots` ranks regional source sums, with optional visible-unit strength weights.
With `weight: "strength"`, `strength: {worker: 0, explorer: 0, warrior: 2}` selects
per-type integer multipliers (0..1000) for observed health times attack power.
Native unit/building sources use compact observation records and tile bins,
avoiding full JavaScript entity-record allocation during map analysis.
`summary` and military density values describe observed data; unknown tiles are
not evidence of absence. Spatial helpers never acquire hidden enemy records.

Native work is synchronous and bounded: 16 million logical work units and the
shared 32 MiB native conversion budget per callback. Distance fields charge
roughly eleven work units per map tile plus source enumeration. Hotspot ranking
charges `log2(map tiles) + result limit + 6` units per tile plus enumeration.
Large result limits may exceed the budget on a maximum-size map. Region summaries charge one unit per visited tile. Placement
charges candidate count times the inspected footprint and scoring terms, plus
field costs. Schedule expensive decisions using `ctx.tick` and restrict placement
regions. Up to eight field handles/cached fields are retained per controller.
Cache hits pay the same logical charge as recomputation; eviction, worker count,
and loading a save do not change query results or decision budgets.

### Placement

`ctx.spatial.placement(request)` returns ranked candidates, score contributions,
rejection counts, and an ordinary creation descriptor. `ctx.actions.build(request)`
uses the same solver and queues its best result, or returns null if none exists.

Requests specify `buildingType` (variant ID) or `building` (stable key/authored family
name). The variant must be available and explicitly placeable. They may specify staffing, region, colony anchor,
clearance, upgrade-footprint reservation, and reachable access. Constraints and
preferences compose `distance`, `fertility`, `resourceDensity`, and `threat` terms.
Constraints use integer `min`/`max`; preferences use signed integer `weight`.
Positive weights prefer larger values. Equal scores break by wrapped row-major
coordinate. Footprints must be currently observed and buildable. Pending creations
reserve their footprints for later plans, including within the same callback.
Finding a candidate does not guarantee execution after queued orders reach the game.

### Telemetry

`ctx.telemetry.set(name, value, description?)` publishes numbers, booleans, or short
text. The optional third argument is text or `{description, unit}`. Limits are 128
names per controller, names up to 128 bytes, and text values up to 512 bytes.
The in-game **AI telemetry** menu also displays C++ controllers. Own/allied values
are copied into Scenes during play; spectators and replay viewers can see all teams.
Changes are sampled into a versioned replay diagnostic stream, capped at 16 MiB.
A truncated recording explicitly stops providing telemetry past its coverage.
Older replays report telemetry unavailable. Diagnostics do not enter gameplay
checksums and opening the dialog does not run AI code.
