# Map generation, PNG previews, and JSON reports

The normal **client executable** includes map tools. Build with
`scons release=1 server=0`, then run the examples from the repository root.
An installed client can use the same flags. Previews use the game's existing
`MapThumbnail` and `MapPreview` widget, shared by the lobby and landscape
picker. Exports paint that widget into an offscreen software surface with transitions
disabled. PNGs, maps, and JSON reports need no display or OpenGL. No Python or study
executable is needed. The normal game data directory (including fonts and the GUI
theme for PNGs) must be available. Put the
launch mode first; these modes do not combine with game, replay, or server launch modes.

For the fortified countryside generator, see [Forts](FORTS.md) for its controls,
resource guarantees, supported combinations and validation evidence.

## Generate a map and preview

```sh
build/src/glob2 --generate-map coral --seed 7 \
  --width 256 --height 256 --teams 4 \
  --output artifacts/coral.map --preview artifacts/coral.png
```

`--output` writes a playable `.map` using the normal engine serializer.
`--preview` writes a PNG. `--json FILE` writes a detailed map report. Supply any
combination of these three outputs. See the [JSON format and metric definitions](REPORT.md)
for units, fairness formulas, terrain/resource percentages, and travel distances. Generation uses the production
`GenerationService`, at registered defaults with seed **1** unless specified.
It makes one attempt with exactly that seed; a failed layout returns an error
instead of silently retrying with another seed. The diagnostic includes the
chosen generator, revision, and seed. If `--json` is supplied, service-level failures also
write a `generation_failure` report containing partial telemetry and the error, while still
returning nonzero. Argument/config parsing failures happen before an attempt and do not
promise a JSON report. Determinism has the same platform limits
as the existing generators; a seed alone is not a cross-platform guarantee.

## Preview an existing map or save

```sh
build/src/glob2 --preview-map maps/SomeMap.map --output artifacts/map.png
build/src/glob2 --preview-map /path/to/colony.game --output artifacts/save.png
```

`--json artifacts/report.json` also works here, alone or alongside the PNG.

The file is read through `Game::load`, including its existing save-version checks.
No simulation ticks run and the input is never saved back. Pass a filesystem path,
relative to the working directory or absolute; map names are not searched in the
profile. The PNG shows the complete map, ignoring fog of war, at the saved state.

These use the same terrain/resource colors, thumbnail sampling, aspect-ratio
handling, and numbered colony-start markers as the lobby and landscape picker.
For saved games, markers show the stored colony starts, not current unit positions.
The preview is not a screenshot of the main game view. Renderer changes shared
with those screens also apply to CLI exports; there is no separate CLI renderer.

## Render a game as the player would see it

`--preview-map` draws terrain. When the question is where a colony actually put
its buildings, `--render-game` draws the same picture the game itself draws —
buildings, units, resources and health bars — through `Game::drawMap`, with fog
of war off so every team is visible at once.

```sh
build/src/glob2 --render-game /path/to/colony.game --output artifacts/tick.png
build/src/glob2 --render-game maps/SomeMap.map --output artifacts/map.png \
    --render-max-pixels 1400
```

Tiles are 32 pixels, so a 128-tile map renders 4096 across; `--render-max-pixels`
caps the long edge (default 4096) and scales the result down. Like
`--preview-map`, the file is read through `Game::load`, no simulation ticks run,
and the input is never written back.

Rendering needs the drawing assets that a headless run skips — unit skins and
per-building-type sprites — so `MapRender::ensureAssets` brings them up once
against SDL's dummy video driver. No display is required.

### Painting a scalar field over the render

`--render-field <file>` shades a per-tile value over the render, so a scoring
surface can be checked against the geography that produced it. `--field-color
r,g,b` picks the colour (default `0,192,255`).

The format is deliberately trivial: `width height`, then `width * height`
integers in row-major order. Alpha is each value's share of the field's own
maximum, so a field is readable without the renderer knowing its units, and a
flat field paints flat. Anything that can walk the map can emit one.

```sh
build/src/glob2 --render-game colony.game --output threat.png \
    --render-field threat.field --field-color 255,40,40
```

## Discover and configure generators

