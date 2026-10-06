# Map generation, PNG previews, and JSON reports

The normal **client executable** includes map tools. Build with
`scons release=1 server=0`, then run the examples from the repository root.
Set `GLOB2_BIN` to the executable for your platform (for example,
`export GLOB2_BIN="$PWD/build/darwin/client/release/src/glob2"` on macOS,
`build/linux/client/release/src/glob2` on Linux, or
`build/mingw/client/release/src/glob2.exe` on Windows).
An installed client can use the same flags. Previews use the game's existing
`MapThumbnail` and `MapPreview` widget, shared by the lobby and landscape
picker. Exports paint that widget into an offscreen software surface with transitions
disabled. PNGs, maps, and JSON reports need no display or OpenGL. No Python or study
executable is needed. The normal game data directory (including fonts and the GUI
theme for PNGs) must be available. Put the
launch mode first; these modes do not combine with game, replay, or server launch modes.

For the fortified countryside generator, see [Forts](FORTS.md) for its controls,
resource guarantees, supported combinations and validation evidence.

For a valley built around contested fruit and competing inns, see [Orchard Commons](ORCHARD_COMMONS.md).

For finite opening food and exposed shared wheat, see [The Hungry Marches](HUNGRY_MARCHES.md).

For an asymmetric player-zero siege supporting 3–16 colonies (13–16 on 512×512), see [Encircled Kingdom](ENCIRCLED_KINGDOM.md).
For a deliberately asymmetric woodland island with biscuit-shaped bites, see
[Who Ate the Map?](WHO_ATE_THE_MAP.md).

## Generate a map and preview

```sh
"$GLOB2_BIN" --generate-map coral --seed 7 \
  --width 256 --height 256 --teams 4 \
  --output artifacts/coral.map --preview artifacts/coral.png
```

`--output` writes a playable `.map.gz` using the normal engine serializer inside
a gzip container. A `.gz` suffix is appended unless already present; the example
above writes `artifacts/coral.map.gz`. Preview loading accepts both compressed
files and legacy raw maps/saves, and a bare `.map` or `.game` path prefers an
existing `.gz` sibling.
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
"$GLOB2_BIN" --preview-map maps/SomeMap.map --output artifacts/map.png
"$GLOB2_BIN" --preview-map /path/to/colony.game --output artifacts/save.png
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

## Choose the right image representation

| Representation | Purpose | Contents and usage |
| --- | --- | --- |
| **Categorical map image** (`--map-image`, `--export-map-image`) | Edit geography and recreate a playable map | One pixel per underlying terrain-grid location, fixed terrain/resource colors and white colony markers. Use an image editor without antialiasing, then `--import-map-image`. |
| **Map preview** (`--preview`, `--preview-map`) | Browse, compare and illustrate maps | The lobby's overview renderer, with thumbnail sampling and numbered colony markers. Useful for judging overall geography; its display colors, markers and possible averaging are not the import contract. |
| **Full map render / game-view screenshot** | Inspect how the map actually appears in play | Terrain transitions, resource sprites and their amounts, buildings and units as rendered by the game. Capture the game view or use a dedicated rendering/debugging tool. This CLI does not add a full game-view renderer. |

Increasing `--preview-scale` enlarges a preview's retained thumbnail pixels; it
never turns that preview into a detailed game-view render. Terrain-dump diagnostic
renderers can additionally distinguish mixed shore tiles, but their palettes are
analysis conventions, not categorical import colors.

Categorical images are a deliberately lossy interchange format. Export/import
recreates terrain, resource footprints and colony starts rather than restoring an
entire saved game. Resource quantities and varieties are initialized from the
import seed; existing buildings, running units, alliances and scripts are not
encoded. Keep the original `.map` or `.game` when exact state preservation matters.
For repeatable editing, preserve pixel dimensions, palette colors and distinct
colony markers; pass matching `--width`, `--height`, `--teams`, and a fixed seed.
Seam repair is enabled by default. Use `--image-seam-width 0` when the decoded
geography must remain unstitched; mandatory terrain legality and colony
initialization still apply.

## Discover and configure generators

```sh
"$GLOB2_BIN" --list-map-generators
"$GLOB2_BIN" --list-map-generators maze
"$GLOB2_BIN" --generate-map maze --set cell-shape=1 --teams 6 \
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
"$GLOB2_BIN" --generate-map maze --config maze.cfg \
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
"$GLOB2_BIN" --preview-map maps/SomeMap.map --output artifacts/map-4x.png --preview-scale 4
"$GLOB2_BIN" --generate-map maze --seed 7 --preview artifacts/map-8x.png --preview-scale 8
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
    "$GLOB2_BIN" --generate-map "$generator" --seed 7 \
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

## Import and export flat map images

The client also converts a single opaque PNG into a new playable map. These
images contain terrain, resource types and colony markers. They approximate the
geography of a map; they do not preserve saved-game state, existing buildings,
resource amounts, alliances or scenario scripts. Ordinary previews are unchanged.

```sh
"$GLOB2_BIN" --generate-map forts --seed 7 --teams 4 \
  --map-image artifacts/forts-image.png --output artifacts/forts.map
