# AI artwork pipeline

This pipeline stages AI-derived sprite candidates and reproduces reviewed finals.
Begin with the [artwork workflow](../../../docs/assets/artwork-workflow.md). Historical trial galleries, model weights and intermediate
inference images are excluded from Git.

## Approved sources and runtime build

`python3 tools/artwork/package_runtime.py` assembles the committed approved
original/AI/material/mask folders into the PNG **source pack** in
`content/data/highres/v1`, without model tooling. It reproduces reviewed source pixels
byte-for-byte. These PNGs are retained for provenance and editing; the client
loads WebP artwork.

The build and release packagers use `tools/package_assets.py` for runtime encoding,
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
materials (`content/data/gfx/terrain-<name>N.png`) are produced by
`tools/artwork/terrain_synth.py` (procedural originals) and
`tools/artwork/export_material.py` (image-generated materials with stored
prompts), both validated by `tools/artwork/validate_material.py`; see
[terrain materials](../../../docs/assets/terrain-production.md#material-production).
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

## DAT2 building candidates

`dat2_upscale.py` stages candidates for the AI-upscaled building frames:
finished, construction and variant frames with their team layers. Recovered
originals, walls and building sites are excluded. It runs transformer SR
models on MPS or CPU, then a deterministic CPU assembly: joint team-layer
unmixing with the classic hue, luminance-only sharpening and back-projection to
the classic. Its docstring lists the steps.

`--variant` picks the models; `MODELS` records each file's hash, URL and licence.
`bhi`, `nomos` and `atd` are the CC-BY-4.0 models by Philip Hofmann (credit him):
4xBHI_dat2_real, 4xNomos8kDAT and 4xRealWebPhoto_v3_atd. Variants use them
alone or as equal-weight ensembles (`bhi-atd`, `bhi-nomos-atd`, ...).
`--weights bhi=0.5,nomos=0.3,atd=0.2` instead runs a weighted ensemble, recipe
`dat2 weighted chain ensemble v1`, with the weights in the provenance and
manifest. Only recipes whose models all allow commercial use can be promoted.
`usv2-bhi` reproduces the approved bench look with 4x-UltraSharpV2, which is
CC-BY-NC-SA; it is reference-only.
Inference needs the separate environment in `requirements-dat2.txt`.

```sh
python tools/artwork/ai/dat2_upscale.py --list-buildings
python tools/artwork/ai/dat2_upscale.py --weights bhi=0.75,atd=0.25 --all-buildings --classic-rev HEAD \
  --models /path/to/models --output /tmp/glob2-dat2 [--device cpu] [--streams 2] [--jobs 15]
python3 tools/artwork/ai/dat2_upscale.py --promote /tmp/glob2-dat2 --dry-run
python3 tools/artwork/ai/dat2_upscale.py --promote /tmp/glob2-dat2 [--only a,b | --skip c]
```

Every finished frame is stored at once in a persistent frame cache
(`<output>.frames`, or `--cache DIR`). Each entry is keyed by the classic
bytes, recipe, weights, model hashes, device and assembly version. A rerun
reuses matching, hash-verified entries and computes only the rest.
`--resume-from DIR` imports finished frames from an earlier staging or
salvage directory. A failing frame does not stop the others: the run
finishes, reports the failures and publishes nothing.
Staging follows the rules above: a fresh directory, all-or-nothing, with
`provenance/<frame>.json` recording the classic and candidate hashes, model hashes,
licences, package versions and recipe. MPS reproduces earlier MPS runs exactly.
CPU inference is deterministic too, but differs from MPS by at most one float16
step. Each MPS stream needs 4–7 GB, so use two streams on a 24 GB machine.
Promotion needs only Pillow and copies the reviewed PNGs byte-for-byte. It writes
base and team layers into `content-src/gfx/production/ai-upscaled/` and
`content/data/highres/v1/`, then updates both manifests, `frames.txt` and
`package.json`. It refuses reference-only variants, and candidates whose classic
source or staged bytes have changed. Afterwards rerun `sharpen_upscales.py`
(it skips `dat2 ` recipes and refreshes its own source hash), then
`runtime_provenance.py` and the checks below.

## TRELLIS meshes for building shadows

Building ground shadows are cast from 3D models of the sprites
([ground shadows](../../../docs/assets/lighting.md#ground-shadows)).
`trellis_meshes.py` sends each building frame (base without its baked shadow,
team layer composited, upscaled 2x) to Microsoft TRELLIS
(`microsoft/TRELLIS-image-large`, MIT-licensed weights) hosted by fal.ai as
`fal-ai/trellis`, one request per seed, about $0.02 and 25-75 s each, in
parallel. It writes GLB meshes and per-request records (endpoint, seed, input
and sprite hashes, mesh hash, request id, timings) to a fresh staging directory
outside the repository; meshes are never committed. The key comes from
`FAL_KEY` or `~/.config/fal/key` and is never printed.

    uv venv --python 3.13 .venv-mesh
    uv pip install --python .venv-mesh/bin/python -r tools/artwork/ai/requirements-mesh.txt
    .venv-mesh/bin/python tools/artwork/ai/trellis_meshes.py --all-buildings --output /tmp/glob2-meshes
    .venv-mesh/bin/python tools/artwork/ai/trellis_meshes.py --frames hosp0b0 --seeds 2 3 4 \
        --output /tmp/glob2-meshes --append                  # retries for gate failures
    .venv-mesh/bin/python tools/artwork/mesh_shadows.py --meshes /tmp/glob2-meshes \
        --out content-src/gfx/shadows/mesh --review artifacts/mesh-review --lengths artifacts/lengths
    python3 tools/artwork/cast_shadows.py                    # registers the masks, offline

`mesh_shadows.py` runs no inference: it aligns, gates and casts the staged
meshes and commits only the masks and their records. Builds and
`cast_shadows.py` never call fal.ai.

## Terrain and water

The committed final tiles contain shared original-based grass and sand,
retained rugged corner transitions and periodic water. Do not run independent
sprite inference over these tiles: it would break their shared boundaries.
Grass and sand now use the original-based refinement recipe in
`tools/artwork/terrain_materials.py`, including retained transition masks and
canonical edges at every mip. Its source material and tuning controls are kept
in `content-src/gfx/derived/terrain-materials-v1`; see the
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
