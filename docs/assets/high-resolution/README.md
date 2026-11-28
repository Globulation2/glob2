# High-resolution artwork pack

The current [manifest](../../../content/data/highres/v1/manifest.json) registers 3,665 frames.
The pack contains the organized artist originals, 60 verified original-derived
world frames, and 2,016 unit animation poses rendered from the original Blender
rigs. The pack also includes reviewed AI-enhanced sprites, original-based terrain and generated water,
resampled masks, and complete padded resource/terrain mip atlases.
All overlapping original-derived frames remain byte-identical.

World artwork includes both hives, 3 flags, 5 construction frames, 10 trees, 10 wheat (two ripe frames recoloured from stage 4 by
`tools/artwork/ripen_wheat.py`), 5 papyrus,
completed middle school, first two racetracks and 24 area markers, plus the
hand-authored farm-area marker (8 frames drawn as SVG in `content-src/gfx/authored`).
The 12 carried-material icons (`ressourcemini`, drawn beside a harvesting unit
and in the unit panel) are SVG too, drawn in the style of each material's map
resource. Their classic icons stay as they are; `render_authored.py --check`
keeps each icon's downscaled mean colour within CIEDE2000 2 of its classic
icon, or within 12 for stone and the later materials, whose classic icons are
far duller than their map art.

Assemble approved PNG sources: `python3 tools/artwork/package_runtime.py`.
Client builds encode those sources with the shared WebP pipeline. For a complete
runtime export, use `python3 tools/artwork/package_runtime.py --runtime-output
artifacts/ai-runtime`; add `--lossless-images` for exact RGBA comparisons.
Release exports use the shared Q90/lossless size-selection policy.
Validate: `python3 tools/artwork/validate_runtime.py` and the original family
validators under tools/artwork. Sources and staging exports are preserved in
content-src/gfx; production/original-derived contains approved final layers.
Validate the built WebP layers, atlases and index with
`python3 tools/artwork/validate_runtime.py --export artifacts/ai-runtime`.
The HD terrain atlas covers 272 legacy connected tiles. The catalogue materials
(`terrain-<name>N`) ship standalone 4× frames in the `procedural-materials/`
production folder; `tools/artwork/terrain_synth.py` writes and registers them
together with the classic tiles. Catalog ice now has procedural HD artwork;
the old connected ice frames and legacy cobblestone sprites retain native fallback.
Every AI-upscaled base layer then goes through `tools/artwork/sharpen_upscales.py`:
resource frames get a crisp outline and sharpened shading, while buildings, walls
and effects get colour sharpening only, so soft shadows, smoke and glow survive.
Team-colour layers stay unchanged. It reads its inputs from Git, so rerunning it
reproduces the committed pixels, then rebuilds the resource atlas.
Buildings, trees, fruit trees, wheat and rocks then get ground shadow layers
(`<frame>s`, production folder `shadows/`) from `tools/artwork/cast_shadows.py`,
which also removes the shadows the old art baked into those base layers
([ground shadows](../lighting.md#ground-shadows)). Its specs are
`content-src/gfx/shadows/specs.json`. It runs last, after anything that rewrites
base layers, and reruns without changes:

    python3 tools/artwork/sharpen_upscales.py   # only when rerun: rewrites sharpened bases
    python3 tools/artwork/ripen_wheat.py        # only when rerun: rewrites the ripe wheat
    python3 tools/artwork/cast_shadows.py       # de-shadow, cast, register, resource atlas
    python3 tools/artwork/package_runtime.py --check
    python3 tools/artwork/runtime_provenance.py
    python3 tools/artwork/validate_runtime.py

Promoting new base or team layers for a frame with a shadow spec (an AI
upscale, say) must be followed by `cast_shadows.py`: its provenance records the
base, team and classic hashes it cast from, and `validate_runtime.py` checks
that de-shadowed bases keep no baked shadow and that the classic shadow is the
HD one area-averaged.
AI candidates are generated into fresh staging directories only; see the
[candidate pipeline](../../../tools/artwork/ai/README.md). Production packaging
reproduces the committed reviewed finals without model inference.

HD grass uses **C refined**, and sand uses the same approved refinement method.
Both are reconstructed from the classic 32px tiles with
periodic interpolation, connected grain and a soft pull toward each original
pixel's color. It preserves the original palette and broad patches while adding
detail at 128px; downscaling stays close rather than requiring exact pixels.
The selected materials, tuning parameters and retained transition masks live in
`content-src/gfx/derived/terrain-materials-v1`. Grass/sand weights were recovered from
the previous approved composites. Sand/water masks are their exact shoreline
alpha channels; shoreline alpha also stays unchanged at every atlas mip. The
historical revision and input hashes are recorded in `recipe.json`. Pure water
frames and the animated water material keep their existing pixels.

Rebuild with `python3 tools/artwork/terrain_materials.py`, then run the shared
packager above. Use `python3 tools/artwork/terrain_materials.py --check` to verify
the retained recipe, 16 variants per material, 224 connected transitions and their
four atlas mip levels without writing. Each level shares canonical edges and
corners; the source tiles receive a one-pixel edge correction so terrain joins
stay exact. The selected material's interior stays unchanged. This recipe uses
Pillow and NumPy, with no AI inference. Changing the reference material requires
an explicit new selection and matching source hashes. Approved references are
`grass.png` and `sand.png`; each variant uses its own classic tile as the color
target. Production finals live under `content-src/gfx/production/original-materials`.

The shared controls in `recipe.json` are:

| Control | Purpose |
| --- | --- |
| `seed` | Repeatable grain placement. |
| `smoothing` | Blend the original pixel colors toward periodic bilinear interpolation. |
| `connected_grain` | Strength of connected detail sampled from a two-HD-pixel noise lattice. |
| `fine_grain` | Strength of independent fine grain. |
| `original_pull` | Fraction of each 4×4 block's mean color drift removed. |
| `max_block_color_drift` | Limit the retained floating-point drift before rounding and channel clipping. |

Noise is shared across RGB channels to preserve the palette. The drift limit is
a soft reconstruction control, not a promise of a maximum downscale error:
rounding, clipping near zero/255 and the downscale filter affect the final result.
Compare the native tile, 128px candidate and a 32px reduction when tuning. Keep
source references, masks, recipe hashes and generated provenance together.
Recipe regression checks run with
`python3 -m unittest tools.artwork.test_terrain_materials -v`.

Unit textures render onto a fixed 128×128 pixel canvas (4× for the 32px-native
explorer set, ~3.37× and 3.2× for the 38px/40px-native worker and warrior sets),
while the native 32-pose sprites remain in `content/data/gfx`. This preserves logical sprite size,
32 poses per direction. Normal simulation speed is 30 ticks per second
([engine timing](../../../src/engine/EngineTiming.h)); pose count does not set
the display refresh rate. The seven sets
cover explorer flight, worker walk/swim/harvest-build, and warrior walk/swim/fight.
The classic artwork setting and software backend retain native unit textures.
See [the unit pipeline](../../../tools/unit-animation/README.md) for reproducible
render settings, layer mapping, CPU limits and validation.

Related: [artwork workflow](../artwork-workflow.md), [asset production](../README.md) and the [generated inventory](ASSET-PROVENANCE.md).
