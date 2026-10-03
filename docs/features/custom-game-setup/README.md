# Custom-game setup

The native lobby has Map, Players & Teams, and Game Rules tabs. The fixed footer
keeps the match summary and launch action available while dense content scrolls.

## Behavior

Matches support up to **16 colonies and 16 controllers**. Shared human/AI control
uses two controller slots for one colony. Individual landscapes can impose lower
limits when their homes, resources or routes need more room.

- Start on a random map with four colonies in a free-for-all: you plus three Numbi AIs
  (since 2026-09-14; the premade library, a tab away, preselects FourSquares1 the first time
  it is opened). A saved lobby restores whichever mode it was left in.
- Unix map libraries separate installed and user roots. Windows/shared-root
  installations show one combined library so shipped maps remain accessible.
- Select a premade map or generate a random world. Random maps start at
  256×256 unless saved settings say otherwise. Random previews appear
  automatically after a 500 ms edit debounce, with generation deferred during a
  drag or open choice menu. The displayed snapshot is the map that launches.
  Randomize, under the preview, rolls the same settings again with a new seed.
- Landscape, at the top of the map column, opens a sheet showing every landscape as
  a real map at the current size and colony count. Regenerate all rolls the sheet
  again with fresh seeds; Randomize parameters rolls it with every landscape's own
  controls drawn at random, redrawing any set the world refuses; Reset to defaults
  puts every landscape back on its registered controls. Use plays exactly the map
  shown: its seed, and the parameters it was rolled with, come back to the lobby.
- Right under the landscape chooser, Reset to defaults returns width, height, colony
  count and the landscape's own controls to their defaults, keeping the landscape
  itself; Random parameters beside it draws every one of the landscape's controls at
  random (size, colony count and workers stay). Some combinations make no map: a
  draw the generator refuses is redrawn on the spot, and one the world refuses is
  redrawn when the preview fails, up to six times, so the first set that generates a
  valid map is the one shown.
- Expand Terrain, Resources and Layout to tune the applicable generator controls.
  Resource amounts are percentages of the landscape's own default (100). On/off
  switches are checkbox rows: click one, or press Space or Return while it has
  focus. Starting workers belong to Game Rules; premade maps retain authored units.
