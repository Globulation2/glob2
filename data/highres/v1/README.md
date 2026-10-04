# High-resolution runtime pack

2287 registered frames combine approved original-derived artwork, 1,792
unit poses rendered from preserved Blender sources, eight SVG farm markers,
AI-enhanced sprite finals, generated terrain/water materials and resampled masks.
Native sprites remain in `data/gfx`; logical sizes, team colors and animation
cadence are preserved. Unsupported backends and missing frames use classic art.

Approved inputs live in `datasrc/gfx/production`; package them with
`tools/artwork/package_runtime.py`. `manifest.json` records provenance and hashes;
`frames.txt` is the runtime lookup. Terrain/resource atlases contain padded mip
levels matching these frames. See `docs/assets/high-resolution/README.md`,
`tools/artwork/ai/README.md`, and `tools/unit-animation/README.md` for maintenance.
