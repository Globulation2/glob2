# Building catalogs

A game resolves its building catalog before simulation. The installed stock catalog
is `data/buildings/manifest.json`; its `files` list determines dense runtime IDs in
file and variant order. Each variant also has a stable `key`. Keep keys stable when
editing a catalog, and use them for references. Historical `properties.type` and
`shortTypeNum` aliases are optional compatibility and presentation metadata.

A catalog composes the existing unit classes, resources and ability primitives.
Definitions contain data, not executable scripts. Compilation also creates a dense,
64-byte-per-variant runtime table for frequently read simulation traits. Buildings
bind to its immutable rows when their variant changes; strings, artwork and full
service/training recipes remain outside that table. Engine behavior and AI requests
use capabilities. An AI may defer a strategy when its required capability is absent;
a catalog does not have to supply every service.

## Authoring

Copy the stock manifest and the definition files you want to change into a new
directory. Edit the manifest's `catalogKey` and its explicit `files` list. Each
file contains a `variants` array. Paths are relative to the manifest, and only listed
files are loaded. Select the manifest with `--building-catalog PATH` (also accepted
by structured headless commands and map-generation setup).

`schemaVersion` is currently 1. The loader rejects unknown fields, invalid ranges,
unresolved references, transition cycles and inconsistent recipes with contextual
diagnostics. The resolved canonical snapshot must fit the 8 MiB transport bound,
including defaults expanded by the loader. Initial health cannot exceed maximum
health. Installed sprite references use the existing artwork pipeline.
Transferring definitions does not download artwork.

Variant fields are:

| Field | Meaning |
| --- | --- |
| `key` | Unique lowercase ASCII key; letters, digits, dots, hyphens and underscores |
| `next` | Construction completion or the single outgoing upgrade-site key; empty for no transition |
| `previous` | Explicit construction/repair-site reference for a completed variant; empty when absent |
| `requiredExperiment` | Optional catalog experiment needed to make the variant available |
| `properties` | Footprint, health, storage, attraction, firing geometry and artwork values |
| `semantics` | Independent services, costs, scheduling, placement and supply behavior |
| `presentation` | Display name, icons, connection group, team/skin presentation and default staffing preference |

A construction site sets `properties.isBuildingSite` and names its completed
variant in `next`. An upgrade names a site explicitly; IDs and display tiers never
imply the next variant. Only one outgoing upgrade is supported. `startingBuilding`
in the manifest optionally names a concrete starting variant. Setup rejects an
unavailable required starting variant instead of substituting another building.
Existing authored maps keep their explicit definitions.

## Capabilities and units

Resource cost objects use `wood`, `wheat`, `papyrus`, `stone`, `algae`, `cherry`,
`orange` and `prune`. Costs are stock units. `maxResource` and `multiplierResource`
use the existing 15-slot resource wire order; the seven unused slots remain zero
capacity when authoring new definitions. A natural resource packet represents one raw unit; a supplier withdrawal
retains its exact fraction of a raw unit. Delivery converts that fraction using
the recipient's multiplier, accepts available capacity and reports discarded
remainder explicitly.

`semantics.replenishResources` names the resources workers should replenish and
ordinary deliveries may add. Storage capacity does not imply replenishment. The
four `semantics.market` sets `suppliesStockResources`,
`suppliesDirectStockResources`, `fetchesStockResources` and
`fetchesDirectStockResources` independently select resources for each supply or
fetch mode; the corresponding role and experiment switches still apply. When
both fetch modes are enabled for a resource, their eligible providers form a union. Each set
is an array of resource names, rejects duplicates, and defaults to all eight
resources when omitted. The stock definitions list their permissions explicitly.
A configured supplier may expose existing inventory above its nominal capacity.
Outstanding construction and repair materials remain deliverable regardless of
the operating replenishment set.

Unit-class masks use worker=1, explorer=2 and warrior=4; 7 accepts all classes.
Arrays follow that same class order. Service durations advance on inside-unit
updates using `properties.insideSpeed`; healing also scales with missing health,
and training divides that speed by the number of ability levels being gained.
Training includes the completion action: a duration of N takes N+1 inside
actions. Parallel training waits for the slowest requested course under this
inclusive clock, including zero-duration courses, the entry phase, and integer
per-tick advances. Inside services advance by 1–256 delta units per tick,
including the minimum speed after diagonal entry. The upper bound matches the
one-action-per-tick clock and prevents long healing visits from overflowing the
phase counter. It also removes the old post-exit speed burst caused by surplus
phase accumulating during tiny-deficit healing, including stock hospitals;
healing completion still takes at least one tick per inside action. Production uses an inclusive
timeout: a recipe duration of N completes after N+1 eligible producer ticks
(duration zero completes on its first tick). Do not compare these clocks without
conversion.

| Configuration | Behavior |
| --- | --- |
| `semantics.feeding`, `healing` | Enabled flag, class mask, duration, costs, partial-settlement policy, exit admission and configured outcomes |
| `semantics.training` | Explicit ability results, duration, class admission, resource cost and optional independent construction qualification |
| `trainingInParallel` | One configured visit may grant its eligible training bundle |
| `production.recipes` | Independently enabled worker/explorer/warrior recipes with costs and durations |
| `projectileDamage` | Damage by target class, independent of the projectile's building damage |
| `ammunitionResource`, `ammunitionCost` | Ammunition recipe; capacity/cadence/range/speed remain explicit properties |
| `constructionCost`, `repairCost` | Materials independent of operating storage limits |
| `repairable`, `regenerationPerTick` | Permission to repair and passive regeneration are independent |
| `requiredWorkerLevel` | Construction qualification, separate from work speed and presentation tier |
| `assignmentLimit`, `admittedUnitMask` | Shared assignment bound and allowed interior unit classes |
| `occupiesGround`, `relocatable`, `instantPlacement` | Independent placement and movement behavior |
| `properties.zonable` | Attraction enabled independently for each unit class |
| `defaultUnitStayRange`, `maxUnitStayRange` | Initial and maximum attraction radius |
| `market` | Shared/local inventory, direct or routed supply/fetch and inter-team fruit exchange |
| `workPriorityBias`, `sightSharing` | Worker task preference and visibility sharing policy |

