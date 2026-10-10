# Unit definitions and runtime behavior

Unit definitions are developer-authored simulation configuration. The shipped
roster remains worker, explorer and warrior; there is no player-facing authoring
or selection interface. Their IDs are permanently 0, 1 and 2. Additional IDs are
local to an immutable catalog and must be resolved from stable keys at setup.
Catalogs contain at most 1,024 definitions; game state allocates by actual count.

`UnitCatalog` reads `data/units/registry.json`. Authored entries may extend an
existing definition and override its behavior fields, four ability-level tables,
material production cost and presentation identity. Parsing validates the whole
catalog before publication. Missing installed data falls back to baked legacy
definitions; malformed installed data fails. Definitions gated by experiment
metadata are compiled as unavailable unless the game enables that experiment.
Spawning and production enforce the same availability decision.

A game owns the resolved catalog. Race tables are per-game, snapshots share the
immutable catalog, and two games can use different definitions simultaneously.
Rich definitions contain stable keys and metadata. A separate contiguous runtime
table contains numeric traits; units cache capability flags and effective ability,
hunger and learning values. Vision radius uses an existing padding byte in unit
state, so completed actions read it directly without growing the record. Loading
and supported catalog setup rebuild this derived cache; saves and checksums
continue to identify it through the catalog. Simulation loops do not resolve keys, scan the catalog
for a type, allocate behavior objects or invoke virtual behavior dispatch.

## Authoring a definition

A source catalog overlays the built-in definitions and appends new stable keys.
Inheritance resolves in source order, so a base must already exist. This example
adds a developer-only larger carrier without altering the shipped registry:

```json
{
  "schemaVersion": 1,
  "experiments": [
    {"key": "bulk-carriers", "label": "Bulk carriers", "help": "Test larger mixed inventories."}
  ],
  "units": [
    {
      "key": "bulk-worker",
      "extends": "worker",
      "requiredExperiment": "bulk-carriers",
      "mesh": "worker",
      "sprite": "units",
      "behaviors": {"cargoCapacity": 4, "cargoKinds": 3},
      "cost": {"food": 2}
    }
  ]
}
```

Compile with `UnitCatalog::fromJson`, bind it with
`game.gameHeader.setUnitCatalog`, and call `Game::configureBuildingCatalog`
before creating dependent entities. A resolved snapshot uses
`UnitCatalog::serialize`/`deserialize`; it must contain complete definitions and
cannot contain unresolved inheritance. Definitions and snapshots have an 8 MiB
limit. Unknown fields, keys, invalid arithmetic bounds and unresolved experiments
fail validation.

`configureBuildingCatalog` also binds production widths and interaction rows to
the unit count. `configureUnitCatalog` alone updates unit state and availability;
it does not prepare buildings for added definitions.

Add an explicit building production recipe keyed by `bulk-worker`, and include
its weight in the production `initialRatios` object. Recipe duration remains a
building property. Admission services use a `units` array of stable keys;
`admittedUnits` selects interior eligibility; `attractionUnits` selects stable
keys separately for `clear`, `explore` and `defend` jobs; a `projectileDamage`
object maps stable keys to damage. A uniform historical three-entry damage array
also applies to additional types; heterogeneous arrays require explicit additional
target keys. Keyed overrides take precedence. See [building semantics](../features/building-semantics.md).
The three-choice gameplay controls and AI production strategy continue to select
the built-ins. Default map generators also retain their starting-worker roster
and choose clear land tiles. A water-only replacement for the starting worker
requires authored starting positions; these generators report a placement
failure rather than creating units on illegal terrain.

`levels` contains exactly four objects when supplied. Each object can override
individual fields inherited from its base, but a supplied `performance` or
`startImage` array must have its full length. Performance indices follow
`Abilities` in `src/unit/UnitConsts.h`: idle clocks 0–2, walk/swim/fly 3–5,
build/harvest 6–7, melee speed/strength 8–9, ranged air/ground 10–11,
material creation 12–14, armor 15 and HP 16. Animation indices follow `NB_MOVE`.
`harvestDamage`, `armorReductionPerHappyness`, `experiencePerLevel` and
`magicActionCooldown` are per-level fields. `hungriness` in these tables retains
the historical learning modifier; `behaviors.hungerRate` controls food loss.

