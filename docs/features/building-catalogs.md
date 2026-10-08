# Building catalogs

A game resolves its building catalog before simulation. The installed stock catalog
is `data/buildings/manifest.json`; its `files` list determines dense runtime IDs in
file and variant order. Each variant also has a stable `key`. Keep keys stable when
editing a catalog, and use them for references. Historical `properties.type` and
`shortTypeNum` aliases are optional compatibility and presentation metadata.

A catalog composes the existing unit classes, materials and ability primitives.
Definitions contain data, not executable scripts. Compilation also creates a dense,
64-byte-per-variant runtime table for frequently read simulation traits. Buildings
bind to its immutable rows when their variant changes; strings, artwork and full
service/training recipes remain outside that table. Engine behavior and AI requests
use capabilities. An AI may defer a strategy when its required capability is absent;
a catalog does not have to supply every service.

## Authoring

Copy the stock manifest and the definition files you want to change into a new
directory. Edit the manifest's `catalogKey` and its explicit `files` list. Each
file contains a `variants` array. Each `files` entry is a sibling basename, such
as `field-kitchen.json`; subdirectories, absolute paths and duplicate filenames
are rejected. Only listed files are loaded. Manifests do not inherit another
catalog implicitly. Select the manifest with `--building-catalog PATH` (also accepted
by structured headless commands and map-generation setup).

`schemaVersion` is currently 1. The loader rejects unknown fields, invalid ranges,
unresolved references, transition cycles and inconsistent recipes with contextual
diagnostics. The resolved canonical snapshot must fit the 8 MiB transport bound,
including defaults expanded by the loader. Initial health cannot exceed maximum
health. Installed sprite references use the existing artwork pipeline.
Standalone catalog definitions do not transfer installed artwork. Portable
building-family packages can include custom frames, as described below.

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

## Portable building families

The `BuildingPackage` contract in `platform/packages/protocol/src/buildings.ts`
represents an additive family, including its construction, repair and upgrade
variants. A package has `schemaVersion: 1`, a lowercase UUID `namespace`, and
`variants`, `experiments` and `sprites` arrays. Variant, experiment and connection
group keys use the prefix `b-<namespace>-`. Variant references and experiment
requirements stay within the package; packages cannot replace stock definitions
or change the base catalog's starting building. Runtime `id` fields are rejected.
The platform's fork helper assigns a new namespace and rewrites those references.
Every package variant needs a `properties.gameSprite`. It also needs
`properties.miniSprite` unless `miniSpriteImage` is explicitly `-1` to suppress
that image. Omitting these paths leaves unusable engine defaults and is rejected
during native validation.

Compose authored manifests with the capability-advertised command:

```sh
glob2 --compose-buildings --package first-family.json --package second-family.json
```

An optional `--base PATH` selects a base catalog manifest.
`--artwork-bundle PATH` also verifies a portable artwork bundle against the composed
catalog and returns its checked `artworkHash`. Output is one JSON
object with `schemaVersion`, `baseHash` and `catalog: {snapshot, hash}`. Packages
are sorted by namespace, then appended in their authored variant order. Existing
base IDs remain stable. Duplicate namespaces and invalid compositions are
rejected atomically. With no packages the base snapshot and fingerprint remain
unchanged. `GlobEngine.composeBuildings` checks the binary's advertised capability
and base fingerprint and retains its exact canonical output.

Sprites have a local `key` and ordered `frames`. Each frame declares `imageHash`,
`width`, `height`, and optionally `teamColorHash` for the existing rotated-color
layer. A variant uses `package:<sprite-key>` in `gameSprite` or `miniSprite`, or
references installed `data/gfx/` artwork. Composition replaces package references
with content-addressed sprite paths. Damage frame ranges, miniature indices and
all sixteen connected-segment frames must fit the declared sprite. Artwork hashes
contribute to catalog identity. Composition validates declarations; it does not
read, install or render the declared image bytes.

The platform ZIP helpers export deterministic archives containing `manifest.json`
and `assets/<sha256>.png` or `.webp`. Imports accept stored or deflated ZIP32
members, check hashes and CRCs, and reject traversal, symlinks, duplicate members,
undeclared images, ambiguous JSON and decompression beyond the upload bound.
Image normalization accepts still PNG/WebP frames, verifies declared dimensions,
then retains lossless sRGB WebP with transparency and no source metadata.