Interior services share seats and inventory. A unit requests a service, and unpaid
resource reservations are distinct from occupancy. Completion, cancellation,
expulsion and destruction settle each reservation once. Repair materials that
already restored health are consumed when a repair is canceled. Demolition keeps
construction commitments until removal is final, so cancellation remains safe;
final removal releases new/upgrade funding and consumes paid repair materials. Cumulative paid
materials earn the corresponding fraction of the initial health deficit; damage
received after repair begins remains. A repair returns
to its recorded original variant even when several variants share a repair site.
Current-format loads validate that this completed origin points to the active
site through the appropriate repair or upgrade edge.
Bombing training is independent of school tier, as are worker construction
qualification and work-speed training.

Production has two reusable scheduling policies:

- `weighted_late_choice` shares a timer and selects the class at completion. Its
  enabled recipes must have identical costs and durations.
- `weighted_committed_job` selects and reserves a recipe when work begins. Ratio
  changes affect the next job; a blocked exit retains completed work, spawning
  consumes reserved resources, and cancellation releases them.

Transitions retain production preferences for compatible recipes and initialize
newly enabled recipes from the target variant. Canceling construction restores the
original production preferences. Temporary construction jobs retain these values
in saved state.

A combined building participates in each applicable phase. Resource supply,
healing, feeding, training, production and attraction are not mutually exclusive.
Mixed staffing first fills deterministic role quotas, then lends unused assignment
slots to roles with eligible units on the existing recruitment round.
Connected-segment rendering is configured by `crossConnectMultiImage`, a
`presentation.connectionGroup` and its cross-team connection policy; ordinary
sprite rendering remains available for the same capabilities. `presentation.showLevel`
controls the level label directly, independently of upgrade availability. The stock
market therefore displays its configured level even with its upgrade experiment off.

Supplier selection excludes the recipient itself and any provider drawing from
the same shared inventory. Supplier lists contain only alive buildings; demolition
removes a provider immediately, and canceling demolition restores it. Loading
rebuilds the same membership. Ordinary routed recipients retain shared asynchronous
gradients. A fetch permission uses the ordinary natural-resource field when no
enabled definition can supply that resource or the team has no suppliers of
that mode. Direct recipients share lazy fields; recipients needing supplier
exclusions use separate fields. These synchronous fields share a bounded cache:
64 MiB of cell buffers, or one complete field when a map requires more. Entries
refresh on stock/topology changes and after 128 simulation ticks. Least-recently
used entries are evicted at simulation request boundaries. Saved games retain
resident fields, refresh state and eviction order so continuation follows the
same routes. These consumer-aware requests run on the simulation thread; AI
workers use the existing shared-field interface.

## Experimental definitions

Declare experiment metadata in the manifest and reference the key from a variant:

```json
"experiments": [
  {"key": "field-kitchens", "label": "Field kitchens",
   "help": "Makes the configured mobile feeding buildings available."}
]
```

```json
"requiredExperiment": "field-kitchens"
```

Experiment keys use lowercase letters/digits separated by single hyphens, with
1–128 characters; labels and help must be nonempty. At most 64 distinct keys,
including the built-in experiments, may be registered.

No C++ experiment enum or building-role branch is needed. Installed catalog
experiments appear in the existing experimental-features controls. Structured
runs opt in explicitly with `--experiment field-kitchens`. Definitions embedded
in a map or save carry their own validated experiment metadata; loading one game
does not mutate another game's catalog. New local games take only preferences
declared by their destination catalog, plus built-in engine experiments. Saves
retain their original selection regardless of installed preferences.
See [experimental features](experimental-features.md)
for preference and key-retirement rules.

## Persistence and verification

Format 137 embeds the resolved catalog and ID mapping in maps, saves and replay
setup; multiplayer setup also binds the catalog fingerprint. The fingerprint covers the
whole canonical snapshot, including presentation metadata, so changing a label or
artwork reference currently changes catalog identity too. Installed definition
changes do not rewrite a saved game's rules. Format 136 terrain registries and the
existing save-support floor remain readable through versioned loaders. Replay and
network boundaries are intentionally stricter than save loading.

Stock compatibility checks compare the shipped definitions against the frozen
legacy import. Service/production fixtures check accounting and continuation;
custom-catalog fixtures check replacement providers, absence handling, mixed
capabilities, explicit transitions and malformed inputs. Maintain deterministic
execution within the current simulation. Do not add ordering or RNG compatibility
branches merely to reproduce an obsolete trajectory.

Before accepting changes to this foundation, compare release builds against
current master using identical inputs and dependencies, including gradient
preparation and whole-game workloads. Retain raw timing, allocation/memory and
continuation evidence. A repeatable slowdown above 3% fails the building-refactor
performance gate. See [verification guidance](../development/reference.md#local-and-vm-pr-verification).
