# AI artwork pipeline

Part 3 adds the approved AI-derived finals. It does not change the 60 recovered
original frames. Historical trial galleries, model weights and intermediate
inference images are excluded from Git.

## Approved sources and runtime build

`python3 tools/artwork/package_runtime.py` assembles the committed approved
original/AI/material/mask folders into the PNG **source pack** in
`data/highres/v1`, without model tooling. It reproduces reviewed source pixels
byte-for-byte. These PNGs are retained for provenance and editing; the client
loads WebP artwork.

SCons and release packagers use `tools/package_assets.py` for runtime encoding,
including HD layers, all atlas mips and the rewritten `frames.txt` index. Release
exports select the smaller of lossless WebP Q75/method 4 and lossy WebP Q90/method
6. Alpha and geometry stay exact; lossy RGB can differ. Lossless exports preserve
all RGBA pixels. The shared encoder pins Pillow/libwebp; do not add a separate
artwork encoder or run model inference during client builds.

To assemble the approved sources and export a complete playable client asset tree:

```sh
python3 tools/artwork/package_runtime.py --runtime-output artifacts/ai-runtime
python3 tools/artwork/validate_runtime.py --export artifacts/ai-runtime
```

Add `--lossless-images` for pixel-exact runtime comparison. `--output` alone
selects a source-pack destination and does not produce a playable client tree.
Source manifests and provenance README files are excluded from runtime bundles.
Experimental terrain beyond the 272 legacy connected tiles uses the shared
terrain compiler and native fallback rather than this HD pack. The catalogue
materials (`data/gfx/terrain-<name>N.png`) are produced by
`tools/artwork/terrain_synth.py` (procedural originals) and
`tools/artwork/export_material.py` (image-generated materials with stored
prompts), both validated by `tools/artwork/validate_material.py`; see
[terrain materials](../../../docs/assets/terrain-materials.md#material-production).
They need only Pillow, not this pipeline's model tooling.

## Generate a future sprite candidate

Use an external Real-ESRGAN NCNN Vulkan executable and `realesrgan-x4plus` models.
Python dependencies: Pillow, NumPy, SciPy. Do not change the original sources.

```sh
python3 tools/artwork/ai/upscale.py --frame inn0b0 \
  --output /tmp/glob2-inn-candidate --prepare-only
python3 tools/artwork/ai/upscale.py --frame inn0b0 \
  --output /tmp/glob2-inn-candidate \
  --executable /path/to/realesrgan-ncnn-vulkan --models /path/to/models
```

This fills invisible RGB, pads the input, performs 4× inference, restores broad
source color and keeps separate native base/team alpha. It writes staging only, to a fresh directory outside the repository or below
`artifacts/`. Existing directories are rejected. Every source layer is validated
before processing, and a complete result is published only after every layer
succeeds; failed inference leaves no partial candidate selection.
Missing layers or unsupported/original-source frames are rejected. This is a
maintainable constrained candidate pipeline, **not a claim of bit-exact recovery
of every historical finishing pass**. The committed finals remain authoritative.
The old selected recipes included per-frame contour repair, alternate-model
mixing and translucent crystal handling. Candidate changes must be reviewed
against the committed approved finals before promotion, especially shadows,
team recoloring and construction fragments. Do not automatically promote them.
Each recipe records classic source hashes, Python image-tool versions and, for
inference, the executable and selected model-file hashes. Retain this record with
the reviewed candidate selection.

## Terrain and water

The committed final tiles contain shared original-based grass and sand,
retained rugged corner transitions and periodic water. Do not run independent
sprite inference over these tiles: it would break their shared boundaries.
Grass and sand now use the original-based refinement recipe in
`tools/artwork/terrain_materials.py`, including retained transition masks and
canonical edges at every mip. Its source material and tuning controls are kept
in `datasrc/gfx/derived/terrain-materials-v1`; see the
[HD artwork guide](../../../docs/assets/high-resolution/README.md).
`tools/artwork/validate_runtime.py` checks 91,136 directed terrain joins and water
edges at each mip. The approved final material tiles and atlas mips are the
reproducible production input; historical generated-water prompts and trials
are not restored.

## Validate a reviewed selection

Run package_runtime.py --check, validate_runtime.py (also with --export for the
built WebP tree), runtime_provenance.py --check,
the registered `HighResolutionIntegration`, `UnitHighResolutionCache`,
`ImageAssets`, `SpriteLoad` and `SpriteSheets` suites. Integration exercises
gameplay/editor rendering across all 16 hues. Source comparisons use
pr_comparisons.py. Recoloring, logical sizes,
software fallback and simulation remain controlled by the foundation PR.

Pipeline regression checks:

```sh
python3 -m unittest discover -s test/build_system -p test_artwork_package.py -v
python3 -m unittest tools.artwork.ai.test_upscale tools.artwork.test_validate_runtime -v
```

The candidate tests require the optional Pillow/NumPy/SciPy environment. They
exercise preparation, constrained alpha, model-dimension failures, failed paired
inference, protected paths and stale-output rejection with a fake model adapter;
they do not establish external model output quality.