Shared limits are 32 MiB for the archive and its expanded contents, 8 MiB for
manifests, 512 pixels per frame side, 256 image/layer entries per package, and
64 MiB of decoded custom pixels per composition. The existing 4,096-variant,
experiment and resolved-snapshot bounds also apply.

The standalone `G2BA0001` artwork bundle contains a canonical sprite manifest and
hash-addressed normalized WebP bytes. The native decoder verifies hashes, image
headers, decoded pixels, dimensions, catalog references and frame bounds before producing an
in-memory community asset mount. Community mounts cannot shadow installed
artwork or fall through to filesystem assets. Format 144 embeds the bundle after the resolved catalog in map, save and replay
headers. Older files load without a bundle. The native loader verifies the
embedded bytes before mounting sprites, including in headless validation.
Composing packages alone does not publish a release.

## Capabilities and units

Material cost objects use `wood`, `food`, `paper`, `stone`, `algae`, `cherries`,
`oranges`, `prunes`, `gold`, `metal`, `glass` and `fabric`. Costs are stock units.
`properties.maxMaterial` and `properties.materialMultiplier` are 15-element arrays:
the first twelve positions use that fixed material order, followed by three reserved
positions. Reserved capacities must be zero; every delivery multiplier must be at
least one. Harvesting supplies a material packet representing one raw unit; supplier
withdrawals retain their exact fraction of a raw unit. Delivery converts that fraction
using the recipient's multiplier, accepts available capacity and reports discarded
remainder explicitly.

`semantics.replenishMaterials` names materials workers should replenish and ordinary
deliveries may add. Storage capacity does not imply replenishment. The four
`semantics.market` sets `suppliesStockMaterials`, `suppliesDirectStockMaterials`,
`fetchesStockMaterials` and `fetchesDirectStockMaterials` independently select
materials for each supply or fetch mode; role and experiment switches still apply.
When both fetch modes are enabled, eligible providers form a union. Each set is an
array of material keys and rejects duplicates. Default permission masks include all
fixed materials; existing recipes and capacities still use their historical inputs.

Legacy resource-named fields and the keys `wheat`, `papyrus`, `cherry`, `orange`,
`prune` remain accepted import aliases. Canonical saved catalogs use material names;
authoring both aliases for one field or material is an error. See
[resource catalogs](resource-catalogs.md) for map deposits that supply these materials.
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

All paths in this table are relative to one variant:

| Configuration | Behavior |
| --- | --- |
| `semantics.feeding`, `semantics.healing` | Enabled flag, class mask, duration, costs, partial-settlement policy, exit admission and configured outcomes |
| `semantics.training` | Ability-name object with explicit results, duration, class admission, material cost and optional independent construction qualification |
| `semantics.trainingInParallel` | One configured visit may grant its eligible training bundle |
| `semantics.production.recipes` | Unit-name object with independently enabled worker/explorer/warrior recipes, costs and durations |
| `semantics.projectileDamage`, `semantics.projectileBuildingDamage` | Unit-class damage array and independent building damage |
| `semantics.ammunitionMaterial`, `semantics.ammunitionCost` | Material index and amount per ammunition refill; capacity/cadence/range/speed remain explicit properties |
| `semantics.constructionCost`, `semantics.repairCost` | Materials independent of operating storage limits |
| `semantics.repairable`, `semantics.regenerationPerTick` | Permission to repair and passive regeneration are independent |
| `semantics.requiredWorkerLevel` | Construction qualification, separate from work speed and presentation tier |
| `semantics.assignmentLimit`, `semantics.admittedUnitMask` | Shared assignment bound and allowed interior unit classes |
| `properties.maxUnitInside` | Shared interior seats; every enabled interior service needs at least one admitted class and one seat |
| `semantics.placeable`, `semantics.instantPlacement` | Availability for placement and whether a completed variant can be placed directly |
| `semantics.occupiesGround`, `semantics.relocatable` | Independent occupancy and movement behavior |
| `properties.zonable` | Attraction enabled independently for each unit class |
| `properties.defaultUnitStayRange`, `properties.maxUnitStayRange` | Initial and maximum attraction radius |
| `semantics.market` | Shared/local inventory, direct or routed supply/fetch and inter-team fruit exchange |
| `semantics.workPriorityBias`, `semantics.sightSharing` | Worker task preference and visibility sharing policy |