Behavior switches include transport, construction, clearing, melee, movement,
ranged target planes, material creation, conversion, contact interruption and
idle defense/clearing/exploration/medical policies. `learnableMask` independently
selects trainable abilities. Numeric fields configure food capacity/consumption,
starvation damage, Q8 regeneration and service multipliers, cargo capacity and
kind limits, vision/search/magic ranges, recruitment ranking and rational hunger,
retreat, healing and rebound thresholds. Consult `UnitRuntimeTraits` and the
strict parser in `src/unit/types/UnitCatalog.cpp` for accepted names and bounds;
these contracts are exercised by `UnitCatalog` and `UnitCustomization` fixtures.

## Capabilities and upgraded values

A capability enables a behavior; ability tables determine its effective value at
each level. Both must permit an action. For example, a nonzero attack statistic
with melee disabled does not enable melee. Every unit has health. Zero hunger
means no feeding requirement. Immobile definitions retain stationary actions, passive healing and aura services.
They do not enter services that require movement. Producers can place them at
free ground exits without enabling movement. Newly authored flight capability
requires flight at every level: airborne units do not acquire ground occupancy
through training. Walking and swimming use the existing terrain routing; a
swim-only class excludes land and allocates its fields only when needed.
Training admission projects every eligible parallel grant together before
reserving materials. Authored courses cannot remove all movement or change
movement to a mode unsupported by the building's exit terrain; temporary
occupants do not prevent admission. Unsafe restored visits release their
reservations and exit without a partial charge or level change. Direct
single-course `applyTraining` returns false without mutation if it would remove
the last movement mode. Imported historical tables retain their original
application and admission policies for replay compatibility.

Transport, construction, clearing, melee and ranged combat are independent.
Automatic flag recruitment, painted-area seeking, combat interruption, retreat
and regeneration have separate policies. `recruitClear`, `recruitExplore` and
`recruitDefend` independently permit new automatic flag assignments. Missing
values retain the preceding capability-derived eligibility; explicit values
inherit from a parent definition. A true policy cannot enable a missing work
capability, and a building's `attractionUnits` selector cannot bypass a false
policy for a new hire. Semantic attraction roles still govern existing jobs,
building orders, routing and team-list memberships. Changing recruitment policy
during setup does not revoke an existing valid assignment. `clearIdle` and
`guardIdle` instead control autonomous seeking
of painted clearing and guard areas; `exploreIdle` controls autonomous fog
exploration. Recruitment overrides are optional in resolved snapshots, so
pre-policy catalogs preserve their serialized bytes and identity.
`countsForSurvival` determines whether a unit can keep its team alive;
workers and warriors enable it, while explorers retain their historical exclusion.
`releaseClearingClaims` releases an idle clearing claim on conversion or direct
destruction. New definitions enable this policy; the shipped definitions preserve
legacy conversion claim handling. Ordinary death still releases claims for every
definition. Service multipliers use Q8 values, where 256 is the existing speed.
The assigned purpose records transport, clearing, exploration or defense;
capabilities alone do not identify the current job of a hybrid unit.

Production strategy in existing AIs still chooses the three built-ins. Labor,
combat, recruitment and feeding calculations use capabilities and effective
properties. Population aggregates avoid counting a hybrid as simultaneously
performing multiple assignments. Idle defender reserves exclude units that
refuse automatic defense recruitment; total combat counts and ongoing defense
assignments retain their separate meanings. Sampling caches recruitment
permission independently of qualification across every upgrade level, so a
partially trained type can still contribute its currently eligible units.
Feeding admission uses bounded deterministic
capacity allocation rather than enumerating subsets of all types.
Maxima retains its minimum attack-ability level as the unlearned army estimate,
using configured speed, strength and reference armor at that level. Imported
historical race tables retain their private legacy strategy calibration. When
built-in melee is disabled, enabled custom melee definitions provide a fallback
reference for enemy strength.

