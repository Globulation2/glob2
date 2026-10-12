# Produce and review artwork

Artwork production starts with preserved editable sources and ends with a
validated runtime export. The source archive, reviewed production inputs and
runtime bundle have different jobs.

## Preserve and choose inputs

Start with [original artwork](../../datasrc/gfx/README.md), its source catalog,
[building mappings](../../datasrc/gfx/building-map.md) and
[coverage limitations](../../datasrc/gfx/coverage.md). Preserve originals byte for
byte. Work on copies; record source hashes, authorship/license and export settings.
A larger canvas alone does not establish usable detail or a correct sprite mapping.

Use recovered models, vectors and layered sources when suitable. Retain reviewed
fallbacks where source coverage or registration is uncertain. Consult
[third-party attribution](source-attribution.md) before adding outside artwork.

## Stage a family

- Original layers: follow [export recipes](../../datasrc/gfx/recovered-runtime.md).
- Units: use the pinned [Blender animation workflow](../../tools/unit-animation/README.md).
- Terrain: follow [material production](terrain-production.md), including shared edges.
- Authored vectors: use `tools/artwork/render_authored.py` and retained SVG sources.
- AI candidates: use the [candidate pipeline](../../tools/artwork/ai/README.md) in a fresh staging directory.

Review logical size, padding, alpha, shadows, neutral/team layers, all states,
animation directions and every mip. Compare at actual gameplay size as well as
zoomed inspection. Do not promote candidates automatically or overwrite originals.

## Assemble approved inputs

Approved production inputs are under `datasrc/gfx/production/`. Assembly reproduces
reviewed final pixels without model inference:

```sh
python3 tools/artwork/package_runtime.py --runtime-output artifacts/artwork-runtime
python3 tools/artwork/validate_runtime.py --export artifacts/artwork-runtime
```

Use `--lossless-images` when exact RGBA comparison is required. The client loads
encoded WebP artwork; source PNGs and provenance manifests serve editing and
reproduction. Release encoding uses the shared asset packager rather than an
independent encoder. See the [high-resolution pack](high-resolution/README.md)
for recipes and fallback behavior.

## Verify and record review

```sh
python3 tools/artwork/package_runtime.py --check
python3 tools/artwork/runtime_provenance.py --check
python3 tools/artwork/validate_runtime.py
```

Run affected image/sprite/high-resolution integration suites and inspect the result
in the relevant renderer, zoom and classic/fallback modes. Terrain joins need their
family validator; unit changes need animation and team-layer review. Keep commands,
source/build identity, screenshots and any platform omissions with accessible PR
evidence. Appearance, animation and palette can change game feel even when
simulation remains identical.

The [runtime inventory](high-resolution/ASSET-PROVENANCE.md) is generated. Update
its source manifest/generator inputs and regenerate it; do not hand-edit rows.

Related: [asset production](README.md).
