# Custom-game setup

The native lobby has Map, Players & Teams, and Game Rules tabs. The fixed footer
keeps the match summary and launch action available while dense content scrolls.

## Behavior

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
- Rules expose victory, terrain visibility, alliance changes, pace and generated
  workers. Standard, Quick clash, Open book and Last colony standing are visible
  presets. Session speed is restored when the match ends.

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
scons -j8 release=1 custom-setup-test speed-tests build/src/glob2
build/src/CustomGameSetupHarness
mkdir -p artifacts/custom-game/compact artifacts/custom-game/large
build/src/CustomGameSetupHarness artifacts/custom-game/compact
build/src/CustomGameSetupHarness artifacts/custom-game/large large
build/src/CustomGameSetupHarness artifacts/custom-game/compact ui
build/src/CustomGameSetupHarness preferences-write
build/src/CustomGameSetupHarness preferences-read
python3 test/run-game-speed-tests.py
python3 data/check_translations.py --strict
python3 test/test_translations.py
python3 test/test_font_coverage.py
python3 test/test_text_area_layout.py
```

The custom harness covers model validation and restoration, canonical map catalog
identity, stable selection/scroll/focus, inline choices and dismissal, controller
limits, automatic previews/debounce, failure recovery, exact map serialization,
AI order routing, save/load and replay playback. The UI mode drives production
SDL event loops through human, shared-control and AI-only launches.

The Linux workflow runs the headless harness and native compact UI checks under
Xvfb. macOS desktop event injection was unreliable during development; see
[the automation guide](../../test/LOBBY_AUTOMATION.md) for the working SDL approach
and the distinction between game-side input coverage and physical OS input.