```sh
build/src/glob2 --list-map-generators
build/src/glob2 --list-map-generators maze
build/src/glob2 --generate-map maze --set cell-shape=1 --teams 6 \
  --seed 7 --preview artifacts/maze.png
```

Generators accept their stable string ID or numeric legacy ID. The catalog
includes editor-only generators and shows revisions. With an ID, it lists all
shared and generator-specific controls, defaults, allowed values, and choice
labels. Choice labels are the engine's untranslated labels; pass the numeric
value. Toggles use 0 or 1. Unknown keys, malformed numbers, values outside the
allowed domain, and invalid combinations fail with a nonzero exit status.

`--set key=value` is repeatable and exposes every registered control. Shared
controls also have shortcuts: `--width`, `--height`, `--teams`, and `--workers`.
Width and height use **tile counts** (64, 128, 256, 512), including in config
files and `--set`; the study executable's internal exponents are not used here.
`--seed` accepts unsigned decimal integers from 0 through 4294967295.

A config file contains the same keys, plus `seed`:

```ini
# maze.cfg
seed=7
width=256
height=128
teams=6
workers=4
cell-shape=1
```

```sh
build/src/glob2 --generate-map maze --config maze.cfg \
  --seed 42 --set cell-shape=0 --preview artifacts/maze.png
```

Precedence is registered defaults, then config, then CLI settings, regardless
of where `--config` appears. Repeated keys within either source use the last
value. Blank lines, surrounding whitespace, and `#` comments are allowed.
Values are decimal integers, not quoted strings or expressions. The generator
ID and output paths are supplied on the command line, not in the config file.

## Preview options and comparisons

Both generation and file previews support `--preview-scale 2|4|8`, default **2**.
The scale multiplies the retained thumbnail dimensions: one pixel per map tile up
to 512 pixels on the longest axis. Thus a 256×128 map exports at 512×256 by default,
1024×512 at 4×, and 2048×1024 at 8×. A 512×512 map exports at up to 4096×4096.
Maps larger than 512 tiles retain the shared renderer's box-averaged thumbnail.
Scaling enlarges those retained pixels; it does not add game-view sprite detail.
Markers keep the widget's normal pixel size so larger exports reveal more terrain
around them. The full map remains visible; export scale is not interactive zoom.

```sh
build/src/glob2 --preview-map maps/SomeMap.map --output artifacts/map-4x.png --preview-scale 4
build/src/glob2 --generate-map maze --seed 7 --preview artifacts/map-8x.png --preview-scale 8
```

Alternatively, `--preview-size N` (128–4096 pixels) sets the exact longest side
and keeps the map's aspect ratio. It overrides the default 2× sizing; explicitly
combining `--preview-size` and `--preview-scale` is an error. Both options require
a PNG output. The same shared widget handles terrain, centered/wrapped colony
markers, and the map frame at every export size.

Parent output directories are created. Existing output files are replaced;
input/config paths and all output paths must be distinct. Use `-d directory`
(repeatable) to add an asset search directory. Normal profile selection applies (`GLOB2_USER_DIR` on Unix; the working directory
on Windows); these modes do not save preferences or create replays.
`--generate-map --help` and `--preview-map --help` print command usage.

To compare generators at several sizes, invoke the executable for each image:

```sh
for generator in coral spider-web; do
  for size in 128 256 512; do
    build/src/glob2 --generate-map "$generator" --seed 7 \
      --width "$size" --height "$size" \
      --preview "artifacts/comparison/$generator-$size.png"
  done
done
```

This replaces the removed `tools/render_map.py` workflow. Its separate palette
and analysis tints are not used: exports follow the in-game preview renderer.
Batch comparison PNGs are separate files; the executable does not generate the old script's HTML sheet.
The analysis-only `MapGeneratorStudy` remains available for quality measurements,
text-grid dumps, and fairness studies.

## Internal generator telemetry

Generation with `--json` includes generator-supplied measurements, variants and fallback events
at `generation.telemetry`. Collection is disabled for ordinary generation without JSON and for
validation reconstruction. Existing map/save files do not contain this history. The report schema
is version 2; check `report_type` before reading snapshot fields. See [telemetry](TELEMETRY.md)
for the efficient instrumentation API, bulk collector and ad-hoc analysis workflow.
