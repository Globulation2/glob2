# Building catalogs

## On this page

- [Authoring](#authoring)
- [Experimental definitions](#experimental-definitions)
- [Persistence and verification](#persistence-and-verification)
- [Related guides](#related-guides)

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

build/linux/client/release/src/glob2 game run \
  --building-catalog artifacts/field-kitchen-catalog/manifest.json \
  --generator 15 --map-seed 42 --set teams=2 --set width=7 --set height=7 \
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
performance gate. See [verification guidance](../development/verification.md#local-and-vm-pr-verification).

## Related guides

- [Building family packages](building-family-packages.md).
- [Building semantics](building-semantics.md).
- [Building authoring](building-authoring.md).

Related: [features and content](README.md).