Interior services share seats and inventory. A unit requests a service, and unpaid
material reservations are distinct from occupancy. Completion, cancellation,
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
  consumes reserved materials, and cancellation releases them.

Transitions retain production preferences for compatible recipes and initialize
newly enabled recipes from the target variant. Canceling construction restores the
original production preferences. Temporary construction jobs retain these values
in saved state.

A combined building participates in each applicable phase. Material supply,
healing, feeding, training, production and attraction are not mutually exclusive.
Mixed staffing first fills deterministic role quotas, then lends unused assignment
slots to roles with eligible units on the existing recruitment round.
Connected-segment rendering is configured by `crossConnectMultiImage`, a
`presentation.connectionGroup` and its cross-team connection policy; ordinary
sprite rendering remains available for the same capabilities. Ground and overlay
buildings share connection and damage-frame selection; non-segmented healthless
variants use the base sprite frame. `presentation.showLevel`
controls the level label directly, independently of upgrade availability. The stock
market therefore displays its configured level even with its upgrade experiment off.

Supplier selection excludes the recipient itself and any provider drawing from
the same shared inventory. Supplier lists contain only alive buildings; demolition
removes a provider immediately, and canceling demolition restores it. Loading
rebuilds the same membership. Ordinary routed recipients retain shared asynchronous
gradients. A fetch permission uses the ordinary material-source field when no
enabled definition can supply that material or the team has no suppliers of
that mode. Direct recipients share lazy fields; recipients needing supplier
exclusions use separate fields. These synchronous fields share a bounded cache:
64 MiB of cell buffers, or one complete field when a map requires more. Entries
refresh on stock/topology changes and after 128 simulation ticks. Least-recently
used entries are evicted at simulation request boundaries. Saved games retain
resident fields, refresh state and eviction order so continuation follows the
same routes. These consumer-aware requests run on the simulation thread; AI
workers use the existing shared-field interface.

## Experimental definitions

Declare experiment metadata in the manifest and reference its key through a
variant's `requiredExperiment` field. The definition remains in the catalog when
the experiment is off, but is unavailable for placement.

### Complete field-kitchen example

The retained [example manifest](../../test/fixtures/building-catalog/authoring/manifest.json)
and [field-kitchen definition](../../test/fixtures/building-catalog/authoring/field-kitchen.json)
are loaded directly by the `BuildingCatalog` tests. The definition has no historical
family alias. It composes feeding and healing, three shared seats, food storage,
and two delivery-worker slots using the installed inn artwork:

```json
{
  "variants": [{
    "key": "field-kitchen.finished",
    "requiredExperiment": "field-kitchens",
    "properties": {
      "width": 2, "height": 2, "hpInit": 200, "hpMax": 200,
      "insideSpeed": 12, "maxUnitInside": 3,
      "maxMaterial": [0, 12, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
      "gameSprite": "data/gfx/inn0b", "miniSprite": "data/gfx/miniinn0b"
    },
    "semantics": {
      "placeable": true, "instantPlacement": true, "repairable": false,
      "assignmentLimit": 2, "admittedUnitMask": 7,
      "replenishMaterials": ["food"],
      "feeding": {"enabled": true, "unitMask": 7, "duration": 20, "cost": {"food": 1}},
      "healing": {"enabled": true, "unitMask": 7, "duration": 40, "cost": {}}
    },
    "presentation": {
      "displayName": "Field kitchen", "iconFrame": 1,
      "showLevel": false, "defaultAssigned": 2
    }
  }]
}
```

The example deliberately uses free instant placement and free healing to keep the
configuration small; those are balance choices, not implied by feeding or healing.
It has no repair or upgrade edge. Wheat capacity alone would not recruit delivery
workers: `replenishMaterials`, `assignmentLimit` and staffing also matter. The
standalone example manifest validates this one definition; it has no starting
colony and is not a replacement for a playable stock catalog.

To try it alongside the stock buildings, run this from the repository root. It
copies the stock catalog and explicitly appends the example file and experiment:

