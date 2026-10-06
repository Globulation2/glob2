# Experimental features

Experiments are gameplay features we are still testing. They are off by default;
a player switches them on under **Settings → Experiments**. New local or hosted
games retain enabled built-in experiments and enabled building experiments declared
by their destination catalog, plus any terrain experiments required by the map.
That selection stays with the game for its whole life. This guide
covers what players see, the compatibility rules, and how to add an experiment.

## What players see

- **Settings → Experiments** lists one switch per experiment in this build, with
  a line explaining what it changes. Building catalogs can declare additional
  switches with their own stable keys and English labels and help. Translations
  take precedence when available; untranslated catalog entries use that English
  text. The page says when a build has none.
- The set applies to **new games only**: a custom game, a map file played from the
  load screen, a headless `-test-games` match, and a multiplayer game the player
  hosts online or on LAN. A joiner plays with the host's set, whatever their own
  settings say.
- **Campaign missions and the tutorial ignore local experiment preferences**:
  scripted content carries its authored requirements.
- A **saved game or replay keeps the set it was started with**, even if the player
  has since changed the setting. Loading a save never applies current settings.
- Where the set shows: the custom-game lobby footer ("Experiments: …"), the
  multiplayer lobby's side panel and its **Other options** dialog ("Experiments set
  by the host: …", read-only for every player), and the load screen's details for a
  saved game that carries any.
- Terrain experiments control which brushes the editor offers. A map containing
  experimental terrain declares that requirement and enables it when played, even
  if the local editor switch is off. Ice and Trail have separate switches; every
  other terrain group (`src/map/TerrainGroup.h`) has one switch shared by all of
  its visual variants.
- Experiments can change balance and pacing. A map without experimental terrain
  does not acquire new terrain when a switch is enabled.

## Compatibility

`Engine::applyLocalExperiments` (`src/engine/EngineInit.cpp`) is the one place the
new-game rule lives: it filters settings to built-in keys and the destination
building catalog, then adds required terrain experiments to the header unless the
map is a saved game. The local-preference entry points above call it. A hosted multiplayer game sends its
header with the map, so joiners see the set in the lobby.

The set lives in `GameHeader` (`src/game/GameHeader.h`) as an `ExperimentSet`
(`src/game/ExperimentalFeatures.h`), written after the custom-game rules and gated on
save format 124 (`FILE_FORMAT_VERSION_EXPERIMENTS`). It travels in saves, replays
and the match setup every peer of a game starts from, so every peer runs the same
set. Adding the field changed the header's wire
format and introduced network protocol 47. Terrain format 134 adds required
experiments to `MapHeader`. Building format 137 embeds the building catalog and
its experiment definitions; network protocol 57 and replay floor 137 separate the
current simulation from earlier clients. The supported save floor remains 58.

Saves, replays and the wire carry each enabled experiment's **key** (a stable
kebab-case string such as `guard-area-balancing`), never a bit position. Retiring
or reordering an experiment therefore cannot re-interpret an old file. A key this
build does not know is dropped on load with one line on stderr; the game then
plays without it, which is the documented policy for a save from a build whose
experiment was removed. When an experiment graduates into default behaviour,
remove its entry and its gate together: old saves that named it load and play the
now-default rules.

Required terrain keys are stricter: an unknown required key or unknown terrain ID
rejects the map instead of silently changing its behavior. A save containing
experimental terrain must already carry the matching enabled experiments.

Preferences store the set as `experiments=<key>,<key>` in `preferences.txt`;
unknown keys are dropped there too. Installed building-catalog experiment
definitions must therefore be registered before preferences are loaded. Registration
is deterministic and immutable after startup; loading a saved game's embedded
catalog does not change the installed definitions or the settings page.

Catalog-aware readers supply the validated embedded catalog's experiment keys
when reading its enabled set. Those keys survive even if their definitions have
been removed from the installed catalog. This explicit allowlist does not admit
unrelated unknown keys or teach subsequent games about the embedded definitions.
Built-in keys retain their existing serialized order; dynamic keys follow them
in byte-wise order. The existing limit of 64 enabled keys still applies.

