# Terrain materials

Detailed terrain rendering uses `data/terrain/tileset.json`. Gameplay continues to
use stable `TerrainType` identities and properties. Visual material handles, masks,
texture choices and composed pages are transient; they never enter saves, orders,
checksums or the synchronized random stream.

## Add artwork

A version-1 catalog contains `profiles`, `materials`, `bindings` and optional
`pair_treatments`. Material keys are unique strings. Bindings map semantic terrain
names to material keys; the five shipped bindings are water, sand, grass, ice and
road. Adding a visual material does not add gameplay rules. A new gameplay type
still requires the stable enum, properties, compatibility descriptor and authoring
experiment registration.

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

## Boundaries and masks

The presentation resolver uses a 16-pixel lattice. Whole-cell terrains fill their
four quadrants; legacy sprites decode into their original TL/TR/BL/BR material
configuration, including the reversed diagonal groups in the sand/water atlas.
A test verifies both profiles against the engine's frozen lookup. The resolver
reads the scene snapshot, never the live simulation.

A boundary profile has `key`, `roughness_q8` (0–512), and `contours_q12`: exactly four
five-point displacement curves. Each curve starts and ends at zero; each point is
an integer within −256…256. The common endpoints keep neighboring patches joined.
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
texture detail. This prevents adjacent variants from exposing rectangular seams.
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

The compiler produces deterministic texture pages, padded mip levels, normalized
native/HD contour tables, source hashes and provenance in `atlas.json`. Page size
is explicit and output is separate from source assets. `compiled_pack` names the
pack's `atlas.json` under its own data subdirectory. The exporter generates and
audits those pages, and the runtime copies texture regions from their metadata.
Whole source pages stay on the CPU; composed view pages fit device texture limits.
Authoring checkouts, edited catalogs and changed source pixels fall back to sprite
sources. Optional HD sources continue through normal HD frame registration. The
exporter validates every referenced native texture before packaging.

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
pixels. Restart the renderer to load edited catalog definitions. View caches hold 16×16-cell composed pages and compare recipes including
the surrounding lattice, so edits update neighboring tiles and wrapped chunks.
The historical `SoftwareTerrainCache` name is retained for benchmark controls,
but the cache also draws GPU pages. Software storage stays bounded by 32 MiB;
GPU pages have a separate 128 MiB budget. HD oversampling falls from 4× to 2× or
1× when necessary to fit the visible pages or the device texture limit. Prepared source pixels are reported
separately by `sourceBytes()`. Cache admission failure uses the same compositor
through the uncached path.

Minimaps and thumbnails read the catalog's compact palette without changing
legacy thumbnail decoding. Map image interchange colors and editor experiment
gates remain semantic metadata.
Keep reference screenshots, benchmark output and temporary compiled tilesets under
`artifacts/`; publish review evidence separately from durable documentation.
