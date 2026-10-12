# Terrain materials

Detailed terrain rendering uses `data/terrain/tileset.json`. Gameplay continues to
use stable `TerrainType` identities and properties. Visual material handles, masks,
texture choices and composed pages are transient; they never enter saves, orders,
checksums or the synchronized random stream.

## Add artwork

A version-3 catalog contains `profiles`, `materials`, `bindings` and optional
`pair_treatments`. Version-1 packs remain readable with their original five-point
contours and sampling behavior; version 2 adds denser contours, edge softness and
world-space bends; version 3 adds per-profile displacement amplitude, pebble
speckle, diagonal bridging and up to sixty-four curves per profile. Version-2
packs parse unchanged and render through the current resolver, which rounds
corners and reads each curve mirrored and negated. Material keys are unique strings. Bindings map semantic terrain
names to material keys. Every paintable built-in terrain in `src/map/TerrainTypeTable.h`
requires a binding: the five legacy names (water, sand, grass, ice and road) plus the
terrain-catalogue names, which `tools/terrain_builtin_names.json` mirrors for the Python
validator. Several names may bind the same material. The catalogue materials behind those
names are produced as described in [Material production](terrain-production.md#material-production). Adding
a visual material does not add gameplay rules. Runtime terrain definitions
reuse a shipped appearance binding with independently resolved simulation properties;
see [map authoring](../map-generators/game-rules-for-map-design.md#authoring-additional-terrain-types) for the JSON import workflow. Runtime
definitions cannot introduce artwork or modify this catalog. Their canonical IDs
remain in maps and saves, while scene snapshots cache the shipped appearance IDs
used by the compositor.

Each material supplies:

- `key`, a stable authoring name, and `sprite`, a `data/`-relative sprite prefix;
- `variants`, an array of `{ "frame": 272, "weight": 1 }` entries;
- `profile`, the key of its boundary family, and `preview`, three overview RGB channels;
- optional `minimap` RGB channels for minimaps and thumbnails (defaults to `preview`);
- optional `animation_frames`, `animation_ticks`, and `animation_stride`;
- optional `edges` (`blend` or `periodic`, see [Periodic edges](terrain-production.md#periodic-edges))
  and `variant_grid`, see [Positional variants](terrain-production.md#positional-variants);
- optional `seam` (version 3) with `height`, `cast_q8`, `cast_width_q8`, `fringe`,
  `fringe_q8` and `fringe_width_q8`, see [Seams](terrain-rendering.md#seams).

Every material, regular water included, is an ordinary opaque tile set. The
retired `ocean` and `backdrop` keys are rejected: water used to be a transparent
hole over a separately scrolled 512×512 image, which kept it from blending with
its variants.

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
than one phase. Every effective frame must exist and remain below 65536.
All cells of a material share the phase, so a texture that moves must move the
same way in every variant. Between phases the renderer linearly crossfades the
current and next textures using the remainder of `animation_ticks`, including
last-to-first at the loop boundary. It keeps two prepared endpoints and one
blended texture per variant, without adding sprite frames. The existing visual
clock and pause behavior are unchanged. Materials with a one-tick phase duration
still select a discrete frame.

## Validate and inspect

```sh
python3 tools/terrain_tileset.py --check
python3 tools/terrain_tileset.py --output artifacts/terrain/compiled --page-size 256
python3 tools/terrain_profile_curves.py --write
python3 -m unittest discover -s tools -p test_terrain_tileset.py
"$(python3 tools/package_assets.py --encoder-python)" tools/artwork/terrain_synth.py --check
"$(python3 tools/package_assets.py --encoder-python)" tools/artwork/validate_material.py --all
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
one-cell roads, bends, holes, mixed junctions, grass/sand/water corner mixes, torus
edges, translucent water borders and fractional zoom. The current gallery case
also writes a 256-tick simulation checksum trace and cold/warm cache timings.
Compare that trace with the same fixture built against the base revision.

## Related guides

- [Production](terrain-production.md): material recipes and provenance.
- [Rendering](terrain-rendering.md): masks, seams, caches and compatibility.
- [Map-owned artwork](custom-map-artwork.md): custom artwork carried with content.

Related: [asset production](README.md).