- Under a generated map's preview the lobby shows its start quality: the fairness
  (the worst colony's start over the best) and the score it ranked its candidate
  rolls by, with an (i) that opens the breakdown, one row per colony with what was
  measured (the walk to wheat and to wood, the ground's fertility, deposits and
  building sites within reach, distance from rivals), each factor's score and the
  weights.
- Each colony has a numbered color swatch, controller, AI/difficulty and team.
  You, AI, shared You + AI, and Closed are explicit choices. Shared control uses
  two of the twelve controller records; the UI explains unavailable combinations.
- FFA, 2 vs 2 and You vs all presets preserve explicit alliance state. Reducing
  map capacity retains hidden assignments for a later larger map.
- AI profiles explain strategy, strengths and suggested counterplay. Cortex is
  Medium difficulty. The seven existing AI implementations are retained;
- All-AI matches launch live watching, with whole-map visibility, optional colony
  viewpoints, pause/speed/inspection, and no gameplay orders from the viewer.
- Game Rules starts from a ruleset (Standard, Quick clash, Blitz, Sandbox and the
  others in `data/rulesets.json`) and lets the player tweak any rule from there. The
  tab names the ruleset and how many rules differ from it ("Blitz + 2 changes"),
  marks each changed rule with a dot and a reset, and offers Reset to the whole
  ruleset. Undoing an edit removes the change: the count is derived, not a sticky
  "Custom" state. Summary, the default view, shows the Match rules and any rule set
  away from Standard or from the ruleset; All rules shows every rule in four groups:
  Match, Start, Economy and Combat. Wide layouts list the rulesets beside the rules;
  narrower ones open them as their own screen, and in All rules show one group at a
  time. Turning Combat off disables the combat rules it makes moot, and a setup
  that cannot end (combat off, conquest only, no time limit) shows a warning.
  Session speed is restored when the match ends.
- When the sudden-death timer or prestige goal ends a match with non-allied teams
  tied for the most prestige, the tied players and live watchers see **Draw**
  instead of a win. Allies that win together still see a win, and teams below the
  tie still lose. This is presentation only: the engine still marks every tied
  team as won, so saves, replays and result files are unchanged.
  Colonies nobody plays -- empty or locked seats in an online room, which run as
  AI `none` and stay alive and idle -- never share a win, so one player outlasting
  everyone else still sees a win; the platform records results by the same rule.
- [Experimental features](../experimental-features.md) are not lobby rules: they
  come from Settings → Experiments and are baked into the header of every game the
  lobby starts. When any are enabled the footer summary lists them, so a player
  sees what the match will carry.

The lobby automatically saves choices to `custom-game-settings.txt` in the game's
writable configuration directory, after edits and when leaving or starting a
match. Returning to the lobby or restarting the app restores map mode and library,
premade selection, all generator controls, controller/AI/team assignments
(including hidden colonies), rules and expanded generator sections. Random mode
creates a fresh preview using the saved parameters; temporary maps and seeds are
not stored as preferences. Generator controls that have a field in the legacy
map descriptor are saved there; every other control, including every switch and
resource amount, is saved in an `options` section after it. Files written before
that section existed still load, with those controls at their defaults, and an
option the game no longer has is ignored. Missing premade maps retain the draft
and show the existing load error. Malformed or unsupported settings files, or an
option value outside its control's range, fall back to the normal four-player
setup. Writes replace the old file atomically.

Preference format 3 also retains every economy and combat rule, the starting unit
level and the time limit. Formats 1 and 2 remain readable; they did not store
these choices, so those rules load at their normal defaults. Format 5 adds custom
AI library identities and format 6 probability victory. Format 7 stores the
ruleset's id instead of its English name: older files load "Quick clash", "Open
book" and "Last colony standing" as those rulesets and "Custom" as Standard, keeping
the saved rule values, so they show as changes. An id this build does not have also
loads as Standard rather than discarding the draft. Changing a rule only changes
that control; it must not implicitly toggle another.

## Adding or tweaking a ruleset

Rulesets live in `data/rulesets.json`, in display order. Each entry lists only the
rules it changes from Standard; every other rule keeps its Standard value:

```json
{
  "id": "blitz",
  "name": "[Blitz]",
  "description": "[ruleset blitz description]",
  "rules": { "speed": "2x", "workers": 8, "unitLevel": "veteran", "timeLimit": "45" }
}
```

- `id` is stable: preferences store it, so renaming one resets players who chose it
  to Standard. `standard` must exist, change nothing, and is always listed first.
- `name` and `description` are text keys. Add each to `data/texts.keys.txt`, with
  English in `data/texts.en.txt`, a blank line in every other catalog and the key in
  `data/texts.pending.txt` (see the [UI framework](../../development/ui-framework.md)).
  `data/check_translations.py` fails on a key the file uses but the catalogs lack.
- Rule ids and values come from the registry in `src/CustomGameRules.cpp`. Toggles
  take `true`/`false` (`combat`, `hunger`, `revealTerrain`, `alliancesChange`,
  `instantConstruction`, `woundedRetreat`, `unitsCanDie`, `unitTraining`); `workers`
  takes 1 to 8; the others take an option id: `victory` (`prestige`, `conquest`),
  `timeLimit` (`off`, `30`, `45`, `60`, `90`), `winProbability` (`off`, `95`, `97`,
  `99`), `speed` (`1x` to `40x`, `max`),
  `unitLevel` (`standard`, `veteran`, `elite`, `legendary`), `stockpile` (`none`,
  `50`, `150`, `300`), `regrowth` (`normal`, `slow`, `very-slow`, `rare`, `none`),
  `glassCannon` (`off`, `x2`, `x3`) and `buildingStrength` (`normal`, `x5`, `x10`).
- The game skips an invalid entry and reports why on standard error; a file that
  does not parse leaves Standard alone. The `CustomGameSetup` harness requires the
  shipped file to load without errors and to use every rule in some ruleset.

Rulesets only choose values for rules that already exist, so adding or editing one
changes no simulation code and needs no `SIM_REVISION` bump. Online rooms carry
every rule except game speed, starting workers, unit level and probability victory; a room shows the
first ruleset that matches the rules it carries (Quick clash reads as Standard).
A new *rule* is different: it needs a field in the setup and header, a registry
entry, save and replay versioning, and a simulation version bump.

Save/replay encodings are unchanged. Generated maps use owned temporary snapshots
outside the map library; saves and replays remain self-contained after cleanup.
All 32 non-English language tables include localized lobby labels, AI profiles,
and help text. Older English fallback labels were audited as well. Translations
were machine-assisted, edited in a separate three-agent review, and checked
for terminology, placeholders, paragraph structure, and bundled-font coverage.
They have not received human native-speaker review. Standard key legends,
proper names, and shared vocabulary remain unchanged. A regression test rejects new English
fallbacks outside the documented shared-vocabulary allowlist.

## Reproduce verification

Run from the repository root on a machine with the native dependencies:

```sh
scons -j8 release=1 engine-tests build/src/glob2
python3 test/run_tests.py --filter 'CustomGameSetup/*'
python3 test/run_tests.py --filter 'GameSpeed/*'
python3 data/check_translations.py --strict
python3 test/test_translations.py
python3 test/test_font_coverage.py
```

The custom harness covers model validation and restoration, canonical map catalog
identity, stable selection/scroll/focus, inline choices and dismissal, controller
limits, automatic previews/debounce, failure recovery, exact map serialization,
AI order routing, save/load and replay playback. The UI mode drives production
SDL event loops through human, shared-control and AI-only launches.

The Linux workflow runs the headless harness and native compact UI checks under
Xvfb. macOS desktop event injection was unreliable during development; see
[the automation guide](../../../test/LOBBY_AUTOMATION.md) for the working SDL approach
and the distinction between game-side input coverage and physical OS input.