"$GLOB2_BIN" --export-map-image artifacts/forts.map \
  --output artifacts/forts-image.png
"$GLOB2_BIN" --import-map-image artifacts/forts-image.png \
  --width 256 --height 256 --teams 4 --workers 4 --seed 1 \
  --output artifacts/imported.map --preview artifacts/imported.png \
  --json artifacts/imported.json
```

Exports have one pixel per underlying terrain-grid location, top-left origin,
without frames, labels or shading. Resource pixels imply grass, except algae
which implies water. The palette is:

| Meaning | RGB hex |
| --- | --- |
| Grass | `#008000` |
| Sand | `#F0DC8C` |
| Water | `#0040FF` |
| Wood | `#004000` |
| Wheat | `#FFFF00` |
| Stone | `#808080` |
| Algae | `#00FFFF` |
| Papyrus | `#FF00FF` |
| Cherry | `#FF0000` |
| Orange | `#FF8000` |
| Prune | `#8000FF` |
| Colony marker | `#FFFFFF` |
| Ice | `#BEE1F0` |
| Trail (legacy name `road`) | `#B08A62` |

Ice and Trail are whole-cell materials. Import retains them after legacy shore
repair and records their required terrain experiments in the map; adjacent water
is not converted into sand. Their colors are distinct from resource colors.

The terrain catalogue types export with the `image` colour of their row in
`src/map/TerrainTypeTable.h` (boulders `#585A5C`, hedge `#264E28`, thicket `#364222`,
ridge `#626870`, outcrop `#6E706C`, dirt `#80684A`, clay `#9E7054`, gravel `#8A8276`,
flower meadow `#3C7E30`, mud `#604C38`, marsh `#4A604E`, deep snow `#DEE4EC`, scree
`#767A80`, dirt track `#AA8E68`, boardwalk `#A48054`, lava `#BE501E`, ember field
`#5A3426`, loam `#4A3E28`, moss `#2E602A`, spring meadow `#5C963A`, deep water
`#0A3CA0`, dark water `#0E264E`, hole `#0A0A0E`, chasm `#18121A`). Import recognises
them **only by exact colour**: every other pixel classifies by nearest colour among
the classic entries above, so existing images never acquire catalogue terrain or
its experiment requirements by accident. Imported catalogue cells record their
group's experiment like ice and trail do.

Import defaults to 256×256, four workers per colony and seed 1. Width/height
accept 64, 128, 256 or 512 tiles. Input dimensions may differ, but the aspect
ratio must match and each input axis must be at most 8192 pixels. Every pixel
must be fully opaque. Source pixels are classified by squared RGB distance to
the palette; each target cell takes the majority category in its source box.
Ties use palette order in the table. Smaller images repeat source samples.

Exported colony markers are solid 7×7 white squares centered two tiles right and
down from the swarm anchor. Import joins white cells with eight-neighbor
connectivity, including across opposite edges; the unwrapped component centroid,
rounded to the nearest integer, minus two tiles determines its preferred swarm
anchor. Components smaller than four target cells are ignored. There must be
1–16 markers. `--teams` optionally asserts the detected count; it does not add
colonies. Team order follows component discovery in row order.

Before engine shoreline correction, the importer repairs terrain in a narrow
strip on each side of both wrap seams. It interpolates signed terrain distances
along opposing cross-sections, using integer weights and a shared midpoint for
the two edge cells. Interpolation is skipped on already matching cross-sections, even when another
part of the same edge differs. The strip defaults to the
shorter map dimension divided by 32, clamped to 2–12 tiles (8 at 256×256).
`--image-seam-width 0..16` overrides it; zero preserves decoded edge terrain.
The two axes are processed in order, using a shared corner profile. After shore
correction, differing unprotected edge cells are reconciled to sand (all four
corner cells together), preventing scan-order shore differences. The square within 20 tiles of each colony anchor is protected from interpolation,
edge reconciliation and resource relocation. Mandatory shoreline legalization
can still alter terrain or drop illegal resources in that region. This local heuristic can move shorelines or change
crossings; it does not guarantee continuity for large feature offsets or preserve
route topology. Engine shores can change cells just beyond the strip.

