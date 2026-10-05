# Terrain materials

Detailed terrain rendering uses `data/terrain/tileset.json`. Gameplay continues to
use stable `TerrainType` identities and properties. Visual material handles, masks,
texture choices and composed pages are transient; they never enter saves, orders,
checksums or the synchronized random stream.

## Add artwork

A version-1 catalog contains `profiles`, `materials`, `bindings` and optional
`pair_treatments`. Material keys are unique strings. Bindings map semantic terrain
names to material keys; the five shipped bindings are water, sand, grass, ice and
road. Adding a visual material does not add gameplay rules. Runtime terrain definitions
reuse a shipped appearance binding with independently resolved simulation properties;
see [map authoring](../map-generators/GAME_RULES_FOR_MAP_DESIGN.md#authoring-additional-terrain-types) for the JSON import workflow. Runtime
definitions cannot introduce artwork or modify this catalog. Their canonical IDs
remain in maps and saves, while scene snapshots cache the shipped appearance IDs
used by the compositor.

Each material supplies:

- `key`, a stable authoring name, and `sprite`, a `data/`-relative sprite prefix;
- `variants`, an array of `{ "frame": 272, "weight": 1 }` entries;
- `profile`, the key of its boundary family, and `preview`, three overview RGB channels;
- optional `minimap` RGB channels for minimaps and thumbnails (defaults to `preview`);
- optional `animation_frames`, `animation_ticks`, and `animation_stride`;
- optional `backdrop` with `sprite`, `first_frame`, `frames`, and `ticks`;
- `ocean: true` only for materials that reveal the shared scrolling ocean.

Logical frames are 32×32. Existing HD frame registration supplies higher resolution
source textures when available; missing HD artwork uses native pixels. Positive
integer weights select interior variants deterministically from canonical cell
coordinates and the material key. Reordering material definitions does not reseed
variation. Changing a key deliberately changes its visual seed.

To add more variety to an existing terrain, add frames and weights to that
material. To replace its appearance, add a material and change the corresponding
binding. These edits require no new renderer switch statement or save migration.
The shipped catalog reuses the existing sixteen interior variants per terrain. The
`road` binding is terrain ID 4, now named Trail by the engine. This pack retains
the requested cobblestone appearance in `data/gfx/terrain-cobblestone0…15.png`,
copied unchanged from frames 288–303 at revision
`e1634ecda9a2a2d31f47dfe766ddbcb40e364791`. The engine's newer dirt Trail textures
remain in those original saved-frame slots. To use them visually, set the `road`
material's sprite back to `data/gfx/terrain` and its variants to frames 288–303;
no engine, save or renderer change is needed.

### Example: change ice artwork without adding gameplay rules

Add `data/gfx/tundra0.png` and `data/gfx/tundra1.png`, both 32×32 RGBA images.
Append this entry to the catalog's `materials` array, reusing the existing
`fractured` profile key:

```json
{
  "key": "tundra",
  "sprite": "data/gfx/tundra",
  "profile": "fractured",
  "preview": [155, 205, 220],
  "minimap": [170, 210, 225],
  "variants": [
    {"frame": 0, "weight": 3},
    {"frame": 1, "weight": 1}
  ]
}
```

Set `bindings.ice` to `"tundra"`, keeping the other required bindings. The first
variant is selected about three times as often as the second over a large map;
selection is stable for a coordinate and material key. The first variant also
supplies the family's common texture border, so choose a representative texture.
To add a third variant, create `tundra2.png` and append its frame and positive
weight. Sprite prefixes use the normal contiguous frame sequence starting at 0;
a material can reference a subset of that loaded sequence.

Material keys and profile keys must be nonempty and unique in their respective
arrays. Frame indices are 0–65535; individual weights are 1–1,000,000 and their sum
must not exceed 1,000,000,000. RGB channels are integers 0–255. The compiler checks
all required and additional bindings against the registered materials. Paths must
remain under `data/`, with forward slashes, canonical segments (no empty, `.` or
`..` segments), and no drive prefixes.

For animation, a variant's effective frame is `frame + phase * animation_stride`.
`animation_frames` is 1–256, `animation_ticks` is a positive integer duration in the
renderer's animation clock, and the stride must be positive when there is more
than one phase. Every effective frame must exist and remain below 65536. Backdrops
have their own consecutive `first_frame`, `frames` and `ticks` fields and are
composited beneath the material before border preparation. The water binding must
use `ocean: true`; ocean materials reveal the shared scrolling ocean instead of
using a per-material backdrop.

## Boundaries and masks

The presentation resolver uses a 16-pixel lattice. Whole-cell terrains fill their
four quadrants; legacy sprites decode into their original TL/TR/BL/BR material
configuration, including the reversed diagonal groups in the sand/water atlas.
A test verifies both profiles against the engine's frozen lookup. The resolver
reads the scene snapshot, never the live simulation. `PreparedCoverage` resolves
the nine patches needed by a tile once, including shared contour choices and
side-connected corner groups, then samples them at native or HD pixel centers.

A boundary profile has `key`, `roughness_q8` (0–512, where 256 is a multiplier of 1),
and `contours_q12`: exactly four
five-point displacement curves. Each curve starts and ends at zero; each point is
an integer within −256…256 in normalized units of 1/4096 of a lattice patch.
These are displacement controls, not pixel coordinates. The common endpoints
keep neighboring patches joined.
The runtime interpolates these curves in normalized coordinates, so native and HD
renders use the same shape. Boundary support remains within the eight-pixel band
between quadrant centers. Center regions and narrow roads remain visible.

Shared edge keys include canonical wrapped coordinates, orientation and stable
material keys. Both sides choose the same contour. Four corner materials resolve
jointly to normalized coverage, including diagonal, concave and multi-material
junctions. Ocean coverage becomes transparency; other textures use straight alpha
only after coverage and source alpha have been combined. Feathering is narrow,
not a broad blur over the square boundary.

By default the rougher boundary profile wins, with a stable key tie break. Optional
`pair_treatments` entries contain `a`, `b` and `profile`; these choose another
contour family for that pair without changing material connectivity. Pairs are
unordered, unique and must reference existing materials and profiles. Most
materials need no pair entries.

Textures are kept separate from boundary masks. Runtime preparation blends each
material's variant borders toward one periodic master while keeping interior
texture detail. The four-pixel border blend operates on premultiplied RGBA, so both
color and opacity agree across variants without introducing dark transparent fringes.
The compositor does not bake a texture-by-mask-by-material-pair product.

## Validate and inspect

```sh
python3 tools/terrain_tileset.py --check
python3 tools/terrain_tileset.py --output artifacts/terrain/compiled --page-size 256
python3 -m unittest discover -s tools -p test_terrain_tileset.py
python3 test/run_tests.py --filter 'TerrainMaterials/*'
python3 test/run_tests.py --filter 'TerrainPresentation/*'
python3 test/run_tests.py --filter 'TerrainValidation/*'
```

The pipeline has three responsibilities:

1. **Catalog/compiler:** validate the schema and native sources, prepare common
   variant borders, then emit paged texture metadata, contour tables and source
   provenance. Page sizes are powers of two from 64 to 8192; the default is 1024.
   Each 32×32 source has four pixels of padding and three exported mip levels.
2. **Runtime asset exporter:** encode the game's sprite sources, generate the
   terrain pack and audit all output files. It records fingerprints of the actual
   decoded runtime sources, including lossy WebP and sheet frames, separately
   from original PNG provenance hashes.
3. **Runtime compositor:** verify native source fingerprints, read prepared texture
   regions lazily from CPU pages, resolve coverage and compose visible terrain.
   It computes coverage directly from normalized profile controls. Exported
   contour tables and source mips are validation/packaging data; runtime source
   extraction currently reads mip 0. Composed view pages supply their own GPU
   mipmaps and respect the device texture limit.

`compiled_pack` names `atlas.json` in its own data subdirectory, such as
`data/terrain/my-pack/atlas.json`. An absent pack,
an edited catalog, a pack without source fingerprints, or native source pixels
that differ from the recorded runtime export use normal sprite preparation.
This includes user artwork overrides already loaded when the renderer starts.
Present but malformed metadata or missing referenced page images are errors;
regenerate the pack rather than relying on a silent partial load. Optional HD
sources continue through normal HD frame registration and preparation; the native
pack does not replace them. The portable backend loads standalone HD frame sources
and uses the same CPU terrain composition. The optional legacy packed-HD sprite
atlas optimization remains OpenGL-specific.

The standalone command writes a pack for unencoded authoring sources. Normal
release/browser packaging generates fingerprints for that export automatically;
do not transplant a standalone pack into a lossy runtime export. Keep compiler
output under `artifacts/`, away from source assets. To inspect a changed catalog,
validate and rebuild the runtime assets, then restart the renderer. In-process
source surface revisions invalidate cached pixels; catalog file edits do not
trigger a live catalog reload.

Review the gallery at normal play scale and enlarged detail: isolated cells,
one-cell roads, bends, holes, mixed junctions, legacy shore orientation, torus
edges, translucent water borders and fractional zoom. The current gallery case
also writes a 256-tick simulation checksum trace and cold/warm cache timings.
Compare that trace with the same fixture built against the base revision.

## Caches and compatibility

`TerrainCompatibility.h` freezes old frame ranges, corner semantics and the frame
hash used by map authoring. Legacy lookup keeps its synchronized random calls.
Changing visual variants in the catalog cannot change those contracts.

`TerrainVisual::Compositor` owns prepared material sources. Source lifetime and
content revisions, animation phase and native/HD selection invalidate prepared
pixels. View caches hold 16×16-cell composed pages and compare recipes including
the surrounding lattice, so edits update neighboring tiles and wrapped chunks.
Each page and tile tracks revisions only for materials used by its discovered
recipes; an animation outside that dependency set does not rebuild the page.
Changes to the overall native/HD sampling density still invalidate view pages.
The historical `SoftwareTerrainCache` name is retained for benchmark controls,
but the cache also draws GPU pages. Software storage stays bounded by 32 MiB;
GPU pages have a separate 128 MiB budget. HD oversampling falls from 4× to 2× or
1× when necessary to fit the visible pages or the device texture limit. Prepared
source pixels are reported separately by `sourceBytes()`.

When the full view cannot stay cached, rendering streams one temporary canonical
page at a time. It uses the same sampling density, opaque runs and mip neighborhoods
as the cached view, including fractional zoom and maps smaller than one page.
If even one page cannot be allocated or fit the device limit, rendering falls back
to a reusable composed tile. This emergency path preserves coverage and deterministic
artwork, but fractional resampling and HD mip filtering can differ from page rendering.
It is a quality fallback, not a pixel-identical cache replacement.

Minimaps and thumbnails read the catalog's compact palette for built-ins and the
embedded resolved colors for runtime types, without changing legacy thumbnail
decoding. Map image interchange colors and editor experiment gates remain semantic
metadata.
Keep reference screenshots, benchmark output and temporary compiled tilesets under
`artifacts/`; publish review evidence separately from durable documentation.
