# Approved artwork inputs

Production inputs are separated by origin: `original-derived/` contains preserved
artist exports and Blender unit renders; `authored/` contains rendered SVG
markers; `ai-upscaled/` contains reviewed enhanced sprites; `ai-materials/`
contains historical generated terrain and water; `original-materials/` contains
original-based grass, sand and their connected composites; `procedural-materials/` contains the
HD frames of the procedurally synthesised terrain catalogue; `resampled-masks/` contains deterministic
mask resizes. `atlases/` holds the padded mip levels, and `pack-metadata/` the
PNG source index, provenance and hashes. Classic fallback assets remain unchanged.

Run `python3 tools/artwork/package_runtime.py --check` before packaging, then
`python3 tools/artwork/package_runtime.py`. No model inference runs during either
operation. This assembles lossless PNG sources; the shared client exporter
`tools/package_assets.py` builds WebP runtime artwork and rewrites its index.
Use `package_runtime.py --runtime-output artifacts/ai-runtime` to perform both
steps and `validate_runtime.py --export artifacts/ai-runtime` to check decoded
runtime alpha, geometry, hashes and encoding policy. Future candidates use the staging-only workflow in
[`tools/artwork/ai/README.md`](../../../tools/artwork/ai/README.md); promote a
complete reviewed selection explicitly, with base/team pairs and hashes intact.
The [unit pipeline](../../../tools/unit-animation/README.md) preserves the
original Blender sources and controls animation exports.
Reproduce terrain using `tools/artwork/terrain_materials.py`; its retained sources,
settings and mask records are under `datasrc/gfx/derived/terrain-materials-v1`. Packaging copies these
approved finals and never generates new texture details.
