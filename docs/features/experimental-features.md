# Experimental features

Experiments are gameplay features we are still testing. They are off by default;
a player switches them on under **Settings → Experiments**, and every game that
player then starts or hosts carries that set plus any experiments required by its
map for its whole life. This guide
covers what players see, the compatibility rules, and how to add an experiment.

## What players see

- **Settings → Experiments** lists one switch per experiment in this build, with
  a line explaining what it changes. The page says when a build has none.
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
  if the local editor switch is off. Ice and road have separate switches.
- Experiments can change balance and pacing. A map without experimental terrain
  does not acquire new terrain when a switch is enabled.

## Compatibility

`Engine::applyLocalExperiments` (`src/engine/EngineInit.cpp`) is the one place the
new-game rule lives: it combines settings and map requirements in a header unless
the map is a saved game, and every entry point above calls it. A hosted multiplayer game sends its
header with the map, so joiners see the set in the lobby.

The set lives in `GameHeader` (`src/game/GameHeader.h`) as an `ExperimentSet`
(`src/game/ExperimentalFeatures.h`), written after the custom-game rules and gated on
save format 124 (`FILE_FORMAT_VERSION_EXPERIMENTS`). It travels in saves, replays
and the match setup every peer of a game starts from, so every peer runs the same
set. Adding the field changed the header's wire
format and introduced network protocol 47. Terrain format 134 adds required
experiments to `MapHeader`; protocol 55 and replay floor 134 separate that terrain
simulation from earlier clients. Scheduled building gradients add saved scheduling
state in format 135. The demand and partial experiments require format 139 and
protocol 57. Current replay acceptance starts at 139 because scripts can observe
the experiment configuration even when an experiment is
disabled. The supported save floor remains 58.

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
unknown keys are dropped there too.

Headless runs: `GLOB2_TEST_RULES` accepts every experiment key as a 0/1 rule for
`-test-games` matches, and `--run-game` takes `--experiment <key>` (repeatable;
the profile's settings do not apply to structured runs). `result.json` lists the
game's experiments. See [headless replays](../development/headless-replays.md).

## Current experiments

| Key | Setting | What it changes |
| --- | --- | --- |
| `guard-area-balancing` | Guard-area balancing | Free warriors spread between painted guard areas by crowding instead of all taking the nearest one. Design and measurements: [guard-area balancing](guard-area-balancing.md). |
| `building-gradient-pipeline` | Scheduled building gradients | Refreshes cached building routes on private immutable snapshots, then publishes them after a fixed delay. First construction remains synchronous. Scheduling and measurement details: [performance telemetry](../development/performance-telemetry.md#scheduled-building-gradient-experiment). |
| `building-gradient-hybrid` | Demand-based building gradients | With the pipeline enabled, keep fewer than four assigned workers per movement class synchronous and lazy. Classification uses saved simulation state; pending deadlines do not change when staffing changes. |
| `building-gradient-partial` | Partial background building gradients | With the pipeline enabled, resolve captured worker positions in the background, then resume private frozen frontiers on demand after publication. Walking parents needed by round trips still finish first. |
| `farm-areas` | Farm areas | A fourth painted area: a harvest inside it draws from the ripest tile of the connected field and keeps one grain on every tile, and wood growing into it is cleared. Design: [farm areas](farm-areas.md). |
| `ice-terrain` | Ice terrain | Enables the ice editor brush. Ice halves ground movement speed and costs an exposed ground unit one HP per 32 ticks; flying units are unaffected. Ice supports neither buildings nor resources. |
| `road-terrain` | Road terrain | Enables the road editor brush. Roads double ground movement speed, permit buildings, and support no resources. Flying units are unaffected. |

## Adding an experiment

1. Append an `ExperimentId` before `Count` in `src/game/ExperimentalFeatures.h` and add
   its definition (stable key, English label and help) to the table in
   `src/game/ExperimentalFeatures.cpp`. Keys are lowercase letters, digits and hyphens.
2. For terrain, associate its ID with the experiment in `TerrainExperiments.h` and
   gate authoring controls. Simulation always reads the terrain properties; local
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
5. Update the relevant guide and the table above. Stable experiment keys do not
   themselves require a new header format. Simulation changes still require a
   `SIM_REVISION` bump and refreshed golden match, including experimental rules.
5. Update the relevant maintained design and measurement guide and link it from
   the table above. Simulation changes require a `SIM_REVISION` bump and an updated
   golden match record. Add version gates when the header or saved state changes.
6. In the pull request, describe the feel changes with the experiment on; a
   maintainer playing it is part of review.