```sh
python3 - <<'PYTHON'
import json
import shutil
from pathlib import Path

output = Path("artifacts/field-kitchen-catalog")
shutil.copytree("data/buildings", output)  # Choose a fresh directory for each copy.
example = Path("test/fixtures/building-catalog/authoring")
shutil.copy2(example / "field-kitchen.json", output)
manifest_path = output / "manifest.json"
manifest = json.loads(manifest_path.read_text())
manifest["catalogKey"] = "stock-with-field-kitchens"
manifest["files"].append("field-kitchen.json")
manifest["experiments"].extend(json.loads((example / "manifest.json").read_text())["experiments"])
manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
PYTHON

build/linux/client/release/src/glob2 --run-game \
  --building-catalog artifacts/field-kitchen-catalog/manifest.json \
  --generator 15 --map-seed 42 --param teams=2 --param width=7 --param height=7 \
  --game-seed 19 --player numbi --player castor --experiment field-kitchens \
  --ticks 512 --save initial --save final --telemetry checksums \
  --output-dir artifacts/field-kitchen-game
```

Use your platform's client executable and a fresh output directory. This creates a
map with the copied catalog and enables the extra building; the AIs still choose
providers according to their strategies. Omitting `--experiment field-kitchens`
keeps the definition unavailable. Existing authored maps retain their own building
references and are not silently converted by `--building-catalog`.

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

### Private authoring workspace

To create and share a family:

1. Sign in to the website with a registered account, open **Buildings**, then
   **Create a building family**. Give the draft a descriptive name.
2. Edit the initial variant's properties and capabilities. Keep its key stable;
   display names can change without rewriting references. Add stages for explicit
   construction and upgrade transitions, and link them using `next` and `previous`.
3. Reuse an installed stock sprite or add custom sprite frames in the artwork
   controls. Team-color layers are optional and must match the base image's size.
4. Save the draft. Fix validation errors before publication. JSON editing is useful
   for advanced capabilities; the engine validates the saved package as a whole.
5. Choose a description and visibility, then publish the saved revision. Wait for
   native validation; rejected releases explain the error. Further draft changes
   need another publication to appear in the library.
6. In the game, open **Building families** on a local new-game or editor new-map
   screen, browse public families or paste the family's page link into **Family
   link or ID** and choose **Open family**. Private families need sign-in to the
   same instance. Install the compatible release and enable it. Selection applies to new
   maps in that profile. To play it online, share the resulting map and select it
   in the room. Downloading a ZIP on the website exports the authored package.

Common authoring controls include:

| Control | Meaning |
| --- | --- |
| `hpInit`, `hpMax` | Health on creation and maximum health; initial health cannot exceed the maximum |
| `isBuildingSite` | Construction/repair stage; its `next` must name a completed variant |
| `level` | Display tier, 0–3; it does not infer an upgrade link |
| `placeable`, `instantPlacement` | Whether players can place the variant; a completed placeable variant needs instant placement, otherwise use a construction site |
| `repairable`, `previous` | Repair capability and explicit site reference; repairable variants need a positive maximum health and a previous construction site |
| `constructionCost` | Costs attached to a construction site; completed variants cannot carry them |
| `requiredExperiment` | Package-local experiment gate; an unavailable gate keeps the variant out of the new game's choices |

The service and presentation sections earlier in this guide describe the remaining
capabilities. Definitions compose existing primitives; publication does not add
new executable game behavior.

The web app's `/building-studio` page creates private building-family drafts for
registered accounts. It provides scalar property controls, nested JSON controls,
complete manifest editing, upgrade-stage links, experiment definitions and sprite
frame previews. Authors can upload a base frame or an optional team-color layer,
import a package, and export the last saved package. Exporting preserves the
family namespace; importing a package replaces the selected draft's content.

`/api/v1/building-drafts` lists and creates drafts. The draft resource supports
reading, saving and deleting; `/archive` imports and exports ZIP packages;
`/frame` replaces or appends a frame; and `/assets/<hash>` serves an owned draft's
normalized artwork. Saves and uploads require the current revision UUID and
reject stale updates. Changing a manifest cannot invent missing assets. Uploads
retain only normalized, still, lossless WebP bytes, and team-color layers must
match their base frame dimensions. Account exports include the saved ZIP bytes;
account deletion removes private drafts.