After terrain rebuilding, legal wood, wheat and algae footprints are interpolated
across the same seam strip. Stone, fruit and papyrus deposits remain fixed.
Opposite unprotected resource edge cells are reconciled. The pass preserves each
resource type's legal tile count **after** terrain legality filtering; it does not
preserve exact stored amounts, since amounts are inferred afterward. Surplus
resource labels are removed first, then missing deposits are restored on matching
terrain, preferring their original positions or the fringe of an existing patch.
Balancing cannot alter locked edge groups or protected colony supplies. It is
restricted to initially mismatched cross-sections on either axis, leaving
unrelated aligned sections untouched. If the greedy pass fails to restore a
budget in the available strip, all resource stitching is rolled back; terrain
repair remains. This fallback does not prove that no valid arrangement exists. Protected or fixed deposits may leave
resource mismatches at the edge. Zero seam width disables both stitching passes.

Only the 7×7 patch around each recovered anchor is cleared and set to grass.
Overlapping patches fail. Engine shoreline correction and terrain rebuilding
can alter boundaries; pending resources that cannot legally occupy their final
tiles are dropped. The swarm and workers must fit within that patch, or import
fails. The importer does not add starter supplies or guarantee connections between colonies. Imported
resources use seeded amounts from 1 through the resource type's `sizesCount - 1`,
as normal generator deposits do. Dense wood/wheat interiors use amounts 2–4;
fringes also include amount 1. Seed determines amounts, sprite varieties and
settlement placement. Thus importing an image need not produce a
balanced or sustainable game.

`--output` is required for image import/export. Import writes normal gzip maps
and supports normal preview and JSON outputs. Its JSON report adds
`image_import` with marker counts, ignored marker components, terrain changes,
cleared resources, dropped resources, `seam_width` and `seam_terrain_changes`
(terrain cells changed by interpolation before engine shores), and
`seam_shore_changes` (edge cells reconciled to sand after shore correction),
`seam_resource_changes` (legal resource labels changed by stitching), and
`resource_seam_fallback` (true when resource stitching was rolled back). Import failures return nonzero, write
no map, and, if requested, write an `image_import_failure` JSON containing those
counts; the error reason is printed on stderr. Terrain changes count underlying
cells differing from the decoded image, including colony clearing and shores.
No display or network access is needed for image conversion.

Run `python3 test/test_map_image.py` to verify the conversion contract. Fixtures,
exported images, maps and command logs are retained under `artifacts/map-image/`.

### Online AI authoring

The optional online AI Map Studio uses this same categorical importer and normal
map serialization. Its maintained offline Python modules live under
`platform/packages/map-studio/python`; provider calls belong to the durable online
worker. Legacy `map_image_examples.py` and `map_image_crop.py` entry points retain
the original square four-colony workflow. `studio_pipeline.py` generalizes
reference preparation, carrier geometry and crop selection for the studio's
128/256/512-cell sides and 2–8 colonies. Every rectangular tile repeats 2×2 in a
specified region of a square carrier; padding is excluded before decoding.

Revisions edit an exact repetition of the selected imported categorical map.
Crop selection keeps complete colony markers and scores resource/terrain seams;
it is not a playability certificate. Online delivery additionally validates the
native post-import report. All delivered files remain ordinary maps requiring no
model or service connection to play.

## Render a whole game

```sh
"$GLOB2_BIN" --render-game colony.game.gz --output artifacts/colony.png \
  --render-max-pixels 2048
"$GLOB2_BIN" --render-game colony.game.gz --output artifacts/threat.png \
  --render-field threat.field --field-color 255,40,40
```

This renders terrain, resources, units and buildings through the shared Scene
renderer, with fog disabled and animation frozen. It accepts maps and all save
formats supported by the normal loader. It does not execute ticks or modify the
input or profile preferences. Game artwork must be available; the command needs
no display, audio device or OpenGL. Software export follows the software game's
presentation, including its cloud-layer behavior.

`--render-max-pixels` limits the longest side to 1–8192 pixels (default 4096),
without upscaling. The final surface is allocated at that size and world geometry
is scaled directly into it; no full-resolution map canvas is allocated. The
maximum square output surface is 256 MiB, separate from assets and Scene storage.
Output replaces the destination only after PNG encoding and close succeed.

A `.field` starts with positive `width height`, then exactly `width * height`
row-major signed 64-bit integers. Dimensions must match the map; up to 16,777,216
values are accepted. Positive values use alpha proportional to the maximum,
capped at 220; zero and negative values are transparent. `--field-color r,g,b`
accepts components 0–255 and defaults to `0,192,255`. Missing values, extra tokens,
overflow and invalid dimensions are errors. A separately supplied saved game must
represent the field's geography; automatic [Maxima field captures](../ai/telemetry.md#maxima-placement-fields)
retain their own matching Scene for PNG output.
