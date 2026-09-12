# AI artwork pipeline

Part 3 adds the approved AI-derived finals. It does not change the 60 recovered
original frames. Historical trial galleries, model weights and intermediate
inference images are excluded from Git.

## Production build

`python3 tools/artwork/package_runtime.py` assembles the committed approved
original/AI/material/mask folders into the runtime pack, without model tooling.
This reproduces the reviewed finals byte-for-byte.

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
source color and keeps separate native base/team alpha. It writes staging only.
Missing layers or unsupported/original-source frames are rejected. This is a
maintainable constrained candidate pipeline, **not a claim of bit-exact recovery
of every historical finishing pass**. The committed finals remain authoritative.
The old selected recipes included per-frame contour repair, alternate-model
mixing and translucent crystal handling. Candidate changes must be reviewed
against the final before/after gallery before promotion, especially shadows,
team recoloring and construction fragments. Do not automatically promote them.

## Terrain and water

The committed final tiles contain shared generated materials, subdued contrast,
rugged compatible corner transitions and periodic water. Do not run independent
sprite inference over these tiles: it would break their shared boundaries.
`tools/artwork/validate_runtime.py` checks 91,136 directed terrain joins and water
edges at each mip. The approved final material tiles and atlas mips are the
reproducible production input; discarded prompts/trials are not restored.

## Validate a reviewed selection

Run package_runtime.py --check, validate_runtime.py, runtime_provenance.py --check,
the gameplay/editor integration harness, and test/RuntimePackCheck.cpp across
all 16 hues. Final comparisons use pr_comparisons.py. Recoloring, logical sizes,
software fallback and simulation remain controlled by the foundation PR.
