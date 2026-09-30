# Experimental features

Experiments are gameplay features we are still testing. They are off by default;
a player switches them on under **Settings → Experiments**, and every game that
player then starts or hosts carries exactly that set for its whole life. This guide
covers what players see, the compatibility rules, and how to add an experiment.

## What players see

- **Settings → Experiments** lists one switch per experiment in this build, with
  a line explaining what it changes. The page says when a build has none.
- The set applies to **new games only**: a custom game, a map file played from the
  load screen, a headless `-test-games` match, and a multiplayer game the player
  hosts on YOG or LAN. A joiner plays with the host's set, whatever their own
  settings say.
- **Campaign missions and the tutorial never take experiments**: scripted content
  plays as its author tested it.
- A **saved game or replay keeps the set it was started with**, even if the player
  has since changed the setting. Loading a save never applies current settings.
- Where the set shows: the custom-game lobby footer ("Experiments: …"), the
  multiplayer lobby's side panel and its **Other options** dialog ("Experiments set
  by the host: …", read-only for every player), and the load screen's details for a
  saved game that carries any.
- Experiments can change balance and pacing. Nothing in the default game changes
  while every switch is off.

## Compatibility

`Engine::applyLocalExperiments` (`src/EngineInit.cpp`) is the one place the
new-game rule lives: it copies the settings into a header unless the map is a saved
game, and every entry point above calls it. A hosted multiplayer game sends its
header with the map, so joiners see the set in the lobby.

The set lives in `GameHeader` (`src/GameHeader.h`) as an `ExperimentSet`
(`src/ExperimentalFeatures.h`), written after the custom-game rules and gated on
save format 124 (`FILE_FORMAT_VERSION_EXPERIMENTS`). It travels in saves, replays,
the network game-header messages and the YOG after-join information, so every
peer of a game runs the same set. Adding the field changed the header's wire
format: network protocol 47 refuses older clients. Replays recorded at format 123
still play, because a header without the section loads as "no experiments" and
the default simulation is unchanged.

Saves, replays and the wire carry each enabled experiment's **key** (a stable
kebab-case string such as `guard-area-balancing`), never a bit position. Retiring
or reordering an experiment therefore cannot re-interpret an old file. A key this
build does not know is dropped on load with one line on stderr; the game then
plays without it, which is the documented policy for a save from a build whose
experiment was removed. When an experiment graduates into default behaviour,
remove its entry and its gate together: old saves that named it load and play the
now-default rules.

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

## Adding an experiment

1. Append an `ExperimentId` before `Count` in `src/ExperimentalFeatures.h` and add
   its definition (stable key, English label and help) to the table in
   `src/ExperimentalFeatures.cpp`. Keys are lowercase letters, digits and hyphens.
2. Gate the simulation on `game->gameHeader.hasExperiment(ExperimentId::X)` (from a
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
   from the table above. No version bump is needed for an added experiment: the
   header format only changes when the framework itself does.
6. In the pull request, describe the feel changes with the experiment on; a
   maintainer playing it is part of review.
