# Approved artwork inputs

These directories contain only the selected final artwork used to assemble the
HD runtime pack. The separation is physical, not just a manifest label.

| Directory | Contents |
| --- | --- |
| `original-derived/` | 60 frames exported from preserved originals; no AI |
| `ai-upscaled/` | 116 frames enlarged/refined with AI and constrained finishing |
| `ai-materials/` | 273 terrain/water frames using generated materials |
| `resampled-masks/` | 38 frames resized deterministically without AI |
| `atlases/` | Prebuilt padded mip atlases; resources combine multiple source categories |
| `pack-metadata/` | Runtime manifest, logical sizes, recipes and frame lookup |

Untouched artist files remain in `../originals/`, `../reference-exports/` and
`../concept-art/`. Intermediate deterministic original exports are in `../derived/`.
Historical trials and generation tooling remain in `../../../experiments/ai-upscale/`.
They are not inputs to normal production packaging. That archive can contain
historical copies of approved results as well as rejected alternatives.

`data/highres/v1/` is the assembled game output. It deliberately has a flat layout
required by the existing loader. It is not the canonical editing location.
`data/gfx/` remains the classic runtime fallback. Packaging never modifies it.

## Build or validate (Python standard library only)

```sh
python3 tools/artwork/package_runtime.py --check
python3 tools/artwork/package_runtime.py
```

All inputs are hash-checked before copying. Frames, paired layers and atlas levels
must be complete. `package.json` maps the separated inputs to runtime filenames.
The runtime manifest preserves the original source/output hashes and recipes.
The old `experiments/ai-upscale/export_runtime.py` command delegates here by default.

## Deliberately revise approved artwork

Work from originals or the historical recipes into staging and visually review
before promoting results. The legacy regeneration route remains available:

```sh
python3 experiments/ai-upscale/export_runtime.py --from-experiments
python3 experiments/ai-upscale/validate_runtime.py
# After review, promote the complete selection and verify packaging:
python3 tools/artwork/package_runtime.py --capture-approved
python3 tools/artwork/package_runtime.py --check
python3 tools/artwork/runtime_provenance.py
```

Native original exporters still update `derived/`; use the explicit experimental
regeneration route to incorporate those into a new approved selection. Capture
rejects obsolete extra files after a category change; review and remove the old
category copy before capturing again. Do not confuse routine packaging with
promotion of new artwork. Commit production inputs and runtime outputs together.