Training-provider summaries and AI recipient tables use compiled learnability
and active capabilities. Construction qualification is independent: any
learnable course can grant it to a definition with `learnConstruction`, including
a course whose ordinary ability is inactive. Qualification-only courses do not
count as ability training.

## Cargo and production

Cargo capacity counts raw transport packets, including fractional supplier
packets. Default workers carry one inline packet. Larger inventories use a
game-owned sidecar keyed by unit identity; snapshots copy its values. Additional
packets retain material identity, pickup order and exact fractions. Mixed pickup
uses deterministic material ordering. Each completed work action delivers at
most one usable raw packet; rejected packets do not block later usable packets.
Rejected delivery remains cargo on the
extended path; legacy workers retain their original delivery and spillage rules.
Wide fractions use checked portable arithmetic. A delivery that cannot represent
its exact remainder is refused before mutating stock.

Definitions own default production material costs. A building recipe inherits
that cost when omitted; an explicit empty cost means free production. Buildings
continue to own production duration and scheduling. Additional types need an
explicit recipe or developer/test spawning. Existing three-choice orders change
only built-in weights and preserve configured additional weights.

Building interactions compile once into pooled immutable rows addressed directly
by unit ID. Identical complete rows are shared, and compilation rejects more
than 16 MiB of unique interaction storage. The hot building runtime record
remains 64 bytes. Default statistics
and production counters remain inline; additional counters use bounded sidecars.
Statistics reuse team-owned sampling buffers and compile level qualification
rows once per immutable catalog; replacing a catalog invalidates these derived
rows even when its number of definitions is unchanged. Live catalog setup rejects
resizing while projectiles retain launch-time damage rows. Save loading replaces
the discarded game's sectors before restoring its own projectile rows.

## Presentation and persistence

Presentation identity selects an existing mesh/skin slot independently of unit
ID. Animation offsets select frames from the current unit atlas. Several
simulation definitions can share worker appearance. This foundation adds no new
artwork, skin format or customization UI.

Format 153 embeds resolved definitions before dependent state, including cargo
and assignments. Earlier saves remain supported back to format 58. Formats 58–72
read saved race tables inside each team's BaseTeam record before loading units.
Later legacy formats first read records with provisional definitions, then adopt
the saved race tables. Neither path recalculates saved performance or hunger
caches. The last saved
race table applies to every team, matching historical shared-table loading.
Converted legacy tables can carry a private movement compatibility marker;
new authoring cannot request that exception to the flight invariant.
Historical tables with zero health remain loadable and can be saved again;
developer-authored definitions require positive health. The private migration
policy also retains historical AI feeding calibration when an old map saved a
different hunger clock. Newly authored definitions use their configured clock.
Legacy resaves also preserve unreserved inside-list entries with no declared
service (`ACT_RANDOM`, purpose `-1`) only under private migration provenance.
Authored visits retain strict membership, assignment and reservation validation.

Current checksums include catalog identity and extended authoritative state.
Version-152 replay playback uses a representation adapter for its historical
checksum. Compatibility normalization excludes only added representation;
it must not conceal changed gameplay, random streams or orders. See
[simulation verification](../development/simulation-verification.md),
[persistence](persistence.md) and [replay verification](../development/headless-replays.md).

Online MatchSetup carries an optional `unitCatalog` resolved snapshot and digest.
Map reports, stored map metadata, rooms and engine jobs preserve that snapshot;
consumers verify its digest and experiment requirements before starting a game.
The platform database migration `0059_unit_catalogs.sql` must accompany deployment
of the new map metadata transport. The LAN setup path verifies the same catalog
identity. Existing implicit game headers inherit the map's catalog.
For older maps, the migration placeholder is omitted from emitted setups and
inherits definitions recovered from saved race tables. An explicit different
setup snapshot is rejected, including installed defaults that differ from those
tables. Current files always enforce their embedded catalog identity, even when
that catalog was recovered from a legacy save.