Headless runs: `GLOB2_TEST_RULES` accepts every experiment key as a 0/1 rule for
`-test-games` matches, and `--run-game` takes `--experiment <key>` (repeatable;
the profile's settings do not apply to structured runs). `result.json` lists the
game's experiments. See [headless replays](../development/headless-replays.md).

## Current experiments

| Key | Setting | What it changes |
| --- | --- | --- |
| `guard-area-balancing` | Guard-area balancing | Free warriors spread between painted guard areas by crowding instead of all taking the nearest one. Design and measurements: [guard-area balancing](guard-area-balancing.md). |
| `farm-areas` | Farm areas | A fourth painted area: a harvest inside it draws from the ripest tile of the connected field and keeps one grain on every tile, and wood growing into it is cleared. Design: [farm areas](farm-areas.md). |
| `ice-terrain` | Ice terrain | Enables the ice editor brush. Ice halves ground movement speed and costs an exposed ground unit one HP per 32 ticks; flying units are unaffected. Ice supports neither buildings nor resources. |
| `road-terrain` | Trail terrain | Enables the Trail editor brush. Weathered trails double ground movement speed, permit buildings, and support no resources. Flying units are unaffected. |
| `markets-v2` | Markets V2 | Workers fetch shared market stock; upgrades add wheat and wood, then all resources. [Markets V2](markets-v2.md). |
| `obstacle-terrain` | Obstacle terrain | Boulders, hedge and thicket brushes: impassable on the ground, stop projectiles, passable by fliers; no buildings or resources. |
| `ridge-terrain` | Ridge terrain | Ridge and outcrop brushes: impassable on the ground, but towers shoot over them and fliers pass. |
| `barren-terrain` | Barren ground | Dirt, clay, gravel and flower-meadow brushes: buildable, inhibit growth like sand, never count as shoreline. |
| `rough-terrain` | Rough ground | Mud, marsh, deep-snow and scree brushes: ground units move at 160/256 speed; no buildings or resources. |
| `path-terrain` | Path terrain | Dirt-track and boardwalk brushes with Trail's rules: double ground speed, buildable, nothing grows. |
| `lava-terrain` | Lava terrain | Lava and ember-field brushes: impassable on the ground, fliers lose 64/256 HP per tick, projectiles pass. |
| `fertile-terrain` | Fertile ground | Loam, moss and spring-meadow brushes: buildable crop land that is a fertility source three times as strong as water. |
| `deep-water-terrain` | Deep water | Deep-water and dark-water brushes: swimmable at 192/256 speed, no algae, still a fertility source. |
| `void-terrain` | Void terrain | Hole and chasm brushes: nothing crosses, not even fliers; projectiles stop at the edge. |

Each terrain group is one `TerrainProperties` profile in `src/map/TerrainGroup.h`;
`src/map/TerrainTypeTable.h` lists its members and
[terrain materials](../assets/terrain-materials.md) covers their artwork.

Trail retains the legacy experiment key `road-terrain`, terrain ID `4`, and
external terrain name `road`. Existing preferences, maps, saves, scripts, map
reports and `select road` editor actions keep working. The Trail artwork replaces
the former cobblestone frames without changing movement, ecology or building
rules. Its generated source and classic-frame recipe are recorded in
[`datasrc/gfx/trail/`](../../datasrc/gfx/trail/).

## Adding an experiment

For a building-catalog experiment, declare its stable `key`, English `label` and
`help` in the catalog and reference that key from the gated building definition.
The [tested field-kitchen example](building-catalogs.md#complete-field-kitchen-example)
includes a complete definition and commands for extending a copy of the stock
catalog. No `ExperimentId` or C++ registry entry is needed. Keys use lowercase ASCII
letters, digits and single separating hyphens, up to 128 bytes. Labels and help
must be nonempty. The startup loader registers the installed catalog's definitions
with `registerCatalogExperiments`; built-in keys such as `markets-v2` retain their
existing enum identity and interface text. Duplicate declarations are rejected.

Engine code can query `ExperimentSet::has(key)` for a catalog gate. Keep key
resolution outside simulation hot loops by preparing the game's available
building variants once. When reading embedded catalogs, pass their validated keys
to `ExperimentSet::fromKeys` or `load` instead of registering them globally.
Unknown keys remain subject to the reader's existing ignore/reject policy.
Catalog labels can optionally use the same `[experiment <key>]` and
`[experiment <key> help]` translation keys as built-ins. Cover enabled and disabled
availability, missing local definitions, and saved continuation for new gates.

For a built-in engine experiment:

1. Append an `ExperimentId` before `Count` in `src/game/ExperimentalFeatures.h` and add
   its definition (stable key, English label and help) to the table in
   `src/game/ExperimentalFeatures.cpp`. Keys are lowercase letters, digits and hyphens.
2. For terrain, add the group to `TerrainGroup.h` (one property profile), list its
   types in `TerrainTypeTable.h`, and associate the group with the experiment in
   `TerrainExperiments.h`; the editor gates every member of the group through that
   one switch. Simulation always reads the terrain properties; local
   preferences must never change an existing map cell's behavior. For other features,
   gate the simulation on `game->gameHeader.hasExperiment(ExperimentId::X)` (from a
   unit, `owner->game->gameHeader`). The path with the experiment off must stay
   byte-identical to the game before your change: existing replays and the
   checksum fixtures under `test/maxima/fixtures/` guard this. Consuming
   `syncRand()` differently under the experiment is fine, because the set is baked
   into the game.
3. Add `[experiment <key>]` and `[experiment <key> help]` to `data/texts.keys.txt`
   and to every catalog in `data/texts.list.txt`, with the English text equal to the
   definition's label and help. The `SettingsExperiments` test fails when a registry
   entry's keys are missing from `texts.keys.txt` or the English table differs, and
   `python3 data/check_translations.py --strict` then requires every catalog to
   translate them.
4. Cover both sides of the gate in a doctest case (`test/README.md`): the
   `GuardAreaBalance` suite is the pattern. It starts its games with
   `glob2test::GameOptions::experiments` set, and its first case checks the default
   game's per-100-tick checksums against a golden, so an unintended change to the
   default path fails.
5. Write a short design and measurement guide under `docs/features/` and link it
   from the table above. Adding a registry key does not require a header-format bump. New saved
   simulation state still needs a version-gated format extension. Every simulation
   change, including an experiment, needs a fresh `SIM_REVISION` and regenerated
   golden match record as required by the simulation-version policy.
6. In the pull request, describe the feel changes with the experiment on; a
   maintainer playing it is part of review.
