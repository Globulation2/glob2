# Map generation, PNG previews, and JSON reports

The normal **client executable** includes map tools. Build with
`scons release=1 server=0`, then run the examples from the repository root.
An installed client can use the same flags. Previews use the game's existing
`MapThumbnail` and `drawMapThumbnail` renderer, shared by the lobby and landscape
picker. PNG export needs **OpenGL and a display server**; on headless Linux, use
`LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a` before the command to render through Mesa.
It briefly creates a graphics context, hides its window, exports, and exits.
Generating maps or JSON reports without PNGs, listing generators, and validating
settings need no display. No Python or study executable is needed. The normal
game data directory (including its font for PNGs) must be available. Put the
launch mode first; these modes do not combine with game, replay, or server launch modes.

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
chosen generator, revision, and seed. Determinism has the same platform limits
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

Both generation and file previews support `--preview-size N` (128–4096 pixels,
default 512). This sets the longest side; the image keeps the map's aspect ratio.
The existing OpenGL renderer scales its thumbnail just as it does in the lobby.
The software drawing backend currently lacks the cropped scaling operation; PNG
export does not substitute a different renderer or modify that backend.

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