The workspace limits each account to 100 drafts, 64 MiB of saved packages, and
60 saves or uploads per hour.
Device recovery copies supplement account drafts; authors explicitly restore a
copy instead of silently replacing a newer account revision. These are authoring
interfaces. Publishing a saved revision submits an immutable release for native
validation; later edits do not rewrite that release.
### Online library and installed families

`/buildings` browses the public library; each family page lists pinned releases,
validation state, simulation version, downloads, likes, favourites, reports and
forking into a private Studio draft. Publication defaults to unlisted. Only an
active registered account can publish, and the namespace remains owned by its
first publisher. Public, unlisted and private visibility follow the same access
rules as the map library. Hidden families cannot be downloaded or installed.
Moderators can hide/restore families and resolve their reports. Account export
includes drafts, published families, release metadata and the account's social
activity; account deletion removes that authored library content.

Owners can edit a family's published name, description and visibility without
revalidating its releases, or withdraw the family after confirmation. Withdrawal
removes its library releases and social activity; it leaves the Studio draft and
maps, saves and replays that already embed the family's data intact. These
metadata changes do not change catalog identity. Moderation and owner changes
are recorded in the administration audit log.

Moderators retrieve the unresolved report queue with
`GET /api/v1/building-reports`, hide or restore a family with
`PUT /api/v1/buildings/:id/moderation` (`hidden` and `reason`; hiding requires a
nonempty reason), and mark a report resolved with
`PUT /api/v1/building-reports/:id` and `{"resolved": true}`. Hiding a family does
not itself resolve its reports. The website exposes hide/restore on a family
page; the report queue and resolution workflow currently use these API endpoints.

`POST /api/v1/building-drafts/:id/publish` freezes the exact saved archive and
submits `validate-buildings` to a fresh engine agent that advertises
`compose_buildings`. Suite 1 checks package closure, deterministic composition,
stock identity, sprite references and bounded full artwork decoding. A validated
release is bound to its archive hash, stock hash, simulation version and suite.
Unavailable engines leave publication unavailable; pending/rejected releases
cannot be downloaded. Families retain at most 50 releases and publishing/forking
is limited to 20 requests per account per hour. Reports are limited to 10/hour.

The native new-game and new-map screens open **Building families**. The picker
installs a chosen compatible release, verifies its package and artwork hashes
and resolved catalog, and lets players explicitly enable or remove families.
Unlisted families open by their page link or ID; links must belong to the
selected instance. Private family lookup uses that instance's signed-in account.
Installed releases stay pinned until another release is chosen. The cache is
limited to 100 families and 64 MiB; combined manifests and decoded pixels retain
the composition bounds above. Selection is applied before new-map generation.
Loaded maps and ongoing/saved games retain their own definitions and artwork.
Share a newly created map through the existing map library to use those families
in an online room; every participant and verifier receives the map's embedded
catalog and frames. No family cache or original library availability is needed
to load a shared map, save or replay.

Existing map-sharing limits still apply: uploads default to 16 MiB of transferred
bytes, and the native map cache and engine agent accept at most 64 MiB of raw map
data. A valid local package or combined artwork bundle can exceed those limits;
use smaller frames or fewer families when preparing an online map. The bundle's
wire format is limited to 72 MiB, and each mount is limited to 64 MiB of encoded
frame bytes. The 64 MiB decoded-pixel bound applies to each composition. The
Toolkit also retains
previously loaded sprite objects for the session, so repeated loading of distinct
custom artwork can accumulate decoded residency beyond the current bundle's
bound; replacing the mounted bundle does not free those retained sprites.

The native installation API uses a validated release's `/runtime` descriptor
and `/artwork` bundle, while `/archive` exports its portable ZIP. The runtime
descriptor carries the exact canonical package JSON as a string and its hash,
avoiding numeric-serialization differences between JavaScript and C++. All
release resources recheck visibility and moderation on every request.

For command-line new maps, write the composed catalog snapshot to a JSON file
and pass `--building-catalog` with `--building-artwork` to `--generate-map`.
The latter accepts the verified `G2BA0001` bundle, which is embedded in the output.
Format 144 and network protocol 62 separate clients using the new header layout;
the save-support floor remains 58 and replay acceptance remains at 143.
