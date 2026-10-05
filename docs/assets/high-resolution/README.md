# High-resolution artwork pack

The pack contains the organized artist originals, 60 verified original-derived
world frames, and 1,792 unit animation poses rendered from the original Blender
rigs. The pack also includes reviewed AI-enhanced sprites, generated terrain and water,
resampled masks, and complete padded resource/terrain mip atlases.
All overlapping original-derived frames remain byte-identical.

World artwork includes both hives, 3 flags, 5 construction frames, 10 trees, 8 wheat, 5 papyrus,
completed middle school, first two racetracks and 24 area markers, plus the
hand-authored farm-area marker (8 frames drawn as SVG in `datasrc/gfx/authored`).

Assemble approved PNG sources: `python3 tools/artwork/package_runtime.py`.
Client builds encode those sources with the shared WebP pipeline. For a complete
runtime export, use `python3 tools/artwork/package_runtime.py --runtime-output
artifacts/ai-runtime`; add `--lossless-images` for exact RGBA comparisons.
Release exports use the shared Q90/lossless size-selection policy.
Validate: `python3 tools/artwork/validate_runtime.py` and the original family
validators under tools/artwork. Sources and staging exports are preserved in
datasrc/gfx; production/original-derived contains approved final layers.
Validate the built WebP layers, atlases and index with
`python3 tools/artwork/validate_runtime.py --export artifacts/ai-runtime`.
The HD terrain atlas covers 272 legacy connected tiles. Additional experimental
terrain uses the shared tileset compiler and native fallback.
AI candidates are generated into fresh staging directories only; see the
[candidate pipeline](../../../tools/artwork/ai/README.md). Production packaging
reproduces the committed reviewed finals without model inference.

Unit textures render onto a fixed 128×128 pixel canvas (4× for the 32px-native
explorer set, ~3.37× and 3.2× for the 38px/40px-native worker and warrior sets),
while the native 32-pose sprites remain in `data/gfx`. This preserves logical sprite size,
32 poses per direction and the normal 25 FPS display cadence. The seven sets
cover explorer flight, worker walk/swim/harvest-build, and warrior walk/swim/fight.
The classic artwork setting and software backend retain native unit textures.
See [the unit pipeline](../../../tools/unit-animation/README.md) for reproducible
render settings, layer mapping, CPU limits and validation.
