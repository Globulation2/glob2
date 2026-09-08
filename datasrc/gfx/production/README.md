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
Historical experiments and generation trials are excluded from Git.

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


## Update an approved selection

Export original artwork into a staging directory, review the final frames and
update the matching origin folder. Keep base/team layers together. Refresh the
runtime manifest and package hashes when promoting a changed selection.
`--capture-approved` can import a complete reviewed staging pack from the runtime
location; it is an explicit maintenance operation, not part of normal builds.
Commit approved production inputs and assembled runtime outputs together.
